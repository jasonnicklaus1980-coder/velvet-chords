#include "export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PPQ 480

typedef struct { uint32_t tick; int order; unsigned char b[3]; int len; const char *meta; unsigned char metaType; } Ev;

static int ev_cmp(const void *a, const void *b) {
    const Ev *x = (const Ev *)a, *y = (const Ev *)b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->order - y->order;
}

static void put_vlq(FILE *f, uint32_t v) {
    unsigned char buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    while (v >>= 7) buf[n++] = (unsigned char)(0x80 | (v & 0x7F));
    while (n--) fputc(buf[n], f);
}

static void put32(FILE *f, uint32_t v) { fputc(v >> 24, f); fputc((v >> 16) & 255, f); fputc((v >> 8) & 255, f); fputc(v & 255, f); }
static void put16(FILE *f, unsigned v) { fputc((v >> 8) & 255, f); fputc(v & 255, f); }

static void put_meta(FILE *f, unsigned char type, const char *s) {
    size_t n = strlen(s);
    fputc(0xFF, f); fputc(type, f); put_vlq(f, (uint32_t)n); fwrite(s, 1, n, f);
}

static long begin_track(FILE *f) { fwrite("MTrk", 1, 4, f); long pos = ftell(f); put32(f, 0); return pos; }
static void end_track(FILE *f, long lenpos) {
    fputc(0x00, f); fputc(0xFF, f); fputc(0x2F, f); fputc(0x00, f);
    long end = ftell(f);
    fseek(f, lenpos, SEEK_SET);
    put32(f, (uint32_t)(end - lenpos - 4));
    fseek(f, end, SEEK_SET);
}

int vc_midi_write(const char *path, const char *title, const char *text, const MfChord *ch, int n,
                  double bpm, int tsn, int tsd) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite("MThd", 1, 4, f);
    put32(f, 6); put16(f, 1); put16(f, 2); put16(f, PPQ);

    /* conductor track */
    long t0 = begin_track(f);
    put_vlq(f, 0); put_meta(f, 0x03, title);
    if (text && *text) { put_vlq(f, 0); put_meta(f, 0x01, text); }
    uint32_t uspq = (uint32_t)(60000000.0 / (bpm > 0 ? bpm : 90.0) + 0.5);
    put_vlq(f, 0); fputc(0xFF, f); fputc(0x51, f); fputc(3, f); fputc((uspq >> 16) & 255, f); fputc((uspq >> 8) & 255, f); fputc(uspq & 255, f);
    int dd = 0; for (int d = tsd; d > 1; d >>= 1) dd++;
    put_vlq(f, 0); fputc(0xFF, f); fputc(0x58, f); fputc(4, f); fputc(tsn, f); fputc(dd, f); fputc(24, f); fputc(8, f);
    end_track(f, t0);

    /* chord track */
    int cap = n * (VC_MAX_NOTES * 2 + 1) + 4;
    Ev *ev = (Ev *)calloc((size_t)cap, sizeof(Ev));
    if (!ev) { fclose(f); return 0; }
    int k = 0;
    for (int i = 0; i < n; i++) {
        uint32_t on = (uint32_t)(ch[i].start_beats * PPQ + 0.5f);
        uint32_t dur = (uint32_t)(ch[i].dur_beats * PPQ + 0.5f);
        uint32_t off = on + (dur > 20 ? dur - 10 : dur); /* tiny gap so repeated notes re-trigger */
        ev[k].tick = on; ev[k].order = 1; ev[k].meta = ch[i].name; ev[k].metaType = 0x06; k++; /* marker = chord name */
        for (int j = 0; j < ch[i].n; j++) {
            ev[k].tick = on; ev[k].order = 2; ev[k].len = 3;
            ev[k].b[0] = 0x90; ev[k].b[1] = ch[i].notes[j]; ev[k].b[2] = ch[i].vel; k++;
            ev[k].tick = off; ev[k].order = 0; ev[k].len = 3;
            ev[k].b[0] = 0x80; ev[k].b[1] = ch[i].notes[j]; ev[k].b[2] = 0; k++;
        }
    }
    qsort(ev, (size_t)k, sizeof(Ev), ev_cmp);
    long t1 = begin_track(f);
    put_vlq(f, 0); put_meta(f, 0x03, "Chords");
    uint32_t last = 0;
    for (int i = 0; i < k; i++) {
        put_vlq(f, ev[i].tick - last);
        last = ev[i].tick;
        if (ev[i].meta) put_meta(f, ev[i].metaType, ev[i].meta);
        else fwrite(ev[i].b, 1, (size_t)ev[i].len, f);
    }
    end_track(f, t1);
    free(ev);
    int ok = !ferror(f);
    fclose(f);
    return ok;
}

