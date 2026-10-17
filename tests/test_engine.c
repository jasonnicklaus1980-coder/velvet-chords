#include "../src/engine.h"
#include "../src/export.h"
#include "../src/presets.h"
#include "../src/vcmath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int pcm(int x) { x %= 12; return x < 0 ? x + 12 : x; }

static int mask_of_notes(const char *spelled) { /* "C Eb G Bb D" -> pitch-class mask */
    int m = 0;
    char buf[64];
    strncpy(buf, spelled, 63); buf[63] = 0;
    for (char *t = strtok(buf, " "); t; t = strtok(0, " ")) { int pc = vc_pc_parse(t, 0); if (pc >= 0) m |= 1 << pc; }
    return m;
}

static void test_qualities_against_spelled(void) {
    /* independent expectations, written as note names, for root C */
    static const char *EXP[][2] = {
        {"C", "C E G"}, {"Cm", "C Eb G"}, {"Cdim", "C Eb Gb"}, {"Caug", "C E G#"}, {"Csus2", "C D G"}, {"Csus4", "C F G"},
        {"C5", "C G"}, {"C6", "C E G A"}, {"Cm6", "C Eb G A"}, {"Cadd9", "C E G D"}, {"Cmadd9", "C Eb G D"}, {"C6/9", "C E G A D"},
        {"Cmaj7", "C E G B"}, {"Cm7", "C Eb G Bb"}, {"C7", "C E G Bb"}, {"Cdim7", "C Eb Gb A"}, {"Cm7b5", "C Eb Gb Bb"},
        {"Cmmaj7", "C Eb G B"}, {"C7sus4", "C F G Bb"}, {"Cmaj9", "C E G B D"}, {"Cm9", "C Eb G Bb D"}, {"C9", "C E G Bb D"},
        {"C9sus4", "C F G Bb D"}, {"C11", "C G Bb D F"}, {"Cm11", "C Eb G Bb D F"}, {"Cmaj7#11", "C E G B F#"},
        {"C13", "C E Bb D A"}, {"Cm13", "C Eb Bb D A"}, {"Cmaj13", "C E B D A"}, {"C7b9", "C E G Bb Db"}, {"C7#9", "C E G Bb D#"},
        {"C7#5", "C E G# Bb"}, {"C7b5", "C E Gb Bb"}, {"C9#11", "C E Bb D F#"}, {"C13b9", "C E Bb Db A"}, {"C7alt", "C E Bb D# Ab"},
        {"Cmmaj9", "C Eb G B D"}, {"Cm6/9", "C Eb G A D"}, {"Cmaj7#5", "C E G# B"}, {"C7sus2", "C D G Bb"}, {"Cqrt", "C F Bb Eb"},
        /* aliases */
        {"CM7", "C E G B"}, {"C-7", "C Eb G Bb"}, {"Cmin9", "C Eb G Bb D"}, {"C\xc3\xb8", "C Eb Gb Bb"}, {"Co7", "C Eb Gb A"},
        {"C7(#9)", "C E G Bb D#"}, {"Cm(maj7)", "C Eb G B"}, {"C69", "C E G A D"}, {"Csus", "C F G"}, {"C+", "C E G#"},
    };
    int n = (int)(sizeof(EXP) / sizeof(EXP[0]));
    CHECK(n - 10 == VC_NUM_QUALITIES, "every quality needs a spelled expectation (%d vs %d)", n - 10, VC_NUM_QUALITIES);
    for (int i = 0; i < n; i++) {
        ChordSpec c;
        int ok = vc_chord_parse(EXP[i][0], &c);
        CHECK(ok, "parse %s", EXP[i][0]);
        if (!ok) continue;
        CHECK(vc_chord_pcmask(&c) == mask_of_notes(EXP[i][1]), "%s pitch classes (got mask %x)", EXP[i][0], vc_chord_pcmask(&c));
    }
}

static void test_parse(void) {
    ChordSpec c;
    CHECK(vc_chord_parse("G#m/F#", &c) && c.root == 8 && c.bass == 6 && !strcmp(VC_QUALITIES[c.quality].suffix, "m"), "G#m/F#");
    CHECK(vc_chord_parse("A/F#", &c) && c.root == 9 && c.bass == 6, "A/F#");
    CHECK(vc_chord_parse("C6/9", &c) && c.bass == -1 && !strcmp(VC_QUALITIES[c.quality].suffix, "6/9"), "6/9 is not a slash chord");
    CHECK(vc_chord_parse("C6/9/E", &c) && c.bass == 4, "6/9 over E");
    CHECK(vc_chord_parse("Dbmaj7", &c) && c.root == 1, "Dbmaj7");
    CHECK(vc_chord_parse("C/C", &c) && c.bass == -1, "C/C has no separate bass");
    CHECK(!vc_chord_parse("H7", &c), "reject H7");
    CHECK(!vc_chord_parse("Cxyz", &c), "reject unknown suffix");
    CHECK(!vc_chord_parse("C/X", &c), "reject bad bass");
    char buf[32];
    vc_chord_parse("Ebm7b5/A", &c);
    vc_chord_name(&c, 1, buf, sizeof(buf));
    CHECK(!strcmp(buf, "Ebm7b5/A"), "name round trip (%s)", buf);
}

static void test_voicings(void) {
    int total = 0;
    for (int q = 0; q < VC_NUM_QUALITIES; q++)
        for (int root = 0; root < 12; root++)
            for (int bass = -1; bass < 12; bass += 4) /* -1, 3, 7, 11 */
                for (int style = 0; style < VC_VOICE_COUNT; style++)
                    for (int inv = 0; inv < 4; inv++)
                        for (int oct = -2; oct <= 2; oct++) {
                            ChordSpec c;
                            memset(&c, 0, sizeof(c));
                            c.root = root; c.quality = q; c.bass = (bass == root) ? -1 : bass;
                            Voicing v;
                            vc_voice(&c, style, inv, oct, 0, &v);
                            total++;
                            int m = 0, ok = 1;
                            for (int i = 0; i < v.n; i++) { m |= 1 << pcm(v.notes[i]); if (i && v.notes[i] <= v.notes[i - 1]) ok = 0; }
                            int want = vc_chord_pcmask(&c);
                            if (style == VC_VOICE_UPPER_ONLY && c.bass >= 0) want &= ~(1 << c.bass) | vc_chord_pcmask(&(ChordSpec){.root = root, .quality = q, .bass = -1});
                            CHECK(ok, "ascending unique q=%s root=%d style=%d", VC_QUALITIES[q].suffix, root, style);
                            CHECK(m == want, "pitch classes q=%s root=%d bass=%d style=%d inv=%d oct=%d (got %x want %x)",
                                  VC_QUALITIES[q].suffix, root, c.bass, style, inv, oct, m, want);
                            if (c.bass >= 0 && style != VC_VOICE_UPPER_ONLY)
                                CHECK(pcm(v.notes[0]) == c.bass, "slash bass lowest q=%s root=%d bass=%d style=%d", VC_QUALITIES[q].suffix, root, c.bass, style);
                            if ((style == VC_VOICE_BASS_UPPER || style == VC_VOICE_SPREAD) && c.bass < 0)
                                CHECK(pcm(v.notes[0]) == root && v.has_bass, "root in bass style=%d", style);
                            CHECK(v.notes[0] >= 0 && v.notes[v.n - 1] <= 127, "range");
                        }
    printf("  voiced %d chord/voicing combinations\n", total);

    /* inversions of a close triad: bass note changes C -> E -> G */
    ChordSpec c;
    vc_chord_parse("C", &c);
    Voicing v;
    int expect[3] = {0, 4, 7};
    for (int inv = 0; inv < 3; inv++) {
        vc_voice(&c, VC_VOICE_CLOSE, inv, 0, 0, &v);
        CHECK(pcm(v.notes[0]) == expect[inv], "close inversion %d bass", inv);
    }
    /* drop 2 of Cmaj7 root position: C3 E3 G3 B3 -> G2 C3 E3 B3 */
    vc_chord_parse("Cmaj7", &c);
    vc_voice(&c, VC_VOICE_DROP2, 0, 0, 0, &v);
    CHECK(v.n == 4 && v.notes[0] == 43 && v.notes[1] == 48 && v.notes[2] == 52 && v.notes[3] == 59, "drop2 Cmaj7 (%d %d %d %d)", v.notes[0], v.notes[1], v.notes[2], v.notes[3]);
    /* bass+upper Fm9: F1 bass, rootless Ab C Eb G around the middle */
    vc_chord_parse("Fm9", &c);
    vc_voice(&c, VC_VOICE_BASS_UPPER, 0, 0, 0, &v);
    CHECK(v.n == 5 && v.notes[0] == 41, "Fm9 bass F (got %d notes, bass %d)", v.n, v.notes[0]);

    /* transposition preserves interval structure */
    for (int q = 0; q < VC_NUM_QUALITIES; q++) {
        ChordSpec a = {0}, b;
        a.quality = q; a.bass = -1;
        Voicing va, vb;
        vc_voice(&a, VC_VOICE_CLOSE, 0, 0, 0, &va);
        for (int t = 1; t < 12; t++) {
            b = vc_chord_transpose(&a, t);
            vc_voice(&b, VC_VOICE_CLOSE, 0, 0, 0, &vb);
            int same = va.n == vb.n;
            for (int i = 0; same && i < va.n; i++) same = (vb.notes[i] - va.notes[i]) == t;
            CHECK(same, "transpose %s by %d keeps shape", VC_QUALITIES[q].suffix, t);
        }
    }

    /* voice leading keeps neighbouring chords close */
    const char *prog[4] = {"Dbmaj7", "C7", "Fm7", "Ebm7"};
    Voicing prev, cur;
    int have = 0, maxmove = 0;
    for (int i = 0; i < 4; i++) {
        vc_chord_parse(prog[i], &c);
        vc_voice(&c, VC_VOICE_BASS_UPPER, 0, 0, have ? &prev : 0, &cur);
        if (have) {
            int top_move = abs((int)cur.notes[cur.n - 1] - (int)prev.notes[prev.n - 1]);
            if (top_move > maxmove) maxmove = top_move;
        }
        prev = cur; have = 1;
    }
    CHECK(maxmove <= 5, "voice-led top voice moves at most a 4th (moved %d)", maxmove);
}

