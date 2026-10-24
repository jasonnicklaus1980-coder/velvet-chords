#include "chords.h"
#include <string.h>

/* ORDER IS PART OF THE PRESET FORMAT: only append new qualities at the end. */
const ChordQuality VC_QUALITIES[] = {
    {"", "Major", 3, {0, 4, 7}},
    {"m", "Minor", 3, {0, 3, 7}},
    {"dim", "Diminished", 3, {0, 3, 6}},
    {"aug", "Augmented", 3, {0, 4, 8}},
    {"sus2", "Sus2", 3, {0, 2, 7}},
    {"sus4", "Sus4", 3, {0, 5, 7}},
    {"5", "Power (5)", 2, {0, 7}},
    {"6", "Major 6", 4, {0, 4, 7, 9}},
    {"m6", "Minor 6", 4, {0, 3, 7, 9}},
    {"add9", "Add9", 4, {0, 4, 7, 14}},
    {"madd9", "Minor add9", 4, {0, 3, 7, 14}},
    {"6/9", "6/9", 5, {0, 4, 7, 9, 14}},
    {"maj7", "Major 7", 4, {0, 4, 7, 11}},
    {"m7", "Minor 7", 4, {0, 3, 7, 10}},
    {"7", "Dominant 7", 4, {0, 4, 7, 10}},
    {"dim7", "Diminished 7", 4, {0, 3, 6, 9}},
    {"m7b5", "Half-diminished", 4, {0, 3, 6, 10}},
    {"mmaj7", "Minor-major 7", 4, {0, 3, 7, 11}},
    {"7sus4", "7sus4", 4, {0, 5, 7, 10}},
    {"maj9", "Major 9", 5, {0, 4, 7, 11, 14}},
    {"m9", "Minor 9", 5, {0, 3, 7, 10, 14}},
    {"9", "Dominant 9", 5, {0, 4, 7, 10, 14}},
    {"9sus4", "9sus4", 5, {0, 5, 7, 10, 14}},
    {"11", "11 (no 3rd)", 5, {0, 7, 10, 14, 17}},
    {"m11", "Minor 11", 6, {0, 3, 7, 10, 14, 17}},
    {"maj7#11", "Major 7#11", 5, {0, 4, 7, 11, 18}},
    {"13", "Dominant 13", 5, {0, 4, 10, 14, 21}},
    {"m13", "Minor 13", 5, {0, 3, 10, 14, 21}},
    {"maj13", "Major 13", 5, {0, 4, 11, 14, 21}},
    {"7b9", "7b9", 5, {0, 4, 7, 10, 13}},
    {"7#9", "7#9", 5, {0, 4, 7, 10, 15}},
    {"7#5", "7#5", 4, {0, 4, 8, 10}},
    {"7b5", "7b5", 4, {0, 4, 6, 10}},
    {"9#11", "9#11", 5, {0, 4, 10, 14, 18}},
    {"13b9", "13b9", 5, {0, 4, 10, 13, 21}},
    {"7alt", "Altered (#9 b13)", 5, {0, 4, 10, 15, 20}},
    {"mmaj9", "Minor-major 9", 5, {0, 3, 7, 11, 14}},
    {"m6/9", "Minor 6/9", 5, {0, 3, 7, 9, 14}},
    {"maj7#5", "Major 7#5", 4, {0, 4, 8, 11}},
    {"7sus2", "7sus2", 4, {0, 2, 7, 10}},
    {"qrt", "Quartal (4ths)", 4, {0, 5, 10, 15}},
};
const int VC_NUM_QUALITIES = (int)(sizeof(VC_QUALITIES) / sizeof(VC_QUALITIES[0]));

const char *VC_VOICE_NAMES[VC_VOICE_COUNT] = {"Bass + upper", "Close", "Drop 2", "Spread", "Upper only"};

