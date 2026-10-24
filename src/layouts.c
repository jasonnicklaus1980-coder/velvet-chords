#include "layouts.h"
#include "progression.h"
#include <string.h>

const char *VC_MODE_NAMES[VC_MODE_COUNT] = {"Song", "Smart scale", "R&B kit", "Chord types", "Custom"};

const Scale VC_SCALES[] = {
    {"Major", 7, {0, 2, 4, 5, 7, 9, 11}, "Happy, bright, resolved"},
    {"Natural minor", 7, {0, 2, 3, 5, 7, 8, 10}, "Sad, dark, emotional"},
    {"Harmonic minor", 7, {0, 2, 3, 5, 7, 8, 11}, "Dark, classical, dramatic"},
    {"Melodic minor", 7, {0, 2, 3, 5, 7, 9, 11}, "Jazz minor: bright up, dark down"},
    {"Dorian", 7, {0, 2, 3, 5, 7, 9, 10}, "Minor, soulful, a little light"},
    {"Phrygian", 7, {0, 1, 3, 5, 7, 8, 10}, "Spanish, dark, tense"},
    {"Lydian", 7, {0, 2, 4, 6, 7, 9, 11}, "Bright, dreamy, floating"},
    {"Mixolydian", 7, {0, 2, 4, 5, 7, 9, 10}, "Bluesy major, rock and folk"},
    {"Locrian", 7, {0, 1, 3, 5, 6, 8, 10}, "Unstable, very dark, rare"},
    {"Major pentatonic", 5, {0, 2, 4, 7, 9}, "Open, simple, happy"},
    {"Minor pentatonic", 5, {0, 3, 5, 7, 10}, "Bluesy, rock, classic lead"},
    {"Blues", 6, {0, 3, 5, 6, 7, 10}, "Minor with a flat 5th, bluesy"},
    {"Major blues (gospel)", 6, {0, 2, 3, 4, 7, 9}, "Gospel, bright blues"},
    {"Lydian dominant", 7, {0, 2, 4, 6, 7, 9, 10}, "Bright dominant, bold, cinematic"},
    {"Phrygian dominant", 7, {0, 1, 4, 5, 7, 8, 10}, "Flamenco, Arabic, exotic"},
    {"Whole tone", 6, {0, 2, 4, 6, 8, 10}, "Dreamy, ambiguous, Debussy"},
    {"Diminished (half-whole)", 8, {0, 1, 3, 4, 6, 7, 9, 10}, "Dominant tension, jazz, symmetric"},
    /* scales 17 and up: tools/gen_scales.py */
#include "scales_gen.inc"
};
const int VC_NUM_SCALES = (int)(sizeof(VC_SCALES) / sizeof(VC_SCALES[0]));

static int popcount12(unsigned m) { int n = 0; for (; m; m &= m - 1) n++; return n; }

/* a key and scale that fit the played pitch classes; the pitch-class mask has bit k for pitch class k (C = 0) */
static int hit_better(const DetectHit *a, const DetectHit *b) {
    if (a->matches != b->matches) return a->matches > b->matches;
    if (a->scale != b->scale) return a->scale < b->scale;   /* the table lists the common scales first */
    return a->key < b->key;
}

int vc_detect(unsigned pcmask, int containing, DetectHit *out, int max) {
    pcmask &= 0xFFF;
    int want = popcount12(pcmask), n = 0;
    if (want == 0 || max <= 0) return 0;
    for (int key = 0; key < 12; key++) {
        for (int s = 0; s < VC_NUM_SCALES; s++) {
            const Scale *sc = &VC_SCALES[s];
            unsigned m = 0;
            for (int i = 0; i < sc->n; i++) m |= 1u << (((key + sc->iv[i]) % 12 + 12) % 12);
            DetectHit h = {key, s, popcount12(pcmask & m), sc->n};
            if (h.matches == 0 || (containing && h.matches != want)) continue;
            /* insertion into the best-first list, kept at most `max` long */
            int at = n < max ? n : max;
            while (at > 0 && hit_better(&h, &out[at - 1])) { if (at < max) out[at] = out[at - 1]; at--; }
            if (at < max) { out[at] = h; if (n < max) n++; }
        }
    }
    return n;
}