static void test_songs(void) {
    for (int s = 0; s < VC_NUM_SONGS; s++) {
        const Song *so = &VC_SONGS[s];
        CHECK(so->nsections > 0, "%s has sections", so->title);
        CHECK(strcmp(so->status, "verified") != 0, "%s: nothing in the starter set is marked verified", so->title);
        CHECK(!strcmp(so->confidence, "high") || !strcmp(so->confidence, "medium") || !strcmp(so->confidence, "low"), "confidence");
        CHECK(!strcmp(so->status, "original") ? so->year == 0 && so->bpm > 0 : (so->year >= 1960 && so->year <= 2019), "%s year in range (originals: no year, own tempo)", so->title);
        for (int t = 0; t < s; t++) CHECK(strcmp(VC_SONGS[t].id, so->id), "duplicate id %s", so->id);
        float bar = so->ts_num * 4.f / so->ts_den;
        for (int k = 0; k < so->nsections; k++) {
            const SongSection *sec = &so->sections[k];
            float total = 0;
            for (int e = 0; e < sec->nev; e++) {
                ChordSpec c;
                CHECK(vc_chord_parse(sec->ev[e].chord, &c), "%s / %s: chord '%s' parses", so->title, sec->name, sec->ev[e].chord);
                total += sec->ev[e].beats;
            }
            float bars = total / bar;
            CHECK(bars == (float)(int)bars, "%s / %s: whole bars (%.2f)", so->title, sec->name, bars);
        }
    }
    int seen_original = 0;
    for (int s = 0; s < VC_NUM_SONGS; s++) { /* real songs first, so saved projects keep their song numbers */
        int orig = !strcmp(VC_SONGS[s].status, "original");
        CHECK(!(seen_original && !orig), "real songs listed before originals");
        seen_original |= orig;
    }
    printf("  %d songs checked\n", VC_NUM_SONGS);
}

static void test_layouts(void) {
    LayoutParams lp;
    memset(&lp, 0, sizeof(lp));
    static PadChord pads[VC_PADS];
    static Voicing v[VC_PADS];
    CustomPad custom[VC_PADS];
    memset(custom, 0, sizeof(custom));
    lp.mode = VC_MODE_KEY; lp.key = 0; lp.scale = 0;
    vc_build_pads(&lp, custom, pads);
    const char *cmaj[7] = {"Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7b5"};
    for (int i = 0; i < 7; i++) CHECK(!strcmp(pads[i].name, cmaj[i]), "C major diatonic pad %d = %s (got %s)", i + 1, cmaj[i], pads[i].name);
    CHECK(!strcmp(pads[8].name, "Cmaj9") && !strcmp(pads[9].name, "Dm9"), "C major 9ths (%s %s)", pads[8].name, pads[9].name);
    CHECK(!strcmp(pads[0].roman, "Imaj7") && !strcmp(pads[1].roman, "ii7"), "roman numerals (%s %s)", pads[0].roman, pads[1].roman);
    CHECK(!strcmp(pads[24].name, "A7") && !strcmp(pads[24].roman, "V7/ii"), "secondary dominant V7/ii (%s %s)", pads[24].name, pads[24].roman);
    lp.key = 9; lp.scale = 2; /* A harmonic minor */
    vc_build_pads(&lp, custom, pads);
    CHECK(!strcmp(pads[4].name, "E7"), "A harmonic minor V7 = E7 (got %s)", pads[4].name);
    CHECK(!strcmp(pads[0].name, "Ammaj7"), "A harmonic minor i = Ammaj7 (got %s)", pads[0].name);
    for (int sc = 0; sc < VC_NUM_SCALES; sc++)
        for (int key = 0; key < 12; key++) {
            lp.scale = sc; lp.key = key;
            vc_build_pads(&lp, custom, pads);
            vc_voice_pads(&lp, pads, v);
            const Scale *S = &VC_SCALES[sc];
            int smask = 0;
            for (int j = 0; j < S->n; j++) smask |= 1 << pcm(key + S->iv[j]);
            for (int p = 0; p < 16; p++) if (pads[p].valid) CHECK((vc_chord_pcmask(&pads[p].spec) & ~smask) == 0, "scale %s key %d pad %d stays in scale", S->name, key, p);
        }
    /* song mode maps sections to banks, collapses repeats */
    for (int s = 0; s < VC_NUM_SONGS; s++) {
        lp.mode = VC_MODE_SONG; lp.song = s; lp.section = 0; lp.transpose = 0;
        vc_build_pads(&lp, custom, pads);
        CHECK(pads[0].valid, "%s bank A pad 1", VC_SONGS[s].title);
        if (VC_SONGS[s].nsections > 1) CHECK(pads[16].valid, "%s second section on bank B", VC_SONGS[s].title);
        lp.transpose = 3;
        static PadChord tp[VC_PADS];
        vc_build_pads(&lp, custom, tp);
        for (int p = 0; p < VC_PADS; p++) if (pads[p].valid) {
            CHECK(tp[p].spec.quality == pads[p].spec.quality && tp[p].spec.root == pcm(pads[p].spec.root + 3), "transposed pad keeps quality");
        }
    }
    /* Just the Two of Us pads */
    for (int s = 0; s < VC_NUM_SONGS; s++) if (!strcmp(VC_SONGS[s].id, "just-the-two-of-us-1980")) {
        lp.song = s; lp.transpose = 0;
        vc_build_pads(&lp, custom, pads);
        const char *exp[5] = {"Dbmaj7", "C7", "Fm7", "Ebm7", "Ab7"};
        for (int i = 0; i < 5; i++) CHECK(!strcmp(pads[i].name, exp[i]), "JTTOU pad %d %s", i + 1, pads[i].name);
        CHECK(!pads[5].valid, "JTTOU pad 6 empty");
    }
}

/* ---- note handling ---- */

static int rendered_active(Engine *e) { return sy_active(&e->synth); }

static void run(Engine *e, int blocks, int n, float *L, float *R) { for (int b = 0; b < blocks; b++) en_process(e, L, R, n); }