static const char *SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
/* neutral keys (C major / A minor): the usual names for borrowed and chromatic chords */
static const char *MIXED[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static int pcmod(int x) { x %= 12; return x < 0 ? x + 12 : x; }

const char *vc_pc_name(int pc, int flats) { return flats == 2 ? MIXED[pcmod(pc)] : flats ? FLAT[pcmod(pc)] : SHARP[pcmod(pc)]; }

void vc_note_name(int midi, int flats, char *buf, int len) {
    int oct = midi / 12 - 2; /* MPC convention: 60 = C3, 36 = C1 */
    const char *n = vc_pc_name(midi, flats);
    int k = 0;
    while (*n && k < len - 4) buf[k++] = *n++;
    if (oct < 0) { buf[k++] = '-'; oct = -oct; }
    if (k < len - 2) buf[k++] = (char)('0' + (oct % 10));
    buf[k] = 0;
}

int vc_pc_parse(const char *s, int *consumed) {
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7}; /* A..G */
    if (!s || s[0] < 'A' || s[0] > 'G') return -1;
    int pc = base[s[0] - 'A'], k = 1;
    if (s[1] == '#') { pc++; k++; }
    else if (s[1] == 'b') { pc--; k++; }
    if (consumed) *consumed = k;
    return pcmod(pc);
}

static const char *ALIASES[][2] = {
    {"M", ""}, {"maj", ""}, {"min", "m"}, {"mi", "m"}, {"-", "m"},
    {"M7", "maj7"}, {"Maj7", "maj7"}, {"ma7", "maj7"}, {"^7", "maj7"}, {"^", "maj7"},
    {"min7", "m7"}, {"mi7", "m7"}, {"-7", "m7"},
    {"\xc3\xb8", "m7b5"}, {"\xc3\xb8" "7", "m7b5"}, {"m7-5", "m7b5"}, {"-7b5", "m7b5"}, {"min7b5", "m7b5"},
    {"o", "dim"}, {"\xc2\xb0", "dim"}, {"o7", "dim7"}, {"\xc2\xb0" "7", "dim7"},
    {"+", "aug"}, {"+5", "aug"}, {"#5", "aug"},
    {"sus", "sus4"}, {"7sus", "7sus4"}, {"9sus", "9sus4"}, {"add2", "add9"}, {"madd2", "madd9"},
    {"M9", "maj9"}, {"Maj9", "maj9"}, {"min9", "m9"}, {"-9", "m9"},
    {"69", "6/9"}, {"6add9", "6/9"}, {"m69", "m6/9"}, {"min6", "m6"}, {"-6", "m6"},
    {"+7", "7#5"}, {"aug7", "7#5"}, {"7+5", "7#5"}, {"7+", "7#5"}, {"7-5", "7b5"},
    {"7-9", "7b9"}, {"7+9", "7#9"},
    {"mM7", "mmaj7"}, {"minmaj7", "mmaj7"}, {"-maj7", "mmaj7"}, {"mM9", "mmaj9"},
    {"M13", "maj13"}, {"Maj13", "maj13"}, {"min11", "m11"}, {"-11", "m11"}, {"min13", "m13"}, {"-13", "m13"},
    {"M7#11", "maj7#11"}, {"maj7+11", "maj7#11"}, {"alt", "7alt"}, {"7#9b13", "7alt"},
    {"maj7+5", "maj7#5"}, {"M7#5", "maj7#5"},
};

/* Relative chords: the minor chord that shares the notes of a major one, and the reverse. Only the major and minor
 * families that have a clear partner are listed; everything else (dim, aug, sus, dominant, and so on) has none. */
static const char *REL_PAIRS[][2] = {
    {"", "m"}, {"6", "m6"}, {"add9", "madd9"}, {"maj7", "m7"}, {"maj9", "m9"}, {"maj13", "m13"},
};

int vc_relative(const ChordSpec *in, ChordSpec *out) {
    if (in->quality < 0 || in->quality >= VC_NUM_QUALITIES) return 0;
    const char *from = VC_QUALITIES[in->quality].suffix;
    for (int i = 0; i < (int)(sizeof(REL_PAIRS) / sizeof(REL_PAIRS[0])); i++) {
        int q_major = vc_quality_find(REL_PAIRS[i][0]), q_minor = vc_quality_find(REL_PAIRS[i][1]);
        if (q_major < 0 || q_minor < 0) continue;
        if (!strcmp(from, REL_PAIRS[i][0])) {       /* major -> relative minor: the root moves up 9 semitones (down a minor third) */
            *out = *in; out->quality = q_minor; out->root = (in->root + 9) % 12; out->bass = -1; out->ncustom = 0;
            return 1;
        }
        if (!strcmp(from, REL_PAIRS[i][1])) {       /* minor -> relative major: the root moves up 3 semitones */
            *out = *in; out->quality = q_major; out->root = (in->root + 3) % 12; out->bass = -1; out->ncustom = 0;
            return 1;
        }
    }
    return 0;
}