static int pcmod(int x) { x %= 12; return x < 0 ? x + 12 : x; }

int vc_prefer_flats(int key, int minor) {
    key = pcmod(key);
    if ((!minor && key == 0) || (minor && key == 9)) return 2; /* no sharps or flats in the key: mixed spelling */
    if (minor) return key == 2 || key == 7 || key == 0 || key == 5 || key == 10 || key == 3 || key == 8;
    return key == 5 || key == 10 || key == 3 || key == 8 || key == 1;
}

int vc_scale_is_minor(int s) {
    const Scale *sc = &VC_SCALES[s];
    int has3 = 0, has4 = 0;
    for (int i = 0; i < sc->n; i++) { if (sc->iv[i] == 3) has3 = 1; if (sc->iv[i] == 4) has4 = 1; }
    return has3 && !has4;
}

static int is_minorish(const ChordSpec *c) {
    int iv[VC_MAX_IV], n = vc_chord_intervals(c, iv), m3 = 0, M3 = 0;
    for (int i = 0; i < n; i++) { int p = pcmod(iv[i]); if (p == 3) m3 = 1; if (p == 4) M3 = 1; }
    return m3 && !M3;
}

void vc_roman(int key, const ChordSpec *c, char *buf, int len) {
    static const char *UP[12] = {"I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII"};
    static const char *LO[12] = {"i", "bii", "ii", "biii", "iii", "iv", "#iv", "v", "bvi", "vi", "bvii", "vii"};
    int d = pcmod(c->root - key);
    int minor = is_minorish(c);
    const char *suf = c->quality >= 0 ? VC_QUALITIES[c->quality].suffix : "*";
    char s[16];
    if (!strcmp(suf, "dim")) strcpy(s, "\xc2\xb0");
    else if (!strcmp(suf, "dim7")) strcpy(s, "\xc2\xb0" "7");
    else if (!strcmp(suf, "m7b5")) strcpy(s, "\xc3\xb8" "7");
    else if (!strcmp(suf, "aug")) strcpy(s, "+");
    else if (minor && suf[0] == 'm' && strncmp(suf, "maj", 3)) { strncpy(s, suf + 1, 15); s[15] = 0; }
    else { strncpy(s, suf, 15); s[15] = 0; }
    int k = 0;
    const char *r = minor ? LO[d] : UP[d];
    while (*r && k < len - 1) buf[k++] = *r++;
    for (const char *p = s; *p && k < len - 1; p++) buf[k++] = *p;
    if (c->bass >= 0) {
        const char *b = UP[pcmod(c->bass - key)];
        if (k < len - 1) buf[k++] = '/';
        while (*b && k < len - 1) buf[k++] = *b++;
    }
    buf[k] = 0;
}

int vc_song_flats(const Song *s, int transpose) {
    return vc_prefer_flats(s->key_root + transpose, s->minor);
}

int vc_song_section_index(const LayoutParams *p, int bank) {
    if (p->song < 0 || p->song >= VC_NUM_SONGS) return -1;
    const Song *s = &VC_SONGS[p->song];
    int sec = p->section;
    if (sec >= s->nsections) sec = s->nsections - 1;
    if (sec < 0) sec = 0;
    sec += bank;
    return sec < s->nsections ? sec : -1;
}

