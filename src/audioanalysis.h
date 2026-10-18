#ifndef VC_AUDIOANALYSIS_H
#define VC_AUDIOANALYSIS_H
/* Offline chord estimation from a WAV file: STFT chroma, bass-note tracking, change detection by chroma direction,
 * and chord-template matching against the chord table in chords.c. Works on the file, never on the live audio path,
 * and uses no maths library (the plugin must not link one). Results are estimates: each one carries a confidence and
 * the runner-up, and a chord is marked ambiguous when the two are close. */
#include "chords.h"

#define VA_MAX_SEG 16       /* chords kept (a user progression holds 16 steps) */
#define VA_MAX_SECONDS 120  /* longer files are analysed up to this point */

typedef struct {
    double start, end;      /* seconds from the start of the file */
    int root, quality;      /* best chord: pitch class 0..11 and an index into VC_QUALITIES */
    int bass;               /* pitch class of the strongest low note in the segment, -1 if none */
    float conf;             /* 0..1: how closely the chroma matches the chord's notes (cosine squared) */
    float margin;           /* best minus runner-up, 0..1: small means ambiguous */
    int alt_root, alt_quality; /* the runner-up chord (a different chord) */
} VaSegment;

typedef struct {
    int nseg;
    VaSegment seg[VA_MAX_SEG];
    int key, minor;         /* estimated tonic pitch class and mode (1 = minor) */
    float key_conf;         /* 0..1 fit of the key profile */
    double seconds;         /* how much audio was analysed */
    int truncated;          /* 1 if the file was longer than VA_MAX_SECONDS */
} VaResult;

/* Read a 16-bit PCM or 32-bit float WAV as mono (channels are averaged). Reads at most VA_MAX_SECONDS. The caller frees
 * *mono. Returns 0 on success; otherwise a negative value and a message in msg. */
int va_read_wav(const char *path, float **mono, int *n, int *rate, char *msg, int msglen);

/* Analyse mono audio. Returns 0 on success (nseg may be 0 for silence), otherwise negative with a message. */
int va_analyze(const float *mono, int n, int rate, VaResult *out, char *msg, int msglen);

#endif
