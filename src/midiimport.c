#include "midiimport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { long t0, t1; unsigned char note; } Note;

static unsigned long rd_vlq(const unsigned char *b, long len, long *i) {
    unsigned long v = 0;
    for (int k = 0; k < 4 && *i < len; k++) {
        unsigned char c = b[(*i)++];
        v = (v << 7) | (c & 0x7F);
        if (!(c & 0x80)) break;
    }
    return v;
}

static int popcount12(int m) { int n = 0; for (int i = 0; i < 12; i++) n += (m >> i) & 1; return n; }

/* name a set of notes: exact chord-table match, root on the bass first; else drop one tension; else custom */
void vc_identify_chord(const unsigned char *notes, int n, ChordSpec *out) {
    memset(out, 0, sizeof(*out));
    out->bass = -1;
    if (n <= 0) return;
    int mask = 0, lowpc = notes[0] % 12;
    for (int i = 0; i < n; i++) mask |= 1 << (notes[i] % 12);
    for (int pass = 0; pass < 2; pass++) {
        int order[12], no = 0;
        order[no++] = lowpc;
        for (int r = 0; r < 12; r++) if (r != lowpc && (mask & (1 << r))) order[no++] = r;
        for (int drop = -1; drop < 12; drop++) {
            int m = mask;
            if (drop >= 0) { if (!(m & (1 << drop)) || drop == lowpc || pass == 0) continue; m &= ~(1 << drop); }
            for (int oi = 0; oi < no; oi++) {
                int r = order[oi];
                if (!(m & (1 << r))) continue;
                int rel = 0;
                for (int p = 0; p < 12; p++) if (m & (1 << p)) rel |= 1 << ((p - r + 12) % 12);
                int q = vc_quality_match(rel, popcount12(m));
                if (q >= 0) {
                    out->root = r; out->quality = q;
                    out->bass = (r == lowpc) ? -1 : lowpc;   /* not on the root: slash chord */
                    return;
                }
            }
            if (pass == 0) break;
        }
    }
    /* unnamed: intervals above the lowest note */
    out->root = lowpc; out->quality = -1;
    for (int p = 0; p < 12 && out->ncustom < VC_MAX_IV; p++) if (mask & (1 << ((lowpc + p) % 12))) out->custom[out->ncustom++] = (signed char)p;
}