static void test_note_handling(void) {
    static Engine e;
    static float L[512], R[512];
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_RNB;
    e.midi_out = 1;
    e.dirty = 1;
    long ons[128] = {0}, offs[128] = {0};
    unsigned rng = 7;
    for (int iter = 0; iter < 4000; iter++) {
        int nev = 0;
        for (int k = 0; k < 6; k++) {
            rng = rng * 1103515245u + 12345u;
            int r = (int)(rng >> 8);
            int note = 30 + r % 80;
            int frame = (r >> 8) % 512;
            int kind = (r >> 3) % 10;
            if (kind < 5) en_push_midi(&e, frame, 0x90, (unsigned char)note, (unsigned char)(1 + (r >> 5) % 127));
            else if (kind < 9) en_push_midi(&e, frame, 0x80, (unsigned char)note, 0);
            else en_push_midi(&e, frame, 0xB0, 64, (r & 1) ? 127 : 0);
            nev++;
        }
        if (iter % 500 == 0) { e.lanes[0].order = (iter / 500) % ARP_COUNT; e.strum_ms = (float)((iter / 500) % 3) * 40.f; e.strum_dir = (iter / 500) % STRUM_COUNT; }
        if (iter % 700 == 0) { e.lp.mode = (iter / 700) % VC_MODE_COUNT; e.dirty = 1; } /* bank/layout changes while playing */
        en_process(&e, L, R, 512);
        for (int i = 0; i < e.nout; i++) { if ((e.out[i].d[0] & 0xF0) == 0x90) ons[e.out[i].d[1]]++; else offs[e.out[i].d[1]]++; }
        e.nout = 0;
        for (int i = 0; i < 512; i++) CHECK(L[i] == L[i] && L[i] <= 1.f && L[i] >= -1.f, "finite audio");
        if (fails > 20) return;
    }
    /* release everything */
    for (int n = 0; n < 128; n++) en_push_midi(&e, 0, 0x80, (unsigned char)n, 0);
    en_push_midi(&e, 0, 0xB0, 64, 0);
    run(&e, 1, 512, L, R);
    for (int i = 0; i < e.nout; i++) { if ((e.out[i].d[0] & 0xF0) == 0x90) ons[e.out[i].d[1]]++; else offs[e.out[i].d[1]]++; }
    e.nout = 0;
    run(&e, 48000 * 12 / 512, 512, L, R); /* 12 s of release tails */
    CHECK(en_owner_count(&e) == 0, "no held owners after all note-offs (%d)", en_owner_count(&e));
    CHECK(e.nqueue == 0, "strum queue empty");
    CHECK(rendered_active(&e) == 0, "no stuck synth voices (%d)", rendered_active(&e));
    int bal = 1;
    for (int n = 0; n < 128; n++) { if (ons[n] != offs[n] || e.out_ref[n]) { bal = 0; printf("  note %d on %ld off %ld ref %d\n", n, ons[n], offs[n], e.out_ref[n]); } }
    CHECK(bal, "MIDI out note-on/off balanced");

    /* retrigger the same pad without a note-off, then one note-off */
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_RNB; e.dirty = 1;
    en_push_midi(&e, 0, 0x90, 36, 100);
    en_push_midi(&e, 10, 0x90, 36, 100);
    en_push_midi(&e, 20, 0x80, 36, 0);
    run(&e, 1, 512, L, R);
    run(&e, 48000 * 6 / 512, 512, L, R);
    CHECK(en_owner_count(&e) == 0 && sy_active(&e.synth) == 0, "double note-on then one note-off leaves nothing stuck");

    /* sustain: chord keeps ringing after pad release until pedal up */
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_RNB; e.synth.sound = SY_PAD; e.dirty = 1;
    en_push_midi(&e, 0, 0xB0, 64, 127);
    en_push_midi(&e, 0, 0x90, 36, 100);
    en_push_midi(&e, 100, 0x80, 36, 0);
    run(&e, 50, 512, L, R);
    CHECK(en_owner_count(&e) == 1, "sustained owner kept");
    en_push_midi(&e, 0, 0xB0, 64, 0);
    run(&e, 48000 * 8 / 512, 512, L, R);
    CHECK(en_owner_count(&e) == 0 && sy_active(&e.synth) == 0, "pedal up releases");

    /* strum timing: 50 ms at 48 kHz = 2400 samples per note */
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_TYPES; e.lp.voicing = VC_VOICE_CLOSE; e.dirty = 1;
    e.midi_out = 1; e.strum_ms = 50.f;
    en_push_midi(&e, 100, 0x90, 36 + 3, 100); /* Cmaj7, 4 notes */
    int64_t times[8]; int nt = 0;
    for (int b = 0; b < 40; b++) {
        int64_t t0 = e.now;
        en_process(&e, L, R, 512);
        for (int i = 0; i < e.nout; i++) if ((e.out[i].d[0] & 0xF0) == 0x90 && nt < 8) times[nt++] = t0 + e.out[i].frame;
        e.nout = 0;
    }
    CHECK(nt == 4, "strum played 4 notes (%d)", nt);
    for (int i = 1; i < nt; i++) CHECK(times[i] - times[i - 1] == 2400, "strum gap %d = %lld", i, (long long)(times[i] - times[i - 1]));

    /* arp timing: 120 bpm, 1/16 = 6000 samples at 48 kHz */
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_TYPES; e.dirty = 1;
    e.midi_out = 1; e.lanes[0].order = ARP_UP; e.lanes[0].rate = RATE_16; e.tempo = 120.0;
    en_push_midi(&e, 0, 0x90, 36 + 3, 100);
    nt = 0;
    for (int b = 0; b < 60; b++) {
        int64_t t0 = e.now;
        en_process(&e, L, R, 512);
        for (int i = 0; i < e.nout; i++) if ((e.out[i].d[0] & 0xF0) == 0x90 && nt < 8) times[nt++] = t0 + e.out[i].frame;
        e.nout = 0;
    }
    CHECK(nt >= 4, "arp produced steps (%d)", nt);
    for (int i = 1; i < nt; i++) CHECK(times[i] - times[i - 1] == 6000, "arp step %d = %lld", i, (long long)(times[i] - times[i - 1]));

    /* velocity changes level */
    float rms[2];
    for (int k = 0; k < 2; k++) {
        en_init(&e, 48000.f);
        e.lp.mode = VC_MODE_RNB; e.dirty = 1;
        en_push_midi(&e, 0, 0x90, 36, k ? 127 : 30);
        double acc = 0;
        for (int b = 0; b < 20; b++) { en_process(&e, L, R, 512); for (int i = 0; i < 512; i++) acc += L[i] * L[i]; }
        rms[k] = (float)acc;
    }
    CHECK(rms[1] > rms[0] * 2.f, "velocity 127 louder than 30");
}

/* ---- pattern generator ---- */
typedef struct { int64_t t; int note, on, vel; } Hit;

static int collect(Engine *e, int blocks, Hit *h, int max) {
    static float L[512], R[512];
    int n = 0;
    for (int b = 0; b < blocks; b++) {
        int64_t t0 = e->now;
        en_process(e, L, R, 512);
        for (int i = 0; i < e->nout && n < max; i++) {
            int on = (e->out[i].d[0] & 0xF0) == 0x90;
            h[n].t = t0 + e->out[i].frame; h[n].note = e->out[i].d[1]; h[n].on = on; h[n].vel = e->out[i].d[2]; n++;
        }
        e->nout = 0;
    }
    return n;
}

static Engine *pat_engine(const char *steps, double tempo, int rate) {
    static Engine e;
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_TYPES; e.lp.voicing = VC_VOICE_BASS_UPPER; e.dirty = 1;
    e.midi_out = 1; e.tempo = tempo; e.pat_rate = rate; e.pat_sync = 1; e.playing = 0;
    e.pattern_on = 1; e.gate = 1.0f;
    memcpy(e.pat, steps, EN_STEPS);
    return &e;
}

