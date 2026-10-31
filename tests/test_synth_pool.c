/* The synth's voice pool: when every voice is still held, a new note is dropped and the held notes keep sounding. */
#include "synth.h"
#include "vcmath.h"
#include <stdio.h>

static int checks, failures;
#define CHECK(cond, what) do { checks++; if (!(cond)) { failures++; printf("  FAIL: %s\n", what); } } while (0)

int main(void) {
    vc_math_init();
    printf("synth voice pool\n");
    static Synth s;
    sy_init(&s, 48000.f);
    /* the melody note first, then fill the pool with held notes */
    sy_note_on(&s, 48, 0.8f, 1, SY_GROUP_MELODY);
    for (int n = 0; n < SY_VOICES - 1; n++) sy_note_on(&s, 49 + n, 0.8f, 1, SY_GROUP_MELODY);
    CHECK(sy_active(&s) == SY_VOICES, "the pool is full of held notes");

    /* more held notes than there are voices: the extra notes are dropped, nothing held is cut */
    for (int n = 0; n < 4; n++) sy_note_on(&s, 100 + n, 0.8f, 2, SY_GROUP_MELODY);
    CHECK(sy_active(&s) == SY_VOICES, "notes beyond the pool don't take a held voice (still 48 sounding)");

    /* release everything except the melody, and let the released voices finish */
    for (int n = 0; n < SY_VOICES - 1; n++) sy_release(&s, 49 + n, 1);
    sy_release(&s, 100, 2); sy_release(&s, 101, 2); sy_release(&s, 102, 2); sy_release(&s, 103, 2);
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

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