int vc_midi_import(const char *path, ImportChord *out, int max, char *err, int errlen) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, (size_t)errlen, "Can't open file"); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 14 || len > 4 * 1024 * 1024) { fclose(f); snprintf(err, (size_t)errlen, "Not a MIDI file (or too big)"); return -1; }
    unsigned char *b = (unsigned char *)malloc((size_t)len);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(b); snprintf(err, (size_t)errlen, "Read error"); return -1; }
    fclose(f);
    if (memcmp(b, "MThd", 4)) { free(b); snprintf(err, (size_t)errlen, "Not a MIDI file"); return -1; }
    int ntrk = (b[10] << 8) | b[11], div = (b[12] << 8) | b[13];
    if (div & 0x8000) div = 480; /* SMPTE timing: treat as 480 ppq for grouping purposes */
    if (div <= 0) div = 480;
    int cap = 8192, nn = 0;
    Note *notes = (Note *)malloc(sizeof(Note) * (size_t)cap);
    long open_t[16][128];
    for (int c = 0; c < 16; c++) for (int k = 0; k < 128; k++) open_t[c][k] = -1;
    /* lengths are unsigned 32-bit: a byte of 0x80 or more must not overflow a signed int */
    unsigned long hlen = ((unsigned long)b[4] << 24) | ((unsigned long)b[5] << 16) | ((unsigned long)b[6] << 8) | b[7];
    if (hlen > (unsigned long)len) { free(b); free(notes); snprintf(err, (size_t)errlen, "Bad MIDI header"); return -1; }
    long i = 8 + (long)hlen;
    for (int t = 0; t < ntrk && i + 8 <= len && notes; t++) {
        if (memcmp(b + i, "MTrk", 4)) break;
        unsigned long tl = ((unsigned long)b[i + 4] << 24) | ((unsigned long)b[i + 5] << 16) | ((unsigned long)b[i + 6] << 8) | b[i + 7];
        long j = i + 8, end = (tl > (unsigned long)(len - j)) ? len : j + (long)tl;
        long tick = 0; unsigned char run = 0;
        while (j < end) {
            tick += (long)rd_vlq(b, end, &j);
            if (j >= end) break;
            unsigned char st = b[j];
            if (st == 0xFF) { j += 2; long l = (long)rd_vlq(b, end, &j); j += l; continue; }
            if (st == 0xF0 || st == 0xF7) { j++; long l = (long)rd_vlq(b, end, &j); j += l; continue; }
            if (st & 0x80) { run = st; j++; }
            if (!run) break;
            int ty = run & 0xF0, ch = run & 0x0F;
            int d1 = j < end ? b[j] : 0, d2 = (ty != 0xC0 && ty != 0xD0 && j + 1 < end) ? b[j + 1] : 0;
            j += (ty == 0xC0 || ty == 0xD0) ? 1 : 2;
            if (ch == 9) continue; /* drums */
            if (ty == 0x90 && d2 > 0) {
                if (open_t[ch][d1 & 127] >= 0 && nn < cap) { notes[nn].t0 = open_t[ch][d1 & 127]; notes[nn].t1 = tick; notes[nn].note = (unsigned char)(d1 & 127); nn++; }
                open_t[ch][d1 & 127] = tick;
            } else if (ty == 0x80 || ty == 0x90) {
                long s = open_t[ch][d1 & 127];
                if (s >= 0 && nn < cap) { notes[nn].t0 = s; notes[nn].t1 = tick > s ? tick : s + 1; notes[nn].note = (unsigned char)(d1 & 127); nn++; }
                open_t[ch][d1 & 127] = -1;
            }
        }
        i = end;
    }
    free(b);
    if (!notes) { snprintf(err, (size_t)errlen, "Not enough memory"); return -1; }
    if (nn == 0) { free(notes); snprintf(err, (size_t)errlen, "No notes found (drum-only or empty file)"); return -1; }
    /* onset clusters: notes starting within a 32nd note of each other belong to one chord */
    long thr = div / 8 > 0 ? div / 8 : 1;
    int count = 0, last_mask = -1, last_low = -1;
    long t = -1;
    for (;;) {
        long next = -1;
        for (int k = 0; k < nn; k++) if (notes[k].t0 > t && (next < 0 || notes[k].t0 < next)) next = notes[k].t0;
        if (next < 0 || count >= max) break;
        long probe = next + thr;
        /* struck together: the notes that start in this cluster name the chord */
        unsigned char set[128]; int ns = 0;
        /* held: notes that started earlier and still sound (a pedal or held bass); they never name the chord,
         * but a held note below the struck notes is the chord's bass */
        unsigned char held[128]; int nh = 0;
        for (int k = 0; k < nn; k++) {
            if (notes[k].t0 >= next && notes[k].t0 <= probe) {
                int dup = 0;
                for (int s = 0; s < ns; s++) if (set[s] == notes[k].note) dup = 1;
                if (!dup && ns < 128) set[ns++] = notes[k].note;
            } else if (notes[k].t0 < next && notes[k].t1 > next + thr / 2) {
                int dup = 0;
                for (int s = 0; s < nh; s++) if (held[s] == notes[k].note) dup = 1;
                if (!dup && nh < 128) held[nh++] = notes[k].note;
            }
        }
        t = probe;
        /* sort ascending */
        for (int a = 1; a < ns; a++) { unsigned char v = set[a]; int c = a - 1; while (c >= 0 && set[c] > v) { set[c + 1] = set[c]; c--; } set[c + 1] = v; }
        int mask = 0;
        for (int s = 0; s < ns; s++) mask |= 1 << (set[s] % 12);
        if (popcount12(mask) < 2) continue;               /* single notes / octaves aren't chords */
        if (mask == last_mask && set[0] % 12 == last_low) continue; /* same chord re-struck */
        int bass_note = -1;
        for (int h = 0; h < nh; h++) if (held[h] < set[0] && (bass_note < 0 || held[h] < bass_note)) bass_note = held[h];
        ImportChord *ic = &out[count++];
        memset(ic, 0, sizeof(*ic));
        int keep = bass_note >= 0 ? 1 : 0;
        ic->n = ns + keep > VC_MAX_NOTES ? VC_MAX_NOTES : ns + keep;
        if (keep) ic->notes[0] = (unsigned char)bass_note;
        memcpy(ic->notes + keep, set, (size_t)(ic->n - keep));
        vc_identify_chord(set, ns > VC_MAX_NOTES ? VC_MAX_NOTES : ns, &ic->spec);
        if (keep && (bass_note % 12) != ic->spec.root) ic->spec.bass = bass_note % 12; /* slash chord over a held bass */
        last_mask = mask; last_low = set[0] % 12;
    }
    free(notes);
    if (!count) { snprintf(err, (size_t)errlen, "No chords found (melody or single notes only)"); return -1; }
    return count;
}