static void set_pad(PadChord *pc, const ChordSpec *c, int key, int flats, int oct, const char *nameOverride) {
    memset(pc, 0, sizeof(*pc));
    pc->valid = 1;
    pc->spec = *c;
    pc->oct = oct;
    if (nameOverride) { strncpy(pc->name, nameOverride, sizeof(pc->name) - 1); }
    else vc_chord_name(c, flats, pc->name, sizeof(pc->name));
    if (key >= 0) vc_roman(key, c, pc->roman, sizeof(pc->roman));
}

static void pad_from(PadChord *pc, int key, int interval, const char *suffix, int bassIv, int flats, int oct) {
    ChordSpec c;
    memset(&c, 0, sizeof(c));
    c.root = pcmod(key + interval);
    c.quality = vc_quality_find(suffix);
    if (c.quality < 0) c.quality = 0;
    c.bass = bassIv < 0 ? -1 : pcmod(key + bassIv);
    if (c.bass == c.root) c.bass = -1;
    set_pad(pc, &c, key, flats, oct, 0);
}

/* stack every other scale note: count = 3 (triad), 4 (7th), 5 (9th) */
static ChordSpec diatonic(int key, int scale, int degree, int count) {
    const Scale *sc = &VC_SCALES[scale];
    ChordSpec c;
    memset(&c, 0, sizeof(c));
    c.bass = -1;
    int base = sc->iv[degree % sc->n];
    c.root = pcmod(key + base);
    int iv[VC_MAX_IV], mask = 0;
    for (int i = 0; i < count; i++) {
        int idx = degree + 2 * i;
        int v = sc->iv[idx % sc->n] + 12 * (idx / sc->n) - base;
        iv[i] = v;
        mask |= 1 << pcmod(v);
    }
    int q = vc_quality_match(mask, count);
    if (q >= 0) c.quality = q;
    else {
        c.quality = -1;
        c.ncustom = count;
        for (int i = 0; i < count; i++) c.custom[i] = (signed char)iv[i];
    }
    return c;
}

typedef struct { signed char iv; const char *suf; signed char bass; } KitEntry;

static const KitEntry RNB_MAJ_A[16] = {
    {0, "maj9", -1}, {2, "m9", -1}, {4, "m7", -1}, {5, "maj9", -1}, {7, "9sus4", -1}, {9, "m9", -1}, {11, "m7b5", -1}, {7, "13", -1},
    {4, "7b9", -1}, {9, "9", -1}, {1, "9", -1}, {5, "m9", -1}, {10, "13", -1}, {6, "m7b5", -1}, {5, "maj7", 7}, {1, "dim7", -1}};
static const KitEntry RNB_MAJ_B[16] = {
    {0, "maj7", 4}, {5, "", 0}, {0, "9", -1}, {5, "maj7", -1}, {6, "dim7", -1}, {0, "add9", 7}, {9, "7b9", -1}, {2, "m11", -1},
    {7, "7#9", -1}, {8, "maj7", -1}, {10, "9", -1}, {3, "maj7", -1}, {4, "m11", -1}, {9, "m11", -1}, {5, "m6", -1}, {0, "6/9", -1}};
static const KitEntry RNB_MIN_A[16] = {
    {0, "m9", -1}, {2, "m7b5", -1}, {3, "maj7", -1}, {5, "m9", -1}, {7, "m9", -1}, {8, "maj7", -1}, {10, "9", -1}, {7, "7#9", -1},
    {0, "m11", -1}, {5, "9", -1}, {1, "maj7", -1}, {8, "maj7#11", -1}, {7, "7b9", -1}, {11, "dim7", -1}, {10, "9sus4", -1}, {0, "mmaj9", -1}};
static const KitEntry RNB_MIN_B[16] = {
    {0, "m6/9", -1}, {3, "maj9", -1}, {5, "m11", -1}, {7, "m11", -1}, {8, "9", -1}, {10, "7sus4", -1}, {1, "9", -1}, {7, "7alt", -1},
    {0, "m9", 7}, {8, "maj7", 10}, {2, "m11", -1}, {9, "m7b5", -1}, {6, "dim7", -1}, {0, "7#9", -1}, {5, "7", -1}, {3, "7", -1}};

