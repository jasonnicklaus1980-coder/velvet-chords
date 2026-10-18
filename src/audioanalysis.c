#include "audioanalysis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* No maths library: the plugin links only libc (scripts/check_so.sh refuses libm). Sine and cosine come from a Taylor
 * series after range reduction, and every comparison is done on squared values, so no square root is needed. */
#define VA_PI 3.14159265358979323846
#define VA_FFT 4096
#define VA_HOP 2048
#define VA_MIN_FRAMES 8     /* a chord lasts at least this many frames (about 0.4 s at 44.1 kHz) */
#define VA_CHANGE_COS2 0.80 /* a frame whose chroma points less than cos 0.89 from the segment is a chord change */
#define VA_HALF 3                   /* median over 3 frames either side: 7 frames, about 0.32 s (a 40 ms drum hit touches at most 3) */
#define VA_HOP_R_SPAN (2 * VA_HALF + 1)
#define VA_MAX_CAND (VA_MAX_SEG * 4) /* candidate segments kept before the strongest VA_MAX_SEG are chosen */

static double va_sin(double x) {
    while (x > VA_PI) x -= 2 * VA_PI;
    while (x < -VA_PI) x += 2 * VA_PI;
    double term = x, sum = x, x2 = x * x;
    for (int k = 1; k <= 10; k++) {
        term *= -x2 / ((2.0 * k) * (2.0 * k + 1.0));
        sum += term;
    }
    return sum;
}

static double va_cos(double x) { return va_sin(x + VA_PI / 2); }

static unsigned va_le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned va_le32(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24); }

/* pitch class of a frequency, by nearest equal-tempered semitone (A = 9) */
static int va_pc_of_freq(double f, const double *mid) {
    double x = f / 440.0;
    int o = 0;
    while (x >= 2.0) { x *= 0.5; o++; }
    while (x < 1.0) { x *= 2.0; o--; }
    int j = 0;
    while (j < 12 && x >= mid[j]) j++;
    int n = 12 * o + j;
    return ((9 + n) % 12 + 12) % 12;
}

static void va_fft(double *re, double *im, int n, const double *cs, const double *sn) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1, step = n / len;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; j++) {
                double wr = cs[j * step], wi = sn[j * step];
                double xr = re[i + j + half], xi = im[i + j + half];
                double vr = xr * wr - xi * wi, vi = xr * wi + xi * wr;
                double ur = re[i + j], ui = im[i + j];
                re[i + j] = ur + vr; im[i + j] = ui + vi;
                re[i + j + half] = ur - vr; im[i + j + half] = ui - vi;
            }
    }
}

static void va_msg(char *msg, int msglen, const char *s) {
    if (msg && msglen > 0) { strncpy(msg, s, (size_t)msglen - 1); msg[msglen - 1] = 0; }
}