static void test_patterns(void) {
    static Hit h[4000];
    /* quarters at 120 bpm, 1/16 steps: one chord every 4 steps = 24000 samples */
    Engine *e = pat_engine("X---x---x---x---", 120.0, PRATE_16);
    en_push_midi(e, 100, 0x90, 36 + 3, 100); /* Cmaj7: C1 bass + upper */
    int n = collect(e, 220, h, 4000);
    int64_t ons[8]; int k = 0;
    int lowest = 999;
    for (int i = 0; i < n; i++) if (h[i].on && h[i].note < lowest) lowest = h[i].note;
    for (int i = 0; i < n && k < 8; i++) if (h[i].on && h[i].note == lowest) ons[k++] = h[i].t;
    CHECK(k >= 4, "quarter pattern repeats (%d)", k);
    for (int i = 1; i < k; i++) CHECK(ons[i] - ons[i - 1] == 24000, "quarter spacing %lld", (long long)(ons[i] - ons[i - 1]));
    CHECK(ons[0] == 100, "first chord at the press (%lld)", (long long)ons[0]);
    /* accent: first (X) louder than second (x) */
    int v1 = -1, v2 = -1;
    for (int i = 0; i < n; i++) if (h[i].on && h[i].note == lowest) { if (v1 < 0) v1 = h[i].vel; else if (v2 < 0) v2 = h[i].vel; }
    CHECK(v1 > v2 && v2 > 0, "accent louder (%d vs %d)", v1, v2);

    /* ties and gate: X--- with gate 50%% holds 2 steps = 12000 samples */
    e = pat_engine("X---............", 120.0, PRATE_16);
    e->gate = 0.5f;
    en_push_midi(e, 0, 0x90, 36 + 3, 100);
    n = collect(e, 40, h, 4000);
    int64_t on_t = -1, off_t = -1;
    for (int i = 0; i < n; i++) { if (h[i].on && on_t < 0) on_t = h[i].t; if (!h[i].on && off_t < 0) off_t = h[i].t; }
    CHECK(off_t - on_t == 12000, "tie + 50%% gate = 2 steps (%lld)", (long long)(off_t - on_t));

    /* a tap on a second pad takes the pattern for as long as it is down; when it lets go, the held pad plays again */
    e = pat_engine("X.X.X.X.X.X.X.X.", 120.0, PRATE_16);
    en_push_midi(e, 0, 0x90, 39, 100);
    collect(e, 20, h, 4000);
    en_push_midi(e, 0, 0x90, 40, 100);
    collect(e, 4, h, 4000);
    en_push_midi(e, 0, 0x80, 40, 0);
    n = collect(e, 60, h, 4000);
    k = 0;
    for (int i = 0; i < n; i++) if (h[i].on) k++;
    CHECK(k >= 8, "held pad resumes its pattern after a tap on a second pad (%d hits)", k);

    /* swing 50%%: odd 16ths late by a quarter step (1500 samples at 120 bpm) */
    e = pat_engine("xxxxxxxxxxxxxxxx", 120.0, PRATE_16);
    e->swing = 0.5f;
    en_push_midi(e, 0, 0x90, 36 + 3, 100);
    n = collect(e, 60, h, 4000);
    k = 0;
    for (int i = 0; i < n && k < 6; i++) if (h[i].on && h[i].note == lowest) ons[k++] = h[i].t;
    CHECK(k >= 5 && ons[1] - ons[0] == 7500 && ons[2] - ons[1] == 4500 && ons[3] - ons[2] == 7500, "swing alternates 7500/4500 (%lld %lld %lld)",
          (long long)(ons[1] - ons[0]), (long long)(ons[2] - ons[1]), (long long)(ons[3] - ons[2]));

    /* bass-only and upper-only steps */
    e = pat_engine("b...u...........", 120.0, PRATE_16);
    en_push_midi(e, 0, 0x90, 36 + 3, 100);
    n = collect(e, 70, h, 4000);
    int first_group = 0, second_group = 0, bass_in_second = 0;
    for (int i = 0; i < n; i++) if (h[i].on) {
        if (h[i].t < 12000) { first_group++; CHECK(h[i].note == lowest, "b step plays only the bass"); }
        else { second_group++; if (h[i].note == lowest) bass_in_second = 1; }
    }
    CHECK(first_group == 1 && second_group >= 2 && !bass_in_second, "u step plays the upper notes only (%d/%d)", first_group, second_group);

    /* host sync: song position 0.1 beats = 0.4 of a 16th; pressing late waits for the next step (3600 samples) */
    e = pat_engine("xxxxxxxxxxxxxxxx", 120.0, PRATE_16);
    e->playing = 1; e->ppq = 0.1;
    en_push_midi(e, 0, 0x90, 36 + 3, 100);
    {
        static float L[512], R[512];
        int64_t first = -1;
        for (int b = 0; b < 20 && first < 0; b++) {
            int64_t t0 = e->now;
            en_process(e, L, R, 512);
            for (int i = 0; i < e->nout; i++) if ((e->out[i].d[0] & 0xF0) == 0x90 && first < 0) first = t0 + e->out[i].frame;
            e->nout = 0;
            e->ppq += 512.0 / 48000.0 * 2.0; /* host advances 120 bpm */
        }
        CHECK(first >= 3599 && first <= 3601, "synced start on the next 16th (%lld)", (long long)first);
    }

    /* releasing the pad stops the pattern; nothing left hanging */
    e = pat_engine("X.xX.xX.x.X.x.X.", 100.0, PRATE_16);
    e->swing = 0.3f;
    en_push_midi(e, 0, 0x90, 36 + 5, 100);
    collect(e, 50, h, 4000);
    en_push_midi(e, 0, 0x80, 36 + 5, 0);
    n = collect(e, 400, h, 4000);
    int stray = 0;
    for (int i = 0; i < n; i++) if (h[i].on) stray++;
    int refs = 0;
    for (int i = 0; i < 128; i++) refs += e->out_ref[i];
    CHECK(stray == 0 && refs == 0 && en_owner_count(e) == 0 && sy_active(&e->synth) == 0, "pattern stops cleanly (%d %d)", stray, refs);

    /* random abuse with patterns and sync toggling */
    unsigned rng = 99;
    long on_count[128] = {0}, off_count[128] = {0};
    e = pat_engine("X.xb.uU-x.X-b.u.", 128.0, PRATE_16T);
    static float L[512], R[512];
    for (int it = 0; it < 3000; it++) {
        rng = rng * 1103515245u + 12345u;
        int r = (int)(rng >> 8);
        if (r % 3 == 0) en_push_midi(e, r % 512, 0x90, (unsigned char)(36 + (r >> 4) % 20), 80);
        if (r % 4 == 1) en_push_midi(e, (r >> 3) % 512, 0x80, (unsigned char)(36 + (r >> 6) % 20), 0);
        if (it % 300 == 0) { e->pat_rate = (it / 300) % PRATE_COUNT; e->swing = (float)((it / 300) % 4) / 4.f; e->playing = (it / 300) & 1; }
        if (it % 777 == 0) e->pattern_on = !e->pattern_on;
        if (it % 500 == 250) { e->smart = (it / 500) % SMART_COUNT; e->smart_mode = (it / 500) % SMODE_COUNT; e->dirty = 1; } /* smart pads mid-play */
        e->ppq += 512.0 / 48000.0 * 128.0 / 60.0;
        en_process(e, L, R, 512);
        for (int i = 0; i < e->nout; i++) { if ((e->out[i].d[0] & 0xF0) == 0x90) on_count[e->out[i].d[1]]++; else off_count[e->out[i].d[1]]++; }
        e->nout = 0;
    }
    for (int nn = 0; nn < 128; nn++) en_push_midi(e, 0, 0x80, (unsigned char)nn, 0);
    for (int b = 0; b < 1200; b++) {
        en_process(e, L, R, 512);
        for (int i = 0; i < e->nout; i++) { if ((e->out[i].d[0] & 0xF0) == 0x90) on_count[e->out[i].d[1]]++; else off_count[e->out[i].d[1]]++; }
        e->nout = 0;
    }
    int bal = 1;
    for (int nn = 0; nn < 128; nn++) if (on_count[nn] != off_count[nn]) bal = 0;
    CHECK(bal && en_owner_count(e) == 0 && sy_active(&e->synth) == 0, "random pattern abuse: balanced MIDI, nothing stuck");

    /* output channel */
    e = pat_engine("X---------------", 120.0, PRATE_16);
    e->out_channel = 9;
    en_push_midi(e, 0, 0x90, 36, 100);
    {
        en_process(e, L, R, 512);
        CHECK(e->nout > 0 && e->out[0].d[0] == 0x99, "MIDI out on channel 10 (0x%02x)", e->nout ? e->out[0].d[0] : 0);
        e->nout = 0;
    }
    for (int i = 0; i < EN_NUM_PATTERNS; i++) {
        CHECK(strlen(EN_PATTERNS[i].steps) == EN_STEPS, "preset %s has 16 steps", EN_PATTERNS[i].name);
        for (int s2 = 0; s2 < EN_STEPS; s2++) CHECK(strchr(EN_STEP_CHARS, EN_PATTERNS[i].steps[s2]) != 0, "preset %s valid chars", EN_PATTERNS[i].name);
    }
}