static const char *TYPES_A[16] = {"", "m", "7", "maj7", "m7", "m7b5", "dim7", "aug", "sus2", "sus4", "6", "m6", "maj9", "m9", "9", "13"};
static const char *TYPES_B[16] = {"add9", "madd9", "11", "m11", "maj7#11", "7#9", "7b9", "7#5", "13b9", "mmaj7", "6/9", "7sus4", "maj13", "m13", "dim", "7alt"};

static void build_layout(const LayoutParams *p, const CustomPad *custom, PadChord out[VC_PADS]) {
    memset(out, 0, sizeof(PadChord) * VC_PADS);
    int key = pcmod(p->key);
    int scale = (p->scale >= 0 && p->scale < VC_NUM_SCALES) ? p->scale : 0;
    int minor = vc_scale_is_minor(scale);
    int flats = vc_prefer_flats(key, minor);

    switch (p->mode) {
    case VC_MODE_SONG: {
        if (p->song < 0 || p->song >= VC_NUM_SONGS) return;
        const Song *s = &VC_SONGS[p->song];
        int skey = pcmod(s->key_root + p->transpose);
        int sflats = vc_song_flats(s, p->transpose);
        for (int b = 0; b < 4; b++) {
            int si = vc_song_section_index(p, b);
            if (si < 0) continue;
            const SongSection *sec = &s->sections[si];
            int k = 0;
            const char *last = 0;
            for (int e = 0; e < sec->nev && k < 16; e++) {
                if (last && !strcmp(last, sec->ev[e].chord)) continue; /* collapse repeats */
                last = sec->ev[e].chord;
                ChordSpec c;
                if (!vc_chord_parse(sec->ev[e].chord, &c)) continue;
                ChordSpec t = vc_chord_transpose(&c, p->transpose);
                set_pad(&out[b * 16 + k], &t, skey, sflats, 0, p->transpose == 0 ? sec->ev[e].chord : 0);
                k++;
            }
        }
        break;
    }
    case VC_MODE_KEY: {
        int n = VC_SCALES[scale].n;
        int lim = n > 8 ? 8 : n;
        for (int d = 0; d < lim; d++) {
            ChordSpec c7 = diatonic(key, scale, d, 4), c9 = diatonic(key, scale, d, 5), c3 = diatonic(key, scale, d, 3);
            set_pad(&out[d], &c7, key, flats, 0, 0);
            set_pad(&out[8 + d], &c9, key, flats, 0, 0);
            set_pad(&out[16 + d], &c3, key, flats, 0, 0);
            set_pad(&out[48 + d], &c7, key, flats, 1, 0);
            set_pad(&out[56 + d], &c9, key, flats, 1, 0);
        }
        if (lim < 8) {
            ChordSpec c7 = diatonic(key, scale, 0, 4), c9 = diatonic(key, scale, 0, 5), c3 = diatonic(key, scale, 0, 3);
            set_pad(&out[lim], &c7, key, flats, 1, 0);
            set_pad(&out[8 + lim], &c9, key, flats, 1, 0);
            set_pad(&out[16 + lim], &c3, key, flats, 1, 0);
        }
        /* bank B pads 9-16: secondary dominants V7 of each non-tonic degree */
        for (int d = 1; d < lim && d < 8; d++) {
            ChordSpec c;
            memset(&c, 0, sizeof(c));
            c.bass = -1;
            int target = pcmod(key + VC_SCALES[scale].iv[d]);
            c.root = pcmod(target + 7);
            c.quality = vc_quality_find("7");
            PadChord *pc = &out[24 + d - 1];
            set_pad(pc, &c, key, flats, 0, 0);
            ChordSpec tc = diatonic(key, scale, d, 3);
            char tr[16];
            vc_roman(key, &tc, tr, sizeof(tr));
            strcpy(pc->roman, "V7/");
            strncat(pc->roman, tr, sizeof(pc->roman) - strlen(pc->roman) - 1);
        }
        /* bank C: borrowed chords and common substitutions */
        static const KitEntry BORROW_FROM_MINOR[8] = {{0, "m7", -1}, {2, "m7b5", -1}, {3, "maj7", -1}, {5, "m7", -1}, {7, "m7", -1}, {8, "maj7", -1}, {10, "7", -1}, {1, "maj7", -1}};
        static const KitEntry BORROW_FROM_MAJOR[8] = {{0, "maj7", -1}, {2, "m7", -1}, {4, "m7", -1}, {5, "maj7", -1}, {7, "7", -1}, {9, "m7", -1}, {5, "7", -1}, {11, "m7b5", -1}};
        static const KitEntry SUBS[8] = {{1, "7", -1}, {6, "m7b5", -1}, {1, "dim7", -1}, {7, "7sus4", -1}, {5, "m6", -1}, {10, "9", -1}, {7, "13", -1}, {7, "7alt", -1}};
        const KitEntry *bor = minor ? BORROW_FROM_MAJOR : BORROW_FROM_MINOR;
        for (int i = 0; i < 8; i++) {
            pad_from(&out[32 + i], key, bor[i].iv, bor[i].suf, bor[i].bass, flats, 0);
            pad_from(&out[40 + i], key, SUBS[i].iv, SUBS[i].suf, SUBS[i].bass, flats, 0);
        }
        break;
    }
    case VC_MODE_RNB: {
        const KitEntry *A = minor ? RNB_MIN_A : RNB_MAJ_A, *B = minor ? RNB_MIN_B : RNB_MAJ_B;
        for (int i = 0; i < 16; i++) {
            pad_from(&out[i], key, A[i].iv, A[i].suf, A[i].bass, flats, 0);
            pad_from(&out[16 + i], key, B[i].iv, B[i].suf, B[i].bass, flats, 0);
            pad_from(&out[32 + i], key, A[i].iv, A[i].suf, A[i].bass, flats, 1);
            pad_from(&out[48 + i], key, B[i].iv, B[i].suf, B[i].bass, flats, 1);
        }
        break;
    }
    case VC_MODE_TYPES: {
        for (int i = 0; i < 16; i++) {
            pad_from(&out[i], key, 0, TYPES_A[i], -1, flats, 0);
            pad_from(&out[16 + i], key, 0, TYPES_B[i], -1, flats, 0);
            pad_from(&out[32 + i], key, 0, TYPES_A[i], -1, flats, 1);
            pad_from(&out[48 + i], key, 0, TYPES_B[i], -1, flats, 1);
            out[i].roman[0] = out[16 + i].roman[0] = out[32 + i].roman[0] = out[48 + i].roman[0] = 0;
        }
        break;
    }
    case VC_MODE_IMPORT: {
        for (int i = 0; i < p->nimp && i < VC_PADS && p->imp; i++) {
            set_pad(&out[i], &p->imp[i].spec, -1, flats, 0, 0);
            out[i].nexact = p->imp[i].n;
            memcpy(out[i].exact, p->imp[i].notes, (size_t)p->imp[i].n);
        }
        break;
    }
    default: { /* custom */
        for (int i = 0; i < VC_PADS; i++) {
            const CustomPad *cp = &custom[i];
            if (!cp->valid || cp->quality >= VC_NUM_QUALITIES) continue;
            ChordSpec c;
            memset(&c, 0, sizeof(c));
            c.root = cp->root % 12;
            c.quality = cp->quality;
            c.bass = cp->bass == 255 ? -1 : cp->bass % 12;
            if (c.bass == c.root) c.bass = -1;
            if (cp->nnotes >= 2) { /* note-by-note chord: a named quality when the pitch classes match one, else custom intervals */
                int mask = 0;
                for (int j = 0; j < cp->nnotes; j++) mask |= 1 << (((cp->notes[j] % 12) + 12) % 12);
                int q = vc_quality_match(mask, cp->nnotes);
                if (q >= 0) c.quality = q;
                else {
                    c.quality = -1;
                    c.ncustom = cp->nnotes;
                    for (int j = 0; j < cp->nnotes; j++) c.custom[j] = cp->notes[j];
                }
            }
            set_pad(&out[i], &c, -1, flats, cp->oct, 0);
            out[i].inv = cp->inv;
        }
        break;
    }
    }
}

