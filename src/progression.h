#ifndef VC_PROGRESSION_H
#define VC_PROGRESSION_H
/* Progressions written as Roman numerals, so they transpose with the key and keep their harmonic relationships.
 * A step is [b|#] numeral [suffix]: the numeral's case gives the default triad (I = major, ii = minor), the suffix
 * names any chord quality from chords.c ("7", "maj7", "m7b5", "dim7", "sus4", ...), and "°" or "dim" mean diminished. */
#include "chords.h"

#define VC_PROG_MAX 16

typedef struct {
    const char *name;
    int n;
    const char *steps[VC_PROG_MAX];
} Progression;

extern const Progression VC_PROGRESSIONS[];
extern const int VC_NUM_PROGRESSIONS;

/* The harmonic context the arpeggiator, melody and smart pads can follow: key, scale and where the progression is. */
typedef struct {
    int key, scale;   /* key 0..11 (C = 0), scale index into VC_SCALES */
    const Progression *prog;
    int pos;          /* current step, 0..prog->n-1 */
} HarmonicContext;

int vc_roman_chord(int key, int scale, const char *step, ChordSpec *out); /* 1 on success, 0 if not understood */
int vc_prog_chord(const Progression *p, int key, int scale, int i, ChordSpec *out); /* step i, wrapping */
void vc_ctx_set(HarmonicContext *h, int key, int scale, const Progression *p);
void vc_ctx_advance(HarmonicContext *h);          /* next step, wrapping at the end of the progression */
int vc_ctx_chord(const HarmonicContext *h, ChordSpec *out);

#endif