int va_read_wav(const char *path, float **mono, int *n, int *rate, char *msg, int msglen) {
    *mono = 0; *n = 0; *rate = 0;
    FILE *f = fopen(path, "rb");
    if (!f) { va_msg(msg, msglen, "Could not open the file"); return -1; }
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        fclose(f); va_msg(msg, msglen, "Not a WAV file"); return -2;
    }
    int have_fmt = 0, fmt = 0, ch = 0, bits = 0, sr = 0;
    long data_size = 0;
    unsigned char c[8];
    while (fread(c, 1, 8, f) == 8) {
        long size = (long)va_le32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char b[40];
            long want = size < 40 ? size : 40;
            if (want < 16 || fread(b, 1, (size_t)want, f) != (size_t)want) { fclose(f); va_msg(msg, msglen, "Bad WAV format chunk"); return -3; }
            fmt = (int)va_le16(b);
            ch = (int)va_le16(b + 2);
            sr = (int)va_le32(b + 4);
            bits = (int)va_le16(b + 14);
            if (fmt == 0xFFFE && want >= 26) fmt = (int)va_le16(b + 24); /* extensible: the sub-format says what the samples are */
            have_fmt = 1;
            if (size - want > 0) fseek(f, size - want, SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            if (!have_fmt) { fclose(f); va_msg(msg, msglen, "WAV data before the format"); return -4; }
            data_size = size;
            break;
        } else {
            fseek(f, size, SEEK_CUR);
        }
        if (size & 1) fseek(f, 1, SEEK_CUR);
    }
    if (!have_fmt || !data_size) { fclose(f); va_msg(msg, msglen, "No audio data in the file"); return -5; }
    int is_float = fmt == 3, is_pcm = fmt == 1;
    if (!(is_float && bits == 32) && !(is_pcm && (bits == 16 || bits == 24))) {
        fclose(f); va_msg(msg, msglen, "Use a 16-bit, 24-bit PCM or 32-bit float WAV"); return -6;
    }
    if (ch < 1 || ch > 8 || sr < 8000 || sr > 192000) { fclose(f); va_msg(msg, msglen, "Unsupported channel count or rate"); return -7; }
    int bps = bits / 8, fb = bps * ch;
    long frames = data_size / fb;
    long cap = (long)VA_MAX_SECONDS * sr;
    if (frames > cap) frames = cap;
    float *out = (float *)malloc(sizeof(float) * (size_t)(frames > 0 ? frames : 1));
    if (!out) { fclose(f); va_msg(msg, msglen, "Out of memory"); return -8; }
    long chunk_frames = 4096;
    unsigned char *buf = (unsigned char *)malloc((size_t)(chunk_frames * fb));
    if (!buf) { free(out); fclose(f); va_msg(msg, msglen, "Out of memory"); return -8; }
    long done = 0;
    while (done < frames) {
        long take = frames - done < chunk_frames ? frames - done : chunk_frames;
        if (fread(buf, (size_t)fb, (size_t)take, f) != (size_t)take) { frames = done + 0; break; }
        for (long i = 0; i < take; i++) {
            double acc = 0;
            for (int k = 0; k < ch; k++) {
                const unsigned char *p = buf + (size_t)(i * fb + k * bps);
                if (is_float) {
                    unsigned u = va_le32(p);
                    float v;
                    memcpy(&v, &u, 4);
                    acc += (double)v;
                } else if (bps == 2) {
                    short s = (short)va_le16(p);
                    acc += (double)s / 32768.0;
                } else { /* 24-bit: three bytes, little-endian, signed */
                    int v = (int)p[0] | ((int)p[1] << 8) | ((int)p[2] << 16);
                    if (v & 0x800000) v -= 0x1000000;
                    acc += (double)v / 8388608.0;
                }
            }
            out[done + i] = (float)(acc / ch);
        }
        done += take;
    }
    free(buf);
    fclose(f);
    if (done == 0) { free(out); va_msg(msg, msglen, "No audio in the file"); return -9; }
    *mono = out;
    *n = (int)done;
    *rate = sr;
    return 0;
}