int vc_quality_find(const char *suffix) {
    char norm[24];
    int k = 0;
    for (const char *p = suffix; *p && k < 23; p++)
        if (*p != '(' && *p != ')' && *p != ' ') norm[k++] = *p;
    norm[k] = 0;
    for (int i = 0; i < VC_NUM_QUALITIES; i++)
        if (strcmp(norm, VC_QUALITIES[i].suffix) == 0) return i;
    for (unsigned i = 0; i < sizeof(ALIASES) / sizeof(ALIASES[0]); i++)
        if (strcmp(norm, ALIASES[i][0]) == 0) return vc_quality_find(ALIASES[i][1]);
    return -1;
}

int vc_chord_parse(const char *sym, ChordSpec *out) {
    int used = 0;
    memset(out, 0, sizeof(*out));
    out->bass = -1;
    int root = vc_pc_parse(sym, &used);
    if (root < 0) return 0;
    out->root = root;
    const char *suf = sym + used;
    /* slash bass = last '/' followed by a note letter ("6/9" is not a slash chord) */
    const char *slash = 0;
    for (const char *p = suf; *p; p++)
        if (*p == '/' && p[1] >= 'A' && p[1] <= 'G') slash = p;
    char q[24];
    int n = slash ? (int)(slash - suf) : (int)strlen(suf);
    if (n > 23) return 0;
    memcpy(q, suf, (size_t)n);
    q[n] = 0;
    out->quality = vc_quality_find(q);
    if (out->quality < 0) return 0;
    if (slash) {
        int bu = 0;
        int b = vc_pc_parse(slash + 1, &bu);
        if (b < 0 || slash[1 + bu] != 0) return 0;
        out->bass = (b == root) ? -1 : b;
    }
    return 1;
}

int vc_chord_intervals(const ChordSpec *c, int *iv) {
    if (c->quality < 0) {
        for (int i = 0; i < c->ncustom; i++) iv[i] = c->custom[i];
        return c->ncustom;
    }
    const ChordQuality *q = &VC_QUALITIES[c->quality];
    for (int i = 0; i < q->n; i++) iv[i] = q->iv[i];
    return q->n;
}

int vc_chord_pcmask(const ChordSpec *c) {
    int iv[VC_MAX_IV], n = vc_chord_intervals(c, iv), m = 0;
    for (int i = 0; i < n; i++) m |= 1 << pcmod(c->root + iv[i]);
    if (c->bass >= 0) m |= 1 << c->bass;
    return m;
}

ChordSpec vc_chord_transpose(const ChordSpec *c, int semis) {
    ChordSpec t = *c;
    t.root = pcmod(c->root + semis);
    if (c->bass >= 0) t.bass = pcmod(c->bass + semis);
    return t;
}

int vc_quality_match(int mask, int ncount) {
    for (int i = 0; i < VC_NUM_QUALITIES; i++) {
        const ChordQuality *q = &VC_QUALITIES[i];
        if (q->n != ncount) continue;
        int m = 0;
        for (int j = 0; j < q->n; j++) m |= 1 << pcmod(q->iv[j]);
        if (m == mask) return i;
    }
    return -1;
}

static void sappend(char *buf, int len, int *k, const char *s) {
    while (*s && *k < len - 1) buf[(*k)++] = *s++;
    buf[*k] = 0;
}

void vc_chord_name(const ChordSpec *c, int flats, char *buf, int len) {
    int k = 0;
    buf[0] = 0;
    sappend(buf, len, &k, vc_pc_name(c->root, flats));
    if (c->quality >= 0) sappend(buf, len, &k, VC_QUALITIES[c->quality].suffix);
    else {
        sappend(buf, len, &k, "(");
        int iv[VC_MAX_IV], n = vc_chord_intervals(c, iv);
        for (int i = 1; i < n; i++) {
            if (i > 1) sappend(buf, len, &k, " ");
            sappend(buf, len, &k, vc_pc_name(c->root + iv[i], flats));
        }
        sappend(buf, len, &k, ")");
    }
    if (c->bass >= 0) { sappend(buf, len, &k, "/"); sappend(buf, len, &k, vc_pc_name(c->bass, flats)); }
}

