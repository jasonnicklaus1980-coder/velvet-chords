#include "kit.h"
#include "vcmath.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------- reading files ---------- */

static unsigned char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    unsigned char *b = malloc(cap);
    while (b) {
        if (n == cap) {
            unsigned char *nb = realloc(b, cap * 2);
            if (!nb) { free(b); b = NULL; break; }
            b = nb;
            cap *= 2;
        }
        size_t got = fread(b + n, 1, cap - n, f);
        if (got == 0) break;
        n += got;
    }
    fclose(f);
    if (b) *len = n;
    return b;
}

static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned rd32(const unsigned char *p) { return rd16(p) | rd16(p + 2) << 16; }

static int16_t clamp16(long v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

/* ---------- WAV ---------- */

int kit_wav_load(const char *path, KitSample *out) {
    memset(out, 0, sizeof *out);
    size_t len = 0;
    unsigned char *b = read_file(path, &len);
    if (!b) return -1;
    int fmt_tag = 0, ch = 0, bits = 0, rate = 0, ok = -1;
    const unsigned char *data = NULL;
    size_t dlen = 0;
    if (len >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WAVE", 4) == 0) {
        size_t p = 12;
        while (p + 8 <= len) {
            size_t csize = rd32(b + p + 4);
            if (csize > len - p - 8) break;   /* a chunk cut short: the file is damaged */
            const unsigned char *body = b + p + 8;
            if (memcmp(b + p, "fmt ", 4) == 0 && csize >= 16) {
                fmt_tag = (int)rd16(body);
                ch = (int)rd16(body + 2);
                rate = (int)rd32(body + 4);
                bits = (int)rd16(body + 14);
                if (fmt_tag == 0xFFFE && csize >= 26) fmt_tag = (int)rd16(body + 24);   /* extensible: sub-format tag */
            } else if (memcmp(b + p, "data", 4) == 0) {
                data = body;
                dlen = csize;
            }
            p += 8 + csize + (csize & 1);
        }
    }
    int valid = data && (ch == 1 || ch == 2) && rate > 0 &&
                ((fmt_tag == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmt_tag == 3 && bits == 32));
    if (valid) {
        int bps = bits / 8;
        size_t frames = dlen / ((size_t)bps * (size_t)ch);
        if (frames > 0) {
            int16_t *pcm = malloc(frames * (size_t)ch * sizeof *pcm);
            if (pcm) {
                for (size_t i = 0; i < frames * (size_t)ch; i++) {
                    const unsigned char *s = data + i * (size_t)bps;
                    if (fmt_tag == 3) {
                        float f;
                        memcpy(&f, s, 4);   /* little-endian, as MPC and the WAV files are */
                        pcm[i] = clamp16((long)(f * 32767.f + (f < 0 ? -0.5f : 0.5f)));
                    } else if (bits == 16) pcm[i] = (int16_t)rd16(s);
                    else if (bits == 24) pcm[i] = (int16_t)rd16(s + 1);   /* the top 16 bits */
                    else pcm[i] = (int16_t)rd16(s + 2);
                }
                out->data = pcm;
                out->frames = frames;
                out->channels = ch;
                out->rate = rate;
                ok = 0;
            }
        }
    }
    free(b);
    return ok;
}

void kit_sample_free(KitSample *s) {
    free(s->data);
    memset(s, 0, sizeof *s);
}

/* ---------- the kit ---------- */

/* the folder part of a path: "." when there's none */
static void folder_of(const char *path, char *out, size_t len) {
    const char *slash = strrchr(path, '/');
    if (!slash) { snprintf(out, len, "."); return; }
    size_t n = (size_t)(slash - path);
    if (n == 0) n = 1;   /* a file at the root */
    if (n >= len) n = len - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

/* finds sample.WAV in dir, whatever the case of the name the MPC or a computer saved it with */
static int find_wav(const char *dir, const char *sample, char *out, size_t len) {
    char want[300];
    snprintf(want, sizeof want, "%s.wav", sample);
    DIR *d = opendir(dir);
    if (!d) return -1;
    int found = -1;
    struct dirent *de;
    while (found != 0 && (de = readdir(d)) != NULL) {
        if (strcasecmp(de->d_name, want) == 0) {
            snprintf(out, len, "%s/%s", dir, de->d_name);
            found = 0;
        }
    }
    closedir(d);
    return found;
}

Kit *kit_new(const char *xpm_path) {
    if (!xpm_path) return NULL;
    size_t len = 0;
    unsigned char *text = read_file(xpm_path, &len);
    if (!text) return NULL;
    Kit *k = calloc(1, sizeof *k);
    XpmProgram *prog = malloc(sizeof *prog);
    int ok = k && prog && xpm_read((const char *)text, len, prog) == 0;
    free(text);
    if (!ok) { free(prog); free(k); return NULL; }
    k->prog = prog;
    snprintf(k->name, sizeof k->name, "%s", prog->name);
    folder_of(xpm_path, k->dir, sizeof k->dir);
    return k;
}

int kit_load_instrument(Kit *k, int i) {
    if (!k || !k->prog || i < 0 || i >= k->prog->ninstruments) return -1;
    const XpmInstrument *xi = &k->prog->instruments[i];
    int slot = xi->number - 1;
    if (slot < 0 || slot >= XPM_MAX_INSTRUMENTS) return -1;
    KitInst *ki = &k->inst[slot];
    int n = 0;
    for (int l = 0; l < xi->nlayers && n < XPM_MAX_LAYERS; l++) {
        const XpmLayer *xl = &xi->layers[l];
        if (!xl->active || !xl->sample[0]) continue;
        char path[2 * KIT_PATH_LEN];   /* a folder path plus a file name */
        KitLayer *kl = &ki->layers[n];
        if (find_wav(k->dir, xl->sample, path, sizeof path) != 0 || kit_wav_load(path, &kl->s) != 0) continue;
        kl->vel_start = xl->vel_start;
        kl->vel_end = xl->vel_end;
        kl->one_shot = xi->one_shot;
        kl->amp = (float)vc_exp(xl->gain_db / 20.0 * 2.302585092994046);   /* 10^(dB/20) */
        kl->pan = (float)((xl->pan + 1.0) * 0.5);
        n++;
    }
    ki->nlayers = n;
    __atomic_store_n(&ki->ready, 1, __ATOMIC_RELEASE);
    return 0;
}

void kit_free(Kit *k) {
    if (!k) return;
    for (int s = 0; s < XPM_MAX_INSTRUMENTS; s++)
        for (int l = 0; l < k->inst[s].nlayers; l++) kit_sample_free(&k->inst[s].layers[l].s);
    free(k->prog);
    free(k);
}

/* ---------- playing ---------- */

void kit_player_reset(KitPlayer *kp) {
    for (int i = 0; i < KIT_VOICES; i++) kp->v[i].active = 0;
}

void kit_note_on(KitPlayer *kp, const Kit *k, int idx, int vel, double sr) {
    if (!k || idx < 0 || idx >= XPM_MAX_INSTRUMENTS || vel < 1 || sr <= 0) return;
    const KitInst *ki = &k->inst[idx];
    if (!__atomic_load_n(&ki->ready, __ATOMIC_ACQUIRE)) return;
    const KitLayer *layer = NULL;
    for (int l = 0; l < ki->nlayers && !layer; l++) {
        const KitLayer *c = &ki->layers[l];
        if (vel >= c->vel_start && vel <= c->vel_end && c->s.data) layer = c;
    }
    if (!layer) return;
    KitVoice *v = NULL;
    for (int i = 0; i < KIT_VOICES && !v; i++) if (!kp->v[i].active) v = &kp->v[i];
    if (!v) {   /* all busy: take the oldest */
        v = &kp->v[0];
        for (int i = 1; i < KIT_VOICES; i++) if (kp->v[i].age < v->age) v = &kp->v[i];
    }
    float amp = layer->amp * (float)vel / 127.f;
    v->layer = layer;
    v->inst = idx;
    v->pos = 0;
    v->step = (double)layer->s.rate / sr;
    v->gl = amp * (1.f - layer->pan);
    v->gr = amp * layer->pan;
    v->age = ++kp->clock;
    v->active = 1;
}

void kit_note_off(KitPlayer *kp, int idx) {
    for (int i = 0; i < KIT_VOICES; i++) {
        KitVoice *v = &kp->v[i];
        if (v->active && v->inst == idx && !v->layer->one_shot) v->active = 0;
    }
}

void kit_player_follow(KitPlayer *kp, const Kit *k) {
    if (kp->seen != k) {   /* a new kit (or none): the old voices point into memory that may be gone */
        kit_player_reset(kp);
        kp->seen = k;
    }
}

void kit_render(KitPlayer *kp, const Kit *k, float *L, float *R, int n) {
    kit_player_follow(kp, k);
    if (!k) return;
    for (int i = 0; i < KIT_VOICES; i++) {
        KitVoice *v = &kp->v[i];
        if (!v->active) continue;
        const KitSample *s = &v->layer->s;
        for (int f = 0; f < n; f++) {
            if (v->pos >= (double)s->frames) { v->active = 0; break; }
            size_t i0 = (size_t)v->pos;
            size_t i1 = i0 + 1 < s->frames ? i0 + 1 : i0;
            float t = (float)(v->pos - (double)i0);
            float l0, r0, l1, r1;
            if (s->channels == 2) {
                l0 = s->data[i0 * 2] / 32768.f;  r0 = s->data[i0 * 2 + 1] / 32768.f;
                l1 = s->data[i1 * 2] / 32768.f;  r1 = s->data[i1 * 2 + 1] / 32768.f;
            } else {
                l0 = r0 = s->data[i0] / 32768.f;
                l1 = r1 = s->data[i1] / 32768.f;
            }
            L[f] += kp->gain * v->gl * ((1.f - t) * l0 + t * l1);
            R[f] += kp->gain * v->gr * ((1.f - t) * r0 + t * r1);
            v->pos += v->step;
        }
    }
}