void vc_voice_pads(const LayoutParams *p, const PadChord pads[VC_PADS], Voicing out[VC_PADS]) {
    memset(out, 0, sizeof(Voicing) * VC_PADS);
    for (int b = 0; b < 4; b++) {
        const Voicing *prev = 0;
        for (int i = 0; i < 16; i++) {
            int k = b * 16 + i;
            if (!pads[k].valid) { prev = 0; continue; }
            int oct = p->octave + pads[k].oct;
            if (oct < -3) oct = -3;
            if (oct > 3) oct = 3;
            int inv = p->inversion + pads[k].inv;
            /* only voice-lead between neighbours at the same octave setting */
            const Voicing *vl = (p->voice_lead && prev && pads[k].oct == pads[k - 1].oct) ? prev : 0;
            if (pads[k].nexact > 0) { /* imported: play exactly what the file played, moved by the Octave setting */
                out[k].n = pads[k].nexact; out[k].has_bass = 0;
                for (int j = 0; j < pads[k].nexact; j++) {
                    int v = pads[k].exact[j] + 12 * p->octave;
                    while (v > 127) v -= 12;
                    while (v < 0) v += 12;
                    out[k].notes[j] = (unsigned char)v;
                }
            } else vc_voice(&pads[k].spec, p->voicing, inv, oct, vl, &out[k]);
            prev = &out[k];
        }
    }
}