/* ---------- voicing ---------- */

static void sort_int(int *a, int n) {
    for (int i = 1; i < n; i++) { int v = a[i], j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; }
}

/* rotate tones, make strictly ascending */
static void stack(const int *tones, int m, int rot, int *out) {
    int k = 0;
    rot = m ? rot % m : 0;
    for (int i = rot; i < m; i++) out[k++] = tones[i];
    for (int i = 0; i < rot; i++) out[k++] = tones[i] + 12;
    for (int j = 1; j < m; j++) while (out[j] <= out[j - 1]) out[j] += 12;
}

static int mean_x4(const int *a, int n) { int s = 0; for (int i = 0; i < n; i++) s += a[i]; return n ? (4 * s) / n : 0; }

static void shift_to_center(int *a, int n, int center) {
    if (!n) return;
    while (mean_x4(a, n) > 4 * center + 24) for (int i = 0; i < n; i++) a[i] -= 12;
    while (mean_x4(a, n) < 4 * center - 24) for (int i = 0; i < n; i++) a[i] += 12;
}

static int vl_cost(const int *a, int n, const unsigned char *b, int m) {
    int cost = 0;
    for (int i = 0; i < n; i++) { int best = 999; for (int j = 0; j < m; j++) { int d = a[i] - b[j]; if (d < 0) d = -d; if (d < best) best = d; } cost += best; }
    for (int j = 0; j < m; j++) { int best = 999; for (int i = 0; i < n; i++) { int d = a[i] - b[j]; if (d < 0) d = -d; if (d < best) best = d; } cost += best; }
    return cost;
}

static void apply_drop2(int *a, int n) {
    if (n < 4) return;
    a[n - 2] -= 12;
    sort_int(a, n);
}

