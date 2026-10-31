/* The synth's voice pool: when every voice is still held, a new note is dropped and the held notes keep sounding. */
#include "synth.h"
#include "vcmath.h"
#include <stdio.h>
#include <time.h>

static int checks, failures;
#define CHECK(cond, what) do { checks++; if (!(cond)) { failures++; printf("  FAIL: %s\n", what); } } while (0)

int main(void) {
    vc_math_init();
    printf("synth voice pool\n");
    static Synth s;
    sy_init(&s, 48000.f);
    /* the melody note first (owner 1), then fill the pool with held notes (owner 2). Pitches repeat once the pool is
     * bigger than the 128 MIDI notes; each repeat is still its own voice. */
    sy_note_on(&s, 48, 0.8f, 1, SY_GROUP_MELODY);
    for (int n = 0; n < SY_VOICES - 1; n++) sy_note_on(&s, 36 + n % 60, 0.8f, 2, SY_GROUP_MELODY);
    CHECK(sy_active(&s) == SY_VOICES, "the pool is full of held notes");

    /* more held notes than there are voices: the extra notes are dropped, nothing held is cut */
    for (int n = 0; n < 4; n++) sy_note_on(&s, 100 + n, 0.8f, 3, SY_GROUP_MELODY);
    CHECK(sy_active(&s) == SY_VOICES, "notes beyond the pool don't take a held voice (still every voice sounding)");

    /* release everything except the melody, and let the released voices finish */
    sy_release(&s, -1, 2);
    sy_release(&s, -1, 3);
    float L[256], R[256];
    for (int b = 0; b < 2000 && sy_active(&s) > 1; b++) sy_render(&s, L, R, 256);
    CHECK(sy_active(&s) == 1, "after the others end, the melody note is still sounding");

    /* drums and melody share the pool: the drums get at most SY_DRUM_MAX voices, so the melody keeps the rest */
    static Synth d;
    sy_init(&d, 48000.f);
    for (int n = 0; n < 32; n++) sy_note_on(&d, 40 + n, 0.8f, 3, SY_GROUP_MELODY);
    for (int n = 0; n < 40; n++) sy_note_on(&d, 60 + n, 0.8f, 4, SY_GROUP_DRUM);
    int melody = 0, drums = 0;
    for (int i = 0; i < SY_VOICES; i++) {
        if (!d.v[i].active || d.v[i].released) continue;
        if (d.v[i].group == SY_GROUP_MELODY) melody++; else drums++;
    }
    CHECK(melody == 32, "all 32 melody notes still sound after 40 drum hits");
    CHECK(drums == SY_DRUM_MAX, "the drums hold at most their share (16 voices)");

    /* the cost of the full pool: every voice held, rendered in 256-frame blocks at 48 kHz (5333 us a block) */
    static Synth t;
    sy_init(&t, 48000.f);
    for (int n = 0; n < SY_VOICES; n++) sy_note_on(&t, 36 + n % 60, 0.8f, 5, SY_GROUP_MELODY);
    static float TL[256], TR[256];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < 400; b++) sy_render(&t, TL, TR, 256);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / 400.0;
    printf("  full pool of %d voices: %.1f us a block on this machine (budget 5333 us)\n", SY_VOICES, us);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