/* ---- hold, repeat, melody, MIDI import ---- */
static void test_tools(void) {
    static Engine e;
    static float L[512], R[512];
    /* Hold: chord keeps sounding after release; another pad replaces it; same pad again turns it off */
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_RNB; e.dirty = 1; e.hold = 1;
    en_push_midi(&e, 0, 0x90, 36, 100); en_push_midi(&e, 100, 0x80, 36, 0);
    run(&e, 20, 512, L, R);
    CHECK(en_owner_count(&e) == 1 && sy_active(&e.synth) > 0, "hold keeps the chord after pad release");
    en_push_midi(&e, 0, 0x90, 37, 100); en_push_midi(&e, 100, 0x80, 37, 0);
    run(&e, 2, 512, L, R);
    CHECK(en_owner_count(&e) == 1 && e.latched_note == 37, "another pad replaces the held chord (owners %d)", en_owner_count(&e));
    en_push_midi(&e, 0, 0x90, 37, 100); en_push_midi(&e, 100, 0x80, 37, 0);
    run(&e, 400, 512, L, R);
    CHECK(en_owner_count(&e) == 0 && sy_active(&e.synth) == 0, "same pad again turns hold off");
    en_push_midi(&e, 0, 0x90, 38, 100); en_push_midi(&e, 100, 0x80, 38, 0);
    run(&e, 4, 512, L, R);
    e.hold = 0;
    run(&e, 400, 512, L, R);
    CHECK(en_owner_count(&e) == 0 && sy_active(&e.synth) == 0, "switching hold off releases the latched chord");

    /* Learn all 16 pads: an MPC whose pads send rows top-down (pad 1 = note 48 ... pad 13 = note 36) */
    {
        static const int SENDS[16] = {48, 49, 50, 51, 44, 45, 46, 47, 40, 41, 42, 43, 36, 37, 38, 39};
        en_init(&e, 48000.f);
        e.lp.mode = VC_MODE_RNB; e.dirty = 1;
        e.learn_all = 1;
        for (int k = 0; k < 16; k++) { en_push_midi(&e, 0, 0x90, (unsigned char)SENDS[k], 100); en_push_midi(&e, 10, 0x80, (unsigned char)SENDS[k], 0); run(&e, 1, 512, L, R); }
        CHECK(e.map_set && e.learn_all == 0, "learning finishes after 16 pads");
        CHECK(sy_active(&e.synth) == 0, "learning taps don't play");
        int ok = 1;
        for (int k = 0; k < 16; k++) if (en_pad_index(&e, SENDS[k]) != k) ok = 0;
        CHECK(ok, "every learned pad maps to its own position");
        CHECK(en_pad_index(&e, 48 + 16) == 16 && en_pad_index(&e, 39 + 48) == 63, "banks B-D follow the learned layout");
        CHECK(en_pad_index(&e, 100) == -1, "unlearned notes are ignored");
        en_push_midi(&e, 0, 0x90, 48, 100); run(&e, 1, 512, L, R);
        CHECK(e.last_pad == 0 && sy_active(&e.synth) > 0, "MPC pad 1 plays pad 1's chord (last pad %d)", e.last_pad);
        en_push_midi(&e, 0, 0x80, 48, 0); run(&e, 400, 512, L, R);
        e.learn = 1;   /* the simple Learn pad 1 replaces a learned layout */
        en_push_midi(&e, 0, 0x90, 60, 100); en_push_midi(&e, 10, 0x80, 60, 0); run(&e, 1, 512, L, R);
        CHECK(!e.map_set && e.pad_base == 60 && en_pad_index(&e, 61) == 1, "Learn pad 1 switches back to a simple layout");
        run(&e, 400, 512, L, R);
    }

    /* MPC Note Repeat (1/32 at 120 bpm) must not pile up voices: retriggered notes choke their old tails */
    for (int snd = 0; snd < 3; snd++) {
        en_init(&e, 48000.f);
        e.lp.mode = VC_MODE_RNB; e.dirty = 1; e.synth.sound = snd;
        int maxv = 0;
        for (long b = 0; b < 48000 * 6 / 256; b++) {
            for (int f = 0; f < 256; f++) {
                long s = b * 256 + f;
                if (s % 3000 == 0) en_push_midi(&e, f, 0x90, 36, 100);
                if (s % 3000 == 2800) en_push_midi(&e, f, 0x80, 36, 0);
            }
            en_process(&e, L, R, 256);
            if (sy_active(&e.synth) > maxv) maxv = sy_active(&e.synth);
        }
        CHECK(maxv <= 16, "note repeat keeps voices low (%s: %d)", SY_SOUND_NAMES[snd], maxv);
    }

    /* Repeat at 1/8, 120 bpm = 12000 samples at 48 kHz */
    static Hit h[4000];
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_TYPES; e.dirty = 1; e.midi_out = 1; e.tempo = 120.0; e.repeat = REP_8;
    en_push_midi(&e, 0, 0x90, 36 + 3, 100);
    int n = collect(&e, 120, h, 4000), low = 999;
    for (int i = 0; i < n; i++) if (h[i].on && h[i].note < low) low = h[i].note;
    int64_t ons[8]; int k = 0;
    for (int i = 0; i < n && k < 8; i++) if (h[i].on && h[i].note == low) ons[k++] = h[i].t;
    CHECK(k >= 4, "repeat retriggers (%d)", k);
    for (int i = 1; i < k; i++) CHECK(ons[i] - ons[i - 1] == 12000, "repeat spacing %lld", (long long)(ons[i] - ons[i - 1]));
    en_push_midi(&e, 0, 0x80, 36 + 3, 0);
    run(&e, 300, 512, L, R);
    CHECK(en_owner_count(&e) == 0 && sy_active(&e.synth) == 0, "repeat stops on release");

    /* Melody: notes above the chord, from the chord and the scale; stops with the chord */
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_KEY; e.lp.key = 0; e.lp.scale = 0; e.dirty = 1; /* C major */
    e.midi_out = 1; e.tempo = 120.0; e.mel_mode = MEL_BUSY; e.mel_rate = MRATE_16;
    en_push_midi(&e, 0, 0x90, 36, 100);   /* Cmaj7 */
    n = collect(&e, 400, h, 4000);
    int mel = 0, bad = 0;
    for (int i = 0; i < n; i++) if (h[i].on && h[i].note >= 67) {
        mel++;
        if (!(0xAB5 & (1 << (h[i].note % 12)))) bad++;
    }
    CHECK(mel >= 10, "melody plays over the chord (%d notes)", mel);
    CHECK(bad == 0, "melody notes fit C major (%d outside)", bad);
    long ons2[128] = {0}, offs2[128] = {0};
    for (int i = 0; i < n; i++) { if (h[i].on) ons2[h[i].note]++; else offs2[h[i].note]++; }
    en_push_midi(&e, 0, 0x80, 36, 0);
    int n2 = collect(&e, 400, h, 4000);
    int after = 0;
    for (int i = 0; i < n2; i++) { if (h[i].on) { after++; ons2[h[i].note]++; } else offs2[h[i].note]++; }
    int bal = 1; for (int i = 0; i < 128; i++) if (ons2[i] != offs2[i]) bal = 0;
    CHECK(after == 0 && bal && en_owner_count(&e) == 0, "melody stops with the chord, MIDI balanced (%d late notes)", after);

    /* MIDI import: write a progression with the exporter, read it back, chords named correctly */
    const char *syms[] = {"Dbmaj7", "C7", "Fm7", "Ebm7", "Ab7", "C/E", "Fmaj7/G"};
    int ns = 7;
    MfChord mc[8];
    for (int i = 0; i < ns; i++) {
        ChordSpec c; vc_chord_parse(syms[i], &c);
        Voicing v; vc_voice(&c, VC_VOICE_BASS_UPPER, 0, 0, 0, &v);
        memset(&mc[i], 0, sizeof(mc[i]));
        mc[i].start_beats = 4.f * i; mc[i].dur_beats = 4.f; mc[i].n = v.n; memcpy(mc[i].notes, v.notes, (size_t)v.n); mc[i].vel = 90;
        strcpy(mc[i].name, syms[i]);
        if (i == 5) { /* first inversion C/E written explicitly */
            unsigned char ce[4] = {40, 48, 55, 60}; mc[i].n = 4; memcpy(mc[i].notes, ce, 4);
        }
    }
    vc_midi_write("/tmp/vc_import_test.mid", "t", "", mc, ns, 100, 4, 4);
    ImportChord ic[16]; char err[96];
    int got = vc_midi_import("/tmp/vc_import_test.mid", ic, 16, err, sizeof(err));
    CHECK(got == ns, "imported %d chords (%s)", got, got < 0 ? err : "ok");
    for (int i = 0; i < got && i < ns; i++) {
        char nm[32]; vc_chord_name(&ic[i].spec, 1, nm, sizeof(nm));
        ChordSpec want; vc_chord_parse(syms[i], &want);
        CHECK(vc_chord_pcmask(&ic[i].spec) == vc_chord_pcmask(&want) && ic[i].spec.root == want.root && ic[i].spec.bass == want.bass,
              "import chord %d: %s (want %s)", i + 1, nm, syms[i]);
        CHECK(ic[i].n == mc[i].n && !memcmp(ic[i].notes, mc[i].notes, (size_t)mc[i].n), "import keeps exact notes for %s", syms[i]);
    }
    unsigned char odd[3] = {60, 61, 66}; ChordSpec os; vc_identify_chord(odd, 3, &os);
    CHECK(os.quality == -1 && os.ncustom == 3, "unnamed clusters fall back to their intervals");
    got = vc_midi_import("/nonexistent.mid", ic, 16, err, sizeof(err));
    CHECK(got < 0, "missing file reported");
}