/* the Krumhansl-Schmuckler key profiles */
static const double VA_MAJ[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
static const double VA_MIN[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

int va_analyze(const float *mono, int n, int rate, VaResult *out, char *msg, int msglen) {
    memset(out, 0, sizeof(*out));
    out->seconds = (double)n / (double)rate;
    out->truncated = n >= VA_MAX_SECONDS * rate;
    if (!mono || n < 1 || rate < 8000) { va_msg(msg, msglen, "No audio to analyse"); return -1; }

    /* tables: window, FFT twiddles, pitch classes of the bins, and the semitone midpoints */
    static double cs[VA_FFT / 2], sn[VA_FFT / 2], win[VA_FFT];
    static double mid[12];
    double m = 1.0293022366434920; /* 2^(0.5/12) */
    for (int j = 0; j < 12; j++) { mid[j] = m; m *= 1.0594630943592953; }
    for (int k = 0; k < VA_FFT / 2; k++) {
        double a = 2.0 * VA_PI * k / VA_FFT;
        cs[k] = va_cos(a);
        sn[k] = -va_sin(a);
    }
    for (int k = 0; k < VA_FFT; k++) win[k] = 0.5 - 0.5 * va_cos(2.0 * VA_PI * k / (VA_FFT - 1));

    /* pad short files to one frame, then take frames every VA_HOP samples */
    long total = n < VA_FFT ? VA_FFT : n;
    int frames = (int)((total - VA_FFT) / VA_HOP + 1);
    double *fc = (double *)calloc((size_t)frames * 12, sizeof(double));
    double *fb = (double *)calloc((size_t)frames * 12, sizeof(double));
    char *silent = (char *)calloc((size_t)frames, 1);
    double *re = (double *)malloc(sizeof(double) * VA_FFT), *im = (double *)malloc(sizeof(double) * VA_FFT);
    double *ring = (double *)malloc(sizeof(double) * (VA_HOP_R_SPAN) * (VA_FFT / 2));
    double *H = (double *)malloc(sizeof(double) * (VA_FFT / 2));
    if (!fc || !fb || !silent || !re || !im || !ring || !H) {
        free(fc); free(fb); free(silent); free(re); free(im); free(ring); free(H);
        va_msg(msg, msglen, "Out of memory"); return -2;
    }
    /* silence first, from the time signal alone */
    for (int f = 0; f < frames; f++) {
        long start = (long)f * VA_HOP;
        double energy = 0;
        for (int k = 0; k < VA_FFT; k++) {
            double x = (start + k < n) ? (double)mono[start + k] : 0.0;
            energy += x * x;
        }
        silent[f] = energy < 1e-6 * VA_FFT; /* RMS below 0.001 */
    }
    /* power spectrum of frame f into its ring slot; the ring holds the frames around the one being analysed */
#define VA_SLOT(f) (ring + (size_t)((f) % VA_HOP_R_SPAN) * (VA_FFT / 2))
    for (int f = 0; f < frames; f++) {
        if (f + VA_HALF < frames || f == 0) {
            int first = f == 0 ? 0 : f + VA_HALF;
            int last = f == 0 ? (VA_HALF < frames ? VA_HALF : frames - 1) : f + VA_HALF;
            for (int g = first; g <= last; g++) {
                long start = (long)g * VA_HOP;
                for (int k = 0; k < VA_FFT; k++) {
                    double x = (start + k < n) ? (double)mono[start + k] : 0.0;
                    re[k] = x * win[k];
                    im[k] = 0;
                }
                va_fft(re, im, VA_FFT, cs, sn);
                double *sp = VA_SLOT(g);
                for (int k = 0; k < VA_FFT / 2; k++) sp[k] = re[k] * re[k] + im[k] * im[k];
            }
        }
        if (silent[f]) continue;
        /* harmonic part: the median over the frames around f, per bin. A drum hit lasts one or two frames, a chord
         * lasts many, so the median keeps the chord and drops the hit. */
        int lo = f - VA_HALF < 0 ? 0 : f - VA_HALF, hi = f + VA_HALF >= frames ? frames - 1 : f + VA_HALF;
        int cnt = hi - lo + 1;
        for (int k = 1; k < VA_FFT / 2; k++) {
            double v[VA_HOP_R_SPAN];
            for (int g = lo; g <= hi; g++) v[g - lo] = VA_SLOT(g)[k];
            for (int i = 1; i < cnt; i++) { double t = v[i]; int j = i - 1; while (j >= 0 && v[j] > t) { v[j + 1] = v[j]; j--; } v[j + 1] = t; }
            H[k] = v[cnt / 2];
        }
        double *c = fc + (size_t)f * 12, *bs = fb + (size_t)f * 12;
        /* each spectral peak, at its interpolated frequency: 10.8 Hz bins are too coarse to name a note below a few
         * hundred hertz, so a bin's energy is assigned to a note only through the peak it belongs to */
        for (int k = 2; k < VA_FFT / 2 - 1; k++) {
            if (!(H[k] > H[k - 1] && H[k] >= H[k + 1])) continue;
            double a = H[k - 1], b = H[k], cc = H[k + 1], den = a - 2 * b + cc;
            double d = den < 0 ? 0.5 * (a - cc) / den : 0;
            if (d > 0.5) d = 0.5;
            if (d < -0.5) d = -0.5;
            double fr = (k + d) * (double)rate / VA_FFT;
            if (fr >= 65.0 && fr <= 2100.0) c[va_pc_of_freq(fr, mid)] += H[k];
            if (fr >= 40.0 && fr <= 260.0) bs[va_pc_of_freq(fr, mid)] += H[k];
        }
        double mx = 0, bmx = 0;
        for (int j = 0; j < 12; j++) { if (c[j] > mx) mx = c[j]; if (bs[j] > bmx) bmx = bs[j]; }
        if (mx <= 0) { silent[f] = 1; continue; }
        for (int j = 0; j < 12; j++) { c[j] /= mx; bs[j] = bmx > 0 ? bs[j] / bmx : 0; }
    }
    free(re); free(im); free(ring); free(H);

    /* segments: a new chord starts when a frame's chroma points away from the running segment's chroma */
    double sumc[12], sumb[12];
    int have = 0, seg_first = 0, seg_last = 0, count = 0;
    memset(sumc, 0, sizeof(sumc)); memset(sumb, 0, sizeof(sumb));
    int cand_first[VA_MAX_CAND], cand_last[VA_MAX_CAND];
    double cand_c[VA_MAX_CAND][12], cand_b[VA_MAX_CAND][12];
    int ncand = 0;
    for (int f = 0; f <= frames; f++) {
        int end_here = f == frames || silent[f];
        if (!end_here && have && count >= VA_MIN_FRAMES) {
            const double *c = fc + (size_t)f * 12;
            double dot = 0, nf = 0, ns = 0;
            for (int j = 0; j < 12; j++) { dot += c[j] * sumc[j]; nf += c[j] * c[j]; ns += sumc[j] * sumc[j]; }
            if (dot <= 0 || dot * dot < VA_CHANGE_COS2 * nf * ns) end_here = 1;
        }
        if (end_here && have) {
            if (count >= 3 && ncand < VA_MAX_CAND) {
                cand_first[ncand] = seg_first; cand_last[ncand] = seg_last;
                memcpy(cand_c[ncand], sumc, sizeof(sumc)); memcpy(cand_b[ncand], sumb, sizeof(sumb));
                ncand++;
            }
            have = 0; count = 0;
            memset(sumc, 0, sizeof(sumc)); memset(sumb, 0, sizeof(sumb));
        }
        if (f < frames && !silent[f]) {
            const double *c = fc + (size_t)f * 12, *bs = fb + (size_t)f * 12;
            if (!have) { have = 1; seg_first = f; }
            seg_last = f;
            count++;
            for (int j = 0; j < 12; j++) { sumc[j] += c[j]; sumb[j] += bs[j]; }
        }
    }

    /* key: the summed chroma of the whole file against the twelve major and twelve minor profiles */
    double K[12] = {0}, mk = 0;
    for (int f = 0; f < frames; f++) if (!silent[f]) for (int j = 0; j < 12; j++) K[j] += fc[(size_t)f * 12 + j];
    for (int j = 0; j < 12; j++) mk += K[j];
    mk /= 12;
    double ssk = 0;
    for (int j = 0; j < 12; j++) { K[j] -= mk; ssk += K[j] * K[j]; }
    double bestr2 = -1;
    int bestkey = 0, bestminor = 0;
    if (ssk > 0)
        for (int mode = 0; mode < 2; mode++) {
            const double *P = mode ? VA_MIN : VA_MAJ;
            double mp = 0, ssp = 0;
            for (int j = 0; j < 12; j++) mp += P[j];
            mp /= 12;
            for (int j = 0; j < 12; j++) ssp += (P[j] - mp) * (P[j] - mp);
            for (int t = 0; t < 12; t++) {
                double num = 0;
                for (int j = 0; j < 12; j++) num += K[j] * (P[((j - t) % 12 + 12) % 12] - mp);
                double r2 = num > 0 ? num * num / (ssk * ssp) : 0;
                if (r2 > bestr2) { bestr2 = r2; bestkey = t; bestminor = mode; }
            }
        }
    out->key = bestkey;
    out->minor = bestminor;
    out->key_conf = bestr2 > 0 ? (float)bestr2 : 0.f;

    /* classify each segment against every chord in the table, with the bass note breaking ties */
    int nseg = 0;
    for (int s = 0; s < ncand && nseg < VA_MAX_SEG; s++) {
        const double *C = cand_c[s], *B = cand_b[s];
        double ss = 0, bsum = 0, bmax = 0;
        int bass = -1;
        for (int j = 0; j < 12; j++) {
            ss += C[j] * C[j];
            bsum += B[j];
            if (B[j] > bmax) { bmax = B[j]; bass = j; }
        }
        if (ss <= 0) continue;
        if (bsum <= 0) bass = -1;
        /* score: energy the chord explains, minus energy it leaves out, minus a small price for each note past a triad
         * (so 9ths only win when their notes carry real energy), plus a bonus when the chord's root is the bass */
        double tot = 0, mx = 0;
        for (int j = 0; j < 12; j++) { tot += C[j]; if (C[j] > mx) mx = C[j]; }
        if (tot <= 0) continue;
        double best = -1e30, second = -1e30, best_expl = 0;
        int br = 0, bq = 0, sr_ = 0, sq = 0;
        for (int r = 0; r < 12; r++)
            for (int q = 0; q < VC_NUM_QUALITIES; q++) {
                int mask = 0;
                for (int i = 0; i < VC_QUALITIES[q].n; i++) mask |= 1 << (((r + VC_QUALITIES[q].iv[i]) % 12 + 12) % 12);
                int msz = 0;
                double s_in = 0;
                for (int j = 0; j < 12; j++) if (mask & (1 << j)) { msz++; s_in += C[j]; }
                if (msz == 0) continue;
                double s_out = tot - s_in;
                double expl = s_in - s_out;
                double score = expl - 0.05 * mx * (msz > 3 ? msz - 3 : 0) + (r == bass ? 0.05 * tot : 0);
                if (score > best) {
                    second = best; sr_ = br; sq = bq;
                    best = score; best_expl = expl; br = r; bq = q;
                } else if (score > second) {
                    second = score; sr_ = r; sq = q;
                }
            }
        if (best < -1e29) continue;
        double second_expl = 0;
        if (second > -1e29) {
            /* the runner-up's explained share, for the margin */
            int mask = 0;
            for (int i = 0; i < VC_QUALITIES[sq].n; i++) mask |= 1 << (((sr_ + VC_QUALITIES[sq].iv[i]) % 12 + 12) % 12);
            double s_in = 0;
            for (int j = 0; j < 12; j++) if (mask & (1 << j)) s_in += C[j];
            second_expl = s_in - (tot - s_in);
        }
        VaSegment *g = &out->seg[nseg];
        g->start = (double)cand_first[s] * VA_HOP / rate;
        g->end = ((double)cand_last[s] * VA_HOP + VA_FFT) / rate;
        if (g->end > out->seconds) g->end = out->seconds;
        g->root = br;
        g->quality = bq;
        g->bass = bass;
        g->conf = (float)(best_expl / tot);
        if (g->conf < 0) g->conf = 0;
        if (g->conf > 1) g->conf = 1;
        g->margin = (float)((best_expl - second_expl) / tot);
        if (g->margin < 0) g->margin = 0;
        if (g->margin > 1) g->margin = 1;
        g->alt_root = sr_;
        g->alt_quality = sq;
        nseg++;
    }
    out->nseg = nseg;
    free(fc); free(fb); free(silent);
    va_msg(msg, msglen, nseg ? "Analysed" : "No chords found: the audio is silent or too quiet");
    return 0;
}
