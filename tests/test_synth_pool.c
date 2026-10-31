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
    sy_note_on(&s, 48, 0.8f, 1);
    for (int n = 0; n < SY_VOICES - 1; n++) sy_note_on(&s, 49 + n, 0.8f, 1);
    CHECK(sy_active(&s) == SY_VOICES, "the pool is full of held notes");

    /* more held notes than there are voices: the extra notes are dropped, nothing held is cut */
    for (int n = 0; n < 4; n++) sy_note_on(&s, 100 + n, 0.8f, 2);
    CHECK(sy_active(&s) == SY_VOICES, "notes beyond the pool don't take a held voice (still 48 sounding)");

    /* release everything except the melody, and let the released voices finish */
    for (int n = 0; n < SY_VOICES - 1; n++) sy_release(&s, 49 + n, 1);
    sy_release(&s, 100, 2); sy_release(&s, 101, 2); sy_release(&s, 102, 2); sy_release(&s, 103, 2);
    float L[256], R[256];
    for (int b = 0; b < 2000 && sy_active(&s) > 1; b++) sy_render(&s, L, R, 256);
    CHECK(sy_active(&s) == 1, "after the others end, the melody note is still sounding");

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
