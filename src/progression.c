#include "progression.h"
#include "layouts.h"
#include <string.h>
#include <ctype.h>

/* The templates. Each is a starting point, written in Roman numerals so it works in any key. */
const Progression VC_PROGRESSIONS[] = {
    {"I-V-vi-IV", 4, {"I", "V", "vi", "IV"}},
    {"ii-V-I", 3, {"ii", "V", "I"}},
    {"i-VI-III-VII", 4, {"i", "VI", "III", "VII"}},
    {"Andalusian", 4, {"i", "VII", "VI", "V"}},
    {"Axis", 8, {"I", "V", "vi", "iii", "IV", "I", "IV", "V"}},
    {"Pop I-IV", 2, {"I", "IV"}},
    {"Neo-soul ii-V-I", 3, {"ii7", "V7", "Imaj7"}},
    {"12-bar blues", 12, {"I7", "I7", "I7", "I7", "IV7", "IV7", "I7", "I7", "V7", "IV7", "I7", "V7"}},
};
const int VC_NUM_PROGRESSIONS = (int)(sizeof(VC_PROGRESSIONS) / sizeof(VC_PROGRESSIONS[0]));

static const char *NUMERALS[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};

static int pcmod12(int x) { x %= 12; return x < 0 ? x + 12 : x; }

int vc_roman_chord(int key, int scale, const char *step, ChordSpec *out) {
    if (!step || !out || scale < 0 || scale >= VC_NUM_SCALES) return 0;
    const Scale *sc = &VC_SCALES[scale];
    const char *s = step;
    int acc = 0;
    if (*s == 'b') { acc = -1; s++; }
    else if (*s == '#') { acc = 1; s++; }
    /* longest numeral first, so VII is not read as V */
    int deg = -1, len = 0;
    for (int d = 6; d >= 0 && deg < 0; d--) {
        int l = (int)strlen(NUMERALS[d]);
        int match = 1;
        for (int k = 0; k < l && match; k++) if (toupper((unsigned char)s[k]) != NUMERALS[d][k]) match = 0;
        if (match) { deg = d; len = l; }
    }
    if (deg < 0 || deg >= sc->n) return 0;
    int upper = isupper((unsigned char)s[0]);
    const char *suf = s + len;
    const char *quality = 0;
    if (!*suf) quality = upper ? "" : "m";
    else if (!strcmp(suf, "\xc2\xb0") || !strcmp(suf, "dim")) quality = "dim"; /* ° */
    else if (!strcmp(suf, "\xc3\xb8")) quality = "m7b5";                        /* ø */
    else if (!strcmp(suf, "7")) quality = upper ? "7" : "m7";
    else quality = suf;
    int q = vc_quality_find(quality);
    if (q < 0) return 0;
    memset(out, 0, sizeof(*out));
    out->root = pcmod12(key + sc->iv[deg] + acc);
    out->quality = q;
    out->bass = -1;
    return 1;
}

int vc_prog_chord(const Progression *p, int key, int scale, int i, ChordSpec *out) {
    if (!p || p->n < 1) return 0;
    i %= p->n;
    if (i < 0) i += p->n;
    return vc_roman_chord(key, scale, p->steps[i], out);
}

void vc_ctx_set(HarmonicContext *h, int key, int scale, const Progression *p) {
    h->key = key;
    h->scale = scale;
    h->prog = p;
    h->pos = 0;
}

void vc_ctx_advance(HarmonicContext *h) {
    if (!h->prog || h->prog->n < 1) return;
    h->pos = (h->pos + 1) % h->prog->n;
}

int vc_ctx_chord(const HarmonicContext *h, ChordSpec *out) {
    return vc_prog_chord(h->prog, h->key, h->scale, h->pos, out);
}