void vc_voice(const ChordSpec *c, int style, int inversion, int octave, const Voicing *prev, Voicing *out) {
    int iv[VC_MAX_IV], n = vc_chord_intervals(c, iv);
    int tones[VC_MAX_IV] = {0}, m = 0, up[VC_MAX_IV] = {0};
    int bass = -1;
    int root3 = 48 + c->root + 12 * octave; /* root around C3..B3 in MIDI numbers */
    int center = 62 + 12 * octave;
    int has_slash = c->bass >= 0;
    if (inversion < 0) inversion = 0;

    memset(out, 0, sizeof(*out));

    if (style == VC_VOICE_BASS_UPPER || style == VC_VOICE_SPREAD) bass = 36 + (has_slash ? c->bass : c->root) + 12 * octave;

    if (style == VC_VOICE_BASS_UPPER || style == VC_VOICE_UPPER_ONLY) {
        /* upper structure in chord-table order (3-5-7-9...), rootless when the bass plays the root */
        int rootless = (n >= 4 && (style == VC_VOICE_BASS_UPPER) && !has_slash);
        for (int i = 0; i < n; i++) if (!(i == 0 && rootless)) tones[m++] = pcmod(iv[i]);
        int best[VC_MAX_IV] = {0}, bestCost = 1 << 30;
        int tries = prev ? m : 1;
        for (int r = 0; r < tries; r++) {
            int cand[VC_MAX_IV];
            stack(tones, m, prev ? r : inversion, cand);
            for (int i = 0; i < m; i++) cand[i] += 48 + c->root;
            shift_to_center(cand, m, center);
            for (int sh = -12; sh <= 12; sh += 12) {
                int t[VC_MAX_IV];
                for (int i = 0; i < m; i++) t[i] = cand[i] + sh;
                if (!prev && sh != 0) continue;
                if (bass >= 0 && t[0] <= bass + 2) continue;
                int cost = 0;
                if (prev) {
                    int off = prev->has_bass ? 1 : 0;
                    int mc = mean_x4(t, m) / 4 - center;
                    cost = vl_cost(t, m, prev->notes + off, prev->n - off) * 4 + (mc < 0 ? -mc : mc);
                }
                if (cost < bestCost) { bestCost = cost; memcpy(best, t, sizeof(int) * (size_t)m); }
            }
        }
        if (bestCost == (1 << 30)) { stack(tones, m, inversion, best); for (int i = 0; i < m; i++) best[i] += 60 + c->root; }
        memcpy(up, best, sizeof(int) * (size_t)m);
    } else if (style == VC_VOICE_CLOSE || style == VC_VOICE_DROP2) {
        for (int i = 0; i < n; i++) tones[m++] = pcmod(iv[i]);
        sort_int(tones, m);
        /* unique */
        int u = 0;
        for (int i = 0; i < m; i++) if (u == 0 || tones[i] != tones[u - 1]) tones[u++] = tones[i];
        m = u;
        int bestCost = 1 << 30, best[VC_MAX_IV] = {0};
        int tries = prev ? m : 1;
        for (int r = 0; r < tries; r++) {
            int cand[VC_MAX_IV];
            stack(tones, m, prev ? r : inversion, cand);
            for (int i = 0; i < m; i++) cand[i] += root3;
            if (style == VC_VOICE_DROP2) apply_drop2(cand, m);
            for (int sh = -12; sh <= 12; sh += 12) {
                if (!prev && sh != 0) continue;
                int t[VC_MAX_IV];
                for (int i = 0; i < m; i++) t[i] = cand[i] + sh;
                int cost = 0;
                if (prev) {
                    int off = prev->has_bass ? 1 : 0;
                    int mc = mean_x4(t, m) / 4 - center;
                    cost = vl_cost(t, m, prev->notes + off, prev->n - off) * 4 + (mc < 0 ? -mc : mc);
                }
                if (cost < bestCost) { bestCost = cost; memcpy(best, t, sizeof(int) * (size_t)m); }
            }
        }
        memcpy(up, best, sizeof(int) * (size_t)m);
        if (has_slash) { /* slash bass below the lowest voice */
            int b = up[0] - 1;
            while (pcmod(b) != c->bass) b--;
            bass = b;
        }
    } else { /* SPREAD: root (or slash bass) low, fifth next, colour tones an octave up */
        int fifth = -1;
        for (int i = 1; i < n; i++) {
            int p = pcmod(iv[i]);
            if (fifth < 0 && (p == 7 || p == 6 || p == 8) && iv[i] < 12) fifth = iv[i];
            else tones[m++] = iv[i] < 12 ? iv[i] : iv[i];
        }
        if (has_slash) { /* keep the root as a chord tone above the slash bass */
            tones[m++] = 12;
        }
        int rot = m ? inversion % m : 0;
        int st[VC_MAX_IV];
        stack(tones, m, rot, st);
        int k = 0;
        int rootNote = 36 + c->root + 12 * octave;
        if (fifth >= 0) up[k++] = rootNote + fifth + (has_slash && rootNote + fifth <= bass ? 12 : 0);
        for (int i = 0; i < m; i++) up[k++] = rootNote + 12 + st[i];
        m = k;
        sort_int(up, m);
    }

    /* assemble */
    int k = 0;
    if (bass >= 0) { out->notes[k++] = (unsigned char)(bass < 0 ? 0 : bass > 127 ? 127 : bass); out->has_bass = 1; }
    sort_int(up, m);
    for (int i = 0; i < m && k < VC_MAX_NOTES; i++) {
        int v = up[i];
        while (v > 127) v -= 12;
        while (v < 0) v += 12;
        if (out->has_bass && v <= out->notes[0]) v += 12;
        out->notes[k++] = (unsigned char)v;
    }
    out->n = k;
    /* keep ascending & unique */
    for (int i = 1; i < out->n; i++) {
        int j = i;
        while (j > 0 && out->notes[j] < out->notes[j - 1]) { unsigned char t = out->notes[j]; out->notes[j] = out->notes[j - 1]; out->notes[j - 1] = t; j--; }
    }
    int u = 0;
    for (int i = 0; i < out->n; i++) if (u == 0 || out->notes[i] != out->notes[u - 1]) out->notes[u++] = out->notes[i];
    out->n = u;
}