/* every built-in instrument: sounds at the right pitch, stays finite, stops after release */
static double goertzel(const float *x, int n, double f, double sr) {
    double c = 2 * (double)vc_sinc((float)(f / sr) + 0.25f), s0 = 0, s1 = 0, s2 = 0; /* 2 cos(2 pi f / sr) */
    for (int i = 0; i < n; i++) { s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static void test_instruments(void) {
    static Synth sy;
    static float L[48000], R[48000];
    for (int snd = 0; snd < SY_SOUND_COUNT; snd++) {
        sy_init(&sy, 48000.f);
        sy.sound = snd;
        sy_note_on(&sy, 57, 0.8f, 1);                       /* A2 in MIDI 57 = 220 Hz */
        memset(L, 0, sizeof(L)); memset(R, 0, sizeof(R));
        for (int b = 0; b < 48000; b += 256) sy_render(&sy, L + b, R + b, 48000 - b < 256 ? 48000 - b : 256);
        double e = 0; int finite = 1;
        for (int i = 0; i < 48000; i++) { e += L[i] * L[i]; if (!(L[i] == L[i]) || L[i] > 1.f || L[i] < -1.f) finite = 0; }
        if (snd == SY_SILENT) { CHECK(e == 0.0, "silent is silent"); continue; }
        CHECK(e > 1.0 && finite, "%s makes clean sound (%.2f)", SY_SOUND_NAMES[snd], e);
        /* the fundamental must beat its semitone neighbours */
        double f0 = snd == SY_MUSICBOX ? 440.0 : 220.0; /* the music box plays an octave up */
        double p = goertzel(L + 4800, 19200, f0, 48000), lo = goertzel(L + 4800, 19200, f0 / 1.0595, 48000),
               hi = goertzel(L + 4800, 19200, f0 * 1.0595, 48000);
        CHECK(p > 4 * lo && p > 4 * hi, "%s plays in tune (%.3g vs %.3g / %.3g)", SY_SOUND_NAMES[snd], p, lo, hi);
        sy_release(&sy, -1, 1);
        for (int b = 0; b < 48000 * 8; b += 256) { float l[256] = {0}, r[256] = {0}; sy_render(&sy, l, r, 256); }
        CHECK(sy_active(&sy) == 0, "%s stops after release", SY_SOUND_NAMES[snd]);
    }
}

/* ---------- multi arp (ported from Velvet Chords 1.1) ---------- */

/* a custom pad 0 holding the named chord, transposed to `root` */
static void custom_chord(Engine *e, int pad, const char *sym, int root) {
    ChordSpec s;
    CHECK(vc_chord_parse(sym, &s) != 0, "parse %s", sym);
    CustomPad *c = &e->custom[pad];
    memset(c, 0, sizeof(*c));
    c->valid = 1; c->root = (unsigned char)root; c->quality = (unsigned char)s.quality; c->bass = 255;
    en_rebuild(e);
}

static Engine *v11_engine(void) {
    static Engine e;
    en_init(&e, 48000.f);
    e.lp.mode = VC_MODE_CUSTOM;
    e.midi_out = 1;
    e.tempo = 90.0;
    e.dirty = 1;
    custom_chord(&e, 0, "C", 0);
    return &e;
}

/* the arpeggio's position for step k of a sequence of len notes, as the arpeggiator orders them */
static int arp_expect(int mode, int k, int len) {
    int c = k % len;
    int conv = (c & 1) ? len - 1 - c / 2 : c / 2;
    switch (mode) {
    case ARP_DOWN: return len - 1 - c;
    case ARP_UPDOWN: { int period = len > 1 ? 2 * len - 2 : 1, t = k % period; return t < len ? t : period - t; }
    case ARP_CONVERGE: return conv;
    case ARP_DIVERGE: return len - 1 - conv;
    default: return c;
    }
}

static void test_arp_orders(void) {
    static const int modes[] = {ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_CONVERGE, ARP_DIVERGE};
    static Hit h[4000];
    for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        Engine *e = v11_engine();
        e->lanes[0].order = modes[m];
        e->lanes[0].oct = 2;
        e->lanes[0].rate = RATE_16; /* 90 bpm: a step every 8000 samples */
        en_push_midi(e, 0, 0x90, 36, 100);
        int n = collect(e, 200, h, 4000);
        const Voicing *v = &e->voicings[0];
        int len = v->n * 2, ons = 0;
        int note[12];
        for (int i = 0; i < n && ons < 12; i++) if (h[i].on) note[ons++] = h[i].note;
        CHECK(ons == 12, "arp mode %d plays 12 steps (%d)", modes[m], ons);
        for (int k = 0; k < ons; k++) {
            int j = arp_expect(modes[m], k, len);
            int want = v->notes[j % v->n] + 12 * (j / v->n);
            CHECK(note[k] == want, "arp mode %d step %d: %d want %d", modes[m], k, note[k], want);
        }
    }
}

/* ---------- multi arp: four lanes at once ---------- */

static void test_multi_arp(void) {
    static Hit h[6000];
    /* lane 1: up, sixteenths (8000 samples at 90 bpm); lane 2: down, eighths (16000), an octave up */
    Engine *e = v11_engine();
    e->lanes[0].order = ARP_UP; e->lanes[0].oct = 1; e->lanes[0].rate = RATE_16;
    e->lanes[1].order = ARP_DOWN; e->lanes[1].oct = 1; e->lanes[1].rate = RATE_8; e->lanes[1].transpose = 12;
    en_push_midi(e, 0, 0x90, 36, 100);
    int n = collect(e, 200, h, 6000); /* 102400 samples: lane 1 has 13 steps (0..96000), lane 2 has 7 */
    int ons = 0, at0 = 0;
    for (int i = 0; i < n; i++) if (h[i].on) { ons++; if (h[i].t == 0) at0++; }
    CHECK(ons == 20, "two lanes play 13 + 7 steps (%d)", ons);
    CHECK(at0 == 2, "both lanes start on the press (%d)", at0);

    /* a lane switched off while the chord is held stops; the other keeps going */
    e->lanes[1].order = ARP_OFF;
    int64_t from = e->now;
    n = collect(e, 100, h, 6000);
    int late = 0;
    for (int i = 0; i < n; i++) if (h[i].on) late++;
    CHECK(late >= 6 && late <= 7, "after lane 2 is off only lane 1 plays (%d steps in 100 blocks; both lanes would give ~13)", late);
    (void)from;

    /* releasing the pad silences every lane */
    en_push_midi(e, 0, 0x80, 36, 0);
    n = collect(e, 40, h, 6000);
    int on2 = 0, off2 = 0;
    for (int i = 0; i < n; i++) { if (h[i].on) on2++; else off2++; }
    int hanging = 0;
    for (int k = 0; k < 128; k++) hanging += e->out_ref[k] != 0;
    CHECK(hanging == 0, "no notes left sounding after release (%d)", hanging);
    CHECK(en_owner_count(e) == 0, "arp lanes released with the chord");
    (void)on2; (void)off2;

    /* both lanes on, then the chord is replaced by another pad: each lane restarts on the new chord */
    e = v11_engine();
    custom_chord(e, 1, "Dm", 2);
    e->lanes[0].order = ARP_UP; e->lanes[0].oct = 1; e->lanes[0].rate = RATE_8;
    e->lanes[2].order = ARP_CONVERGE; e->lanes[2].oct = 1; e->lanes[2].rate = RATE_8;
    en_push_midi(e, 0, 0x90, 36, 100);
    collect(e, 20, h, 6000);
    en_push_midi(e, 0, 0x90, 37, 100);
    n = collect(e, 100, h, 6000);
    int hanging_after = 0;
    for (int k = 0; k < 128; k++) hanging_after += e->out_ref[k] != 0;
    CHECK(e->lanes[0].owner == e->lanes[2].owner && e->lanes[0].owner == e->next_owner, "the newest chord owns every lane");
    CHECK(n > 0 && hanging_after <= 6, "new chord plays on both lanes (%d sounding)", hanging_after);
    en_push_midi(e, 0, 0x80, 37, 0);
    collect(e, 40, h, 6000);
    hanging_after = 0;
    for (int k = 0; k < 128; k++) hanging_after += e->out_ref[k] != 0;
    CHECK(hanging_after == 0, "second chord released cleanly (%d)", hanging_after);
}

/* MIDI import on files written by hand (not by the exporter): running status, velocity-0 note-offs, meta events,
 * several tracks, and a pedal held across a chord change */
static void test_import_files(void) {
    static const struct { const char *path; int want; const char *names[2]; } T[] = {
        {"tests/midi/A_triads.mid", 2, {"Am", "F"}},
        {"tests/midi/B_pedal.mid", 2, {"G#m7/F#", "A/F#"}},  /* the pedal is struck with the first chord */
        {"tests/midi/C_running.mid", 2, {"Am", "F"}},
        {"tests/midi/D_tracks.mid", 2, {"Am", "F"}},
    };
    /* corrupt lengths (found by fuzzing): must return without reading outside the file */
    ImportChord bad[16];
    char berr[128];
    int r1 = vc_midi_import("tests/midi/bad_track_length.mid", bad, 16, berr, sizeof(berr));
    int r2 = vc_midi_import("tests/midi/bad_header_length.mid", bad, 16, berr, sizeof(berr));
    CHECK(r1 <= 16 && r2 < 0, "corrupt lengths rejected or read safely (%d, %d)", r1, r2);
    for (unsigned f = 0; f < sizeof(T) / sizeof(T[0]); f++) {
        ImportChord ic[16];
        char err[128];
        int got = vc_midi_import(T[f].path, ic, 16, err, sizeof(err));
        CHECK(got == T[f].want, "%s: %d chords (%s)", T[f].path, got, got < 0 ? err : "ok");
        for (int i = 0; i < got && i < 2; i++) {
            char nm[64];
            vc_chord_name(&ic[i].spec, 0, nm, sizeof(nm));
            CHECK(!strcmp(nm, T[f].names[i]), "%s chord %d named %s (want %s)", T[f].path, i + 1, nm, T[f].names[i]);
        }
    }
}

/* Chord MIDI out (To MPC / Virtual port): every message uses the output channel, and every note-on has its
 * note-off. A track set to all inputs receives this stream, so nothing else may leak into it. */
static void test_midi_out_channel(void) {
    static Hit h[6000];
    Engine *e = v11_engine();
    e->out_channel = 15;
    e->midi_out = 1;
    e->lanes[0].order = ARP_UP; e->lanes[0].oct = 1; e->lanes[0].rate = RATE_8;
    en_push_midi(e, 0, 0x90, 36, 100);
    en_push_midi(e, 0, 0x90, 37, 100);
    static float L[512], R[512];
    int stray = 0, n = 0, on = 0, off = 0;
    for (int b = 0; b < 200; b++) {
        if (b == 60) { en_push_midi(e, 0, 0x80, 36, 0); en_push_midi(e, 0, 0x80, 37, 0); }
        en_process(e, L, R, 512);
        for (int i = 0; i < e->nout; i++) {
            int st = e->out[i].d[0] & 0xF0;
            if ((e->out[i].d[0] & 0x0F) != 15) stray++;
            if (st == 0x90 && e->out[i].d[2] > 0) on++;
            else if (st == 0x80 || st == 0x90) off++;
            else stray++; /* controllers or anything else would reach the other tracks */
        }
        e->nout = 0;
    }
    CHECK(stray == 0, "chord MIDI only uses channel 16 and note messages (%d stray)", stray);
    CHECK(on > 0 && on == off, "every chord note-on has its note-off (%d on, %d off)", on, off);
    (void)h; (void)n;
}

/* ---------- riffs, shift keys, presets, note-by-note chords (ported from Velvet Chords 1.1) ---------- */

static void test_presets(void) {
    CHECK(VC_NUM_PRESETS == 230, "230 factory presets (%d)", VC_NUM_PRESETS);
    CHECK(VC_NUM_PRESETS % VC_PRESET_VARS == 0, "presets split evenly into genres of %d", VC_PRESET_VARS);
    for (int i = 0; i < VC_NUM_PRESETS; i++) {
        const VcPreset *p = &VC_PRESETS[i];
        for (int j = 0; j < i; j++) CHECK(strcmp(p->name, VC_PRESETS[j].name) != 0, "duplicate preset name %s", p->name);
        CHECK(strlen(p->name) <= 23 && p->name[0] && p->genre[0], "name and genre (%s)", p->name);
        CHECK(p->sound >= 0 && p->sound < SY_SILENT, "%s: sound", p->name); /* any playable sound, not Silent */
        CHECK(p->voicing >= 0 && p->voicing < VC_VOICE_COUNT && p->inversion >= 0 && p->inversion <= 3, "%s: voicing", p->name);
        CHECK(p->octave >= -2 && p->octave <= 2 && p->smart_oct >= -2 && p->smart_oct <= 2, "%s: octave", p->name);
        CHECK(p->voice_lead == 0 || p->voice_lead == 1, "%s: voice lead", p->name);
        CHECK(p->strum_dir >= 0 && p->strum_dir < STRUM_COUNT && p->strum_ms >= 0.f && p->strum_ms <= 150.f, "%s: strum", p->name);
        CHECK(p->key >= 0 && p->key < 12 && p->scale >= 0 && p->scale < VC_NUM_SCALES, "%s: key and scale", p->name);
        CHECK(p->arp >= 0 && p->arp < ARP_COUNT && p->arp_rate >= 0 && p->arp_rate < RATE_COUNT, "%s: arp", p->name);
        CHECK(!(p->pattern && p->arp), "%s: a pattern preset leaves the arpeggio off (the pattern plays instead)", p->name);
        CHECK(p->arp_oct >= 1 && p->arp_oct <= 4, "%s: arp octaves", p->name);
        CHECK(p->vel >= 0 && p->vel < VEL_COUNT, "%s: velocity", p->name);
        CHECK(p->pattern >= 0 && p->pattern <= EN_NUM_PATTERNS && p->pat_rate >= 0 && p->pat_rate < PRATE_COUNT, "%s: pattern", p->name);
        CHECK(p->smart >= 0 && p->smart < SMART_COUNT && p->smart_mode >= 0 && p->smart_mode < SMODE_COUNT, "%s: smart", p->name);
        CHECK(p->melody >= 0 && p->melody <= VC_RIFFS, "%s: riff", p->name);
        CHECK(p->tone >= 0.f && p->tone <= 1.f && p->release >= 0.f && p->release <= 1.f && p->volume >= 0.f && p->volume <= 1.f, "%s: levels", p->name);
        CHECK(p->swing >= 0.f && p->swing <= 1.f && p->gate >= 0.1f && p->gate <= 1.f, "%s: swing and gate", p->name);
    }
    for (int s = 0; s < VC_RIFFS; s++) {
        int steps = 0;
        for (int k = 0; k < EN_STEPS; k++) {
            int x = EN_RIFF_DEFAULTS[s][k];
            if (x != RIFF_REST) { steps++; CHECK(x >= RIFF_MIN && x <= RIFF_MAX, "riff %d offset %d", s, x); }
        }
        CHECK(steps >= 2, "riff %d has notes (%d)", s, steps);
    }
}

static void test_shift_keys(void) {
    CHECK(en_shift_action(25) == SHIFT_KEY_DOWN && en_shift_action(27) == SHIFT_KEY_UP, "C# and D# move the key");
    CHECK(en_shift_action(30) == SHIFT_SCALE_PREV && en_shift_action(32) == SHIFT_SCALE_NEXT, "F# and G# move the scale");
    CHECK(en_shift_action(34) == SHIFT_INVERSION, "A# moves the inversion");
    CHECK(en_shift_action(24) < 0 && en_shift_action(26) < 0 && en_shift_action(36) < 0 && en_shift_action(35) < 0, "white keys and pads are not shift keys");

    Engine *e = v11_engine();
    en_push_midi(e, 0, 0x90, 25, 100);
    en_push_midi(e, 0, 0x90, 27, 100);
    en_push_midi(e, 0, 0x90, 30, 100);
    en_push_midi(e, 0, 0x90, 30, 100);
    static float L[512], R[512];
    en_process(e, L, R, 512);
    CHECK(e->shift_pending[SHIFT_KEY_DOWN] == 1 && e->shift_pending[SHIFT_KEY_UP] == 1, "key shifts queued");
    CHECK(e->shift_pending[SHIFT_SCALE_PREV] == 2, "two scale presses queued (%d)", e->shift_pending[SHIFT_SCALE_PREV]);
    CHECK(e->last_pad == -1 && e->nout == 0, "shift keys do not play pads");
    e->shift_keys = 0;
    en_push_midi(e, 0, 0x90, 25, 100);
    en_process(e, L, R, 512);
    CHECK(e->shift_pending[SHIFT_KEY_DOWN] == 1, "with shift keys off the note is not a shift");
}

static void test_riff(void) {
    static Hit h[4000];
    Engine *e = v11_engine();
    e->pattern_on = 1;
    e->riff = 1; /* Root and fifth */
    e->pat_rate = PRATE_16;
    e->gate = 1.0f;
    en_push_midi(e, 0, 0x90, 36, 100);
    int n = collect(e, 200, h, 4000);
    int base = (e->voicings[0].notes[0] / 12 + 1) * 12;
    int want[4] = {base, base + 7, base, base + 7}; /* steps 0, 4, 8, 12 of the 16-step riff */
    int got = 0;
    for (int i = 0; i < n && got < 4; i++) if (h[i].on) {
        CHECK(h[i].note == want[got], "riff note %d: %d want %d", got, h[i].note, want[got]);
        got++;
    }
    CHECK(got == 4, "riff plays the slot (%d notes)", got);

    /* transposed with the chord: a D chord moves the same riff up two semitones */
    e = v11_engine();
    custom_chord(e, 0, "Dm", 2);
    e->pattern_on = 1; e->riff = 1; e->pat_rate = PRATE_16; e->gate = 1.0f;
    en_push_midi(e, 0, 0x90, 36, 100);
    n = collect(e, 60, h, 4000);
    base = (e->voicings[0].notes[0] / 12 + 1) * 12;
    int first = -1;
    for (int i = 0; i < n && first < 0; i++) if (h[i].on) first = h[i].note;
    CHECK(first == base + 2, "riff follows the chord root (%d want %d)", first, base + 2);

    /* a rest silences the step */
    e = v11_engine();
    e->pattern_on = 1; e->riff = 1; e->pat_rate = PRATE_16; e->gate = 1.0f;
    e->rif[0][0] = RIFF_REST;
    en_push_midi(e, 0, 0x90, 36, 100);
    n = collect(e, 2, h, 4000);
    int ons0 = 0;
    for (int i = 0; i < n; i++) ons0 += h[i].on;
    CHECK(ons0 == 0, "a rest plays nothing (%d)", ons0);

    /* back to the chord: after a riff the held chord sounds whole again */
    e = v11_engine();
    e->pattern_on = 1; e->riff = 1; e->pat_rate = PRATE_16; e->gate = 1.0f;
    en_push_midi(e, 0, 0x90, 36, 100);
    collect(e, 2, h, 4000);
    e->riff = 0;
    memcpy(e->pat, "XXXXXXXXXXXXXXXX", EN_STEPS); /* every step is the full chord */
    n = collect(e, 40, h, 4000);
    int at_first_chord = 0; /* the first chord step after the riff: all the held chord's notes at one time */
    int64_t t_first = -1;
    for (int i = 0; i < n; i++) if (h[i].on && t_first < 0) t_first = h[i].t;
    for (int i = 0; i < n; i++) if (h[i].on && h[i].t == t_first) at_first_chord++;
    CHECK(at_first_chord == e->voicings[0].n, "chord restored after a riff (%d of %d notes)", at_first_chord, e->voicings[0].n);
}

static void test_note_by_note(void) {
    static PadChord out[VC_PADS];
    CustomPad custom[VC_PADS];
    LayoutParams lp;
    memset(custom, 0, sizeof(custom));
    memset(&lp, 0, sizeof(lp));
    lp.mode = VC_MODE_CUSTOM;
    lp.voicing = VC_VOICE_BASS_UPPER;

    /* C, E and B: no named chord has exactly these pitch classes, so the pad keeps its own notes */
    custom[0].valid = 1; custom[0].root = 0; custom[0].bass = 255; custom[0].nnotes = 3;
    custom[0].notes[0] = 0; custom[0].notes[1] = 4; custom[0].notes[2] = 11;
    vc_build_pads(&lp, custom, out);
    CHECK(out[0].valid && out[0].spec.quality < 0 && out[0].spec.ncustom == 3, "own notes (q %d n %d)", out[0].spec.quality, out[0].spec.ncustom);
    CHECK(out[0].spec.custom[1] == 4 && out[0].spec.custom[2] == 11, "own note intervals");
    CHECK(out[0].name[0] == 'C' && strchr(out[0].name, '(') != 0, "named from its notes: %s", out[0].name);

    /* C, E, G with an added 9th is C6/9 or Cadd9 in the table, so the pad takes that name */
    custom[1].valid = 1; custom[1].root = 0; custom[1].bass = 255; custom[1].nnotes = 3;
    custom[1].notes[0] = 0; custom[1].notes[1] = 4; custom[1].notes[2] = 7;
    vc_build_pads(&lp, custom, out);
    ChordSpec major;
    CHECK(vc_chord_parse("C", &major), "parse C");
    CHECK(out[1].spec.quality == major.quality, "a major triad keeps the named chord");

    /* a note list of one (only the root) is not a note-by-note chord */
    custom[2].valid = 1; custom[2].root = 0; custom[2].bass = 255; custom[2].nnotes = 1;
    custom[2].notes[0] = 0;
    vc_build_pads(&lp, custom, out);
    CHECK(out[2].spec.quality < 0 ? out[2].spec.ncustom == 0 : 1, "root-only list is ignored");
}

/* stress: random pads, sustain, pattern and Repeat changes, arp lanes, all-notes-off and chord changes.
 * Every note-on sent to the host must be matched by one note-off, never more, and nothing may hang at the end. */
static unsigned st_rnd(unsigned *s) { *s = *s * 1103515245u + 12345u; return (*s >> 16) & 0x7fff; }

static void test_stuck_notes(void) {
    static Hit h[4000];
    static const char STEPS[] = ".xXbuU-";
    int trials = 40, bad_range = 0, unbalanced = 0;
    for (int t = 0; t < trials; t++) {
        unsigned seed = 1000u + (unsigned)t;
        Engine *e = v11_engine();
        e->out_channel = 0;
        int bal[128] = {0};
        for (int step = 0; step < 300; step++) {
            unsigned op = st_rnd(&seed) % 16;
            if (op < 4) en_push_midi(e, 0, 0x90, (unsigned char)(36 + st_rnd(&seed) % 16), 100);
            else if (op < 6) en_push_midi(e, 0, 0x80, (unsigned char)(36 + st_rnd(&seed) % 16), 0);
            else if (op == 6) en_push_midi(e, 0, 0xB0, 64, st_rnd(&seed) % 2 ? 127 : 0);
            else if (op == 7) e->pattern_on = (int)(st_rnd(&seed) % 2);
            else if (op == 8) { for (int k = 0; k < EN_STEPS; k++) e->pat[k] = STEPS[st_rnd(&seed) % 7]; }
            else if (op == 9) e->repeat = st_rnd(&seed) % 2 ? REP_8 : REP_OFF;
            else if (op == 10) { int l = st_rnd(&seed) % EN_ARP_LANES; e->lanes[l].order = st_rnd(&seed) % 2 ? ARP_UP : ARP_OFF; e->lanes[l].oct = 1 + (int)(st_rnd(&seed) % 2); }
            else if (op == 11) e->hold = (int)(st_rnd(&seed) % 2);
            else if (op == 12) en_push_midi(e, 0, 0xB0, 123, 0);
            else if (op == 13) custom_chord(e, (int)(st_rnd(&seed) % 16), st_rnd(&seed) % 2 ? "Am7" : "F", 0);
            static float L[512], R[512];
            int n = 0;
            for (int b = 0; b < 2; b++) {
                en_process(e, L, R, 512);
                for (int i = 0; i < e->nout && n < 4000; i++) {
                    int on = (e->out[i].d[0] & 0xF0) == 0x90 && e->out[i].d[2] > 0;
                    int note = e->out[i].d[1] & 127;
                    if ((e->out[i].d[0] & 0xF0) == 0x90 || (e->out[i].d[0] & 0xF0) == 0x80) {
                        bal[note] += on ? 1 : -1;
                        if (bal[note] < 0 || bal[note] > 1) bad_range++;
                    }
                }
                e->nout = 0;
            }
        }
        /* let go of everything the way a host does (Hold off too, or the latched chord keeps sounding), then drain */
        e->hold = 0;
        for (int k = 0; k < 128; k++) en_push_midi(e, 0, 0x80, (unsigned char)k, 0);
        en_push_midi(e, 0, 0xB0, 64, 0);
        static float L[512], R[512];
        for (int b = 0; b < 400; b++) {
            en_process(e, L, R, 512);
            for (int i = 0; i < e->nout; i++) {
                int on = (e->out[i].d[0] & 0xF0) == 0x90 && e->out[i].d[2] > 0;
                int note = e->out[i].d[1] & 127;
                if ((e->out[i].d[0] & 0xF0) == 0x90 || (e->out[i].d[0] & 0xF0) == 0x80) {
                    bal[note] += on ? 1 : -1;
                    if (bal[note] < 0 || bal[note] > 1) bad_range++;
                }
            }
            e->nout = 0;
        }
        for (int k = 0; k < 128; k++) if (bal[k] != 0) { unbalanced++; break; }
    }
    CHECK(bad_range == 0, "every note-on has one note-off, never doubled or early (%d)", bad_range);
    CHECK(unbalanced == 0, "no note hangs after release, all %d runs (%d unbalanced)", trials, unbalanced);
}

int main(void) {
    printf("chord qualities...\n"); test_qualities_against_spelled();
    printf("parser...\n"); test_parse();
    printf("voicings...\n"); test_voicings();
    printf("song data...\n"); test_songs();
    printf("layouts...\n"); test_layouts();
    printf("note handling...\n"); test_note_handling();
    printf("pattern generator...\n"); test_patterns();
    printf("hold, repeat, melody, import...\n"); test_tools();
    printf("instruments...\n"); test_instruments();
    printf("MIDI import, hand-made files...\n"); test_import_files();
    printf("chord MIDI out stays on its channel and balances...\n"); test_midi_out_channel();
    printf("presets...\n"); test_presets();
    printf("shift keys...\n"); test_shift_keys();
    printf("riffs...\n"); test_riff();
    printf("note-by-note chords...\n"); test_note_by_note();
    printf("multi arp orders...\n"); test_arp_orders();
    printf("multi arp lanes...\n"); test_multi_arp();
    printf("stuck notes under random play...\n"); test_stuck_notes();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