static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { fputc('\\', f); fputc(*s, f); }
        else if ((unsigned char)*s < 0x20) fputc(' ', f);
        else fputc(*s, f);
    }
    fputc('"', f);
}

int vc_progression_write(const char *path, const char *name, const char *rootName, const MfChord *ch, int n) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "{\n    \"progression\": {\n        \"name\": ");
    json_str(f, name);
    fprintf(f, ",\n        \"rootNote\": ");
    json_str(f, rootName);
    fprintf(f, ",\n        \"scale\": \"Chromatic\",\n        \"recordingOctave\": 2,\n        \"chords\": [\n");
    for (int i = 0; i < n; i++) {
        fprintf(f, "            {\n                \"name\": ");
        json_str(f, ch[i].name);
        fprintf(f, ",\n                \"role\": \"Normal\",\n                \"notes\": [");
        for (int j = 0; j < ch[i].n; j++) fprintf(f, "%s%d", j ? ", " : "", ch[i].notes[j]);
        fprintf(f, "]\n            }%s\n", i + 1 < n ? "," : "");
    }
    fprintf(f, "        ]\n    }\n}\n");
    int ok = !ferror(f);
    fclose(f);
    return ok;
}

static void fill(MfChord *m, const Voicing *v, const char *name, float start, float dur, int vel) {
    memset(m, 0, sizeof(*m));
    m->start_beats = start;
    m->dur_beats = dur;
    m->n = v->n;
    memcpy(m->notes, v->notes, (size_t)v->n);
    m->vel = (unsigned char)vel;
    strncpy(m->name, name, sizeof(m->name) - 1);
}

int vc_section_chords(const Song *s, int sec, const LayoutParams *lp, MfChord *out, int max) {
    if (sec < 0 || sec >= s->nsections) return 0;
    const SongSection *ss = &s->sections[sec];
    int flats = vc_song_flats(s, lp->transpose);
    Voicing prev, cur;
    int have = 0, k = 0;
    float t = 0.f;
    for (int i = 0; i < ss->nev && k < max; i++) {
        ChordSpec c;
        if (!vc_chord_parse(ss->ev[i].chord, &c)) { t += ss->ev[i].beats; continue; }
        ChordSpec tc = vc_chord_transpose(&c, lp->transpose);
        vc_voice(&tc, lp->voicing, lp->inversion, lp->octave, (lp->voice_lead && have) ? &prev : 0, &cur);
        char name[24];
        if (lp->transpose == 0) { strncpy(name, ss->ev[i].chord, 23); name[23] = 0; }
        else vc_chord_name(&tc, flats, name, sizeof(name));
        fill(&out[k++], &cur, name, t, ss->ev[i].beats, 96);
        prev = cur;
        have = 1;
        t += ss->ev[i].beats;
    }
    return k;
}

int vc_bank_chords(const PadChord *pads, const Voicing *v, int bank, MfChord *out, int max) {
    int k = 0;
    for (int i = 0; i < 16 && k < max; i++) {
        int p = bank * 16 + i;
        if (!pads[p].valid || v[p].n == 0) continue;
        fill(&out[k], &v[p], pads[p].name, (float)(4 * k), 4.f, 96);
        k++;
    }
    return k;
}

void vc_safe_filename(const char *in, char *out, int len) {
    int k = 0;
    for (; *in && k < len - 1; in++) {
        char c = *in;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '.' || c == '#';
        out[k++] = ok ? c : '_';
    }
    out[k] = 0;
}

void vc_provenance_text(const Song *s, const LayoutParams *lp, char *buf, int len) {
    if (!strcmp(s->status, "original")) {
        snprintf(buf, (size_t)len, "Velvet Chords original progression: %s (%s), %s, %.0f BPM. Not from any recording. Voicing: %s. Transpose %+d.",
                 s->title, s->genre, s->key, s->bpm, VC_VOICE_NAMES[lp->voicing], lp->transpose);
        return;
    }
    snprintf(buf, (size_t)len,
             "Velvet Chords export. %s - %s (%d). Data status: %s, confidence: %s. %s Voicing: generated performance voicing (%s), "
             "not a transcription of the record. Tempo %s. Time signature %s. Transpose %+d.",
             s->title, s->artist, s->year, s->status, s->confidence, s->notes ? s->notes : "",
             VC_VOICE_NAMES[lp->voicing], s->bpm > 0 ? "from data" : "not verified (placeholder)",
             s->ts_verified ? "from data" : "not verified (4/4 placeholder)", lp->transpose);
}