int vc_prog_length(const LayoutParams *p) {
    if (p->prog < 1) return 0;
    if (p->prog <= VC_NUM_PROGRESSIONS) return VC_PROGRESSIONS[p->prog - 1].n;
    int slot = p->prog - VC_NUM_PROGRESSIONS - 1;
    return slot < VC_USER_PROGS ? p->un[slot] : 0;
}

int vc_prog_step(const LayoutParams *p, int k, ChordSpec *out) {
    int n = vc_prog_length(p);
    if (k < 0 || k >= n) return 0;
    int scale = (p->scale >= 0 && p->scale < VC_NUM_SCALES) ? p->scale : 0;
    if (p->prog <= VC_NUM_PROGRESSIONS) return vc_prog_chord(&VC_PROGRESSIONS[p->prog - 1], pcmod(p->key), scale, k, out);
    int slot = p->prog - VC_NUM_PROGRESSIONS - 1;
    if (p->uq[slot][k] < 0 || p->uq[slot][k] >= VC_NUM_QUALITIES) return 0;
    memset(out, 0, sizeof(*out));
    out->root = p->ur[slot][k] % 12;
    out->quality = p->uq[slot][k];
    out->bass = -1;
    return 1;
}

/* the layout for the mode, then the progression (if one is chosen) on pads 1.. */
void vc_build_pads(const LayoutParams *p, const CustomPad *custom, PadChord out[VC_PADS]) {
    build_layout(p, custom, out);
    int key = pcmod(p->key);
    int flats = vc_prefer_flats(key, vc_scale_is_minor((p->scale >= 0 && p->scale < VC_NUM_SCALES) ? p->scale : 0));
    int n = vc_prog_length(p);
    for (int k = 0; k < n && k < VC_PADS; k++) {
        ChordSpec c;
        if (vc_prog_step(p, k, &c)) set_pad(&out[k], &c, key, flats, 0, 0);
    }
}
