#ifndef VC_FX_H
#define VC_FX_H

/* Stereo room reverb (a Freeverb-style network of damped combs and allpass filters).
 * All buffers are inside the struct, so processing never allocates. */
#define FX_COMBS 8
#define FX_APS 4
#define FX_CMAX 2048   /* longest comb at 48 kHz, plus the stereo spread */
#define FX_AMAX 1024

typedef struct {
    float buf[FX_CMAX];
    int len, idx;
    float store;
} FxComb;

typedef struct {
    float buf[FX_AMAX];
    int len, idx;
} FxAllpass;

typedef struct {
    FxComb comb[2][FX_COMBS];
    FxAllpass ap[2][FX_APS];
    float feedback, damp;
} VcReverb;

void fx_reverb_init(VcReverb *r, float sr);
/* amount 0..1: 0 leaves the audio exactly as it was; the wet level rises with amount */
void fx_reverb(VcReverb *r, float *L, float *R, int n, float amount);

#endif
