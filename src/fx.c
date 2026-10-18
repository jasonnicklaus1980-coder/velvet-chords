#include "fx.h"
#include <string.h>

/* Freeverb's delay lengths, in samples at 44.1 kHz */
static const int COMB_LEN[FX_COMBS] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int AP_LEN[FX_APS] = {556, 441, 341, 225};
#define STEREO_SPREAD 23

static int scaled(int len44, float sr) {
    int n = (int)(len44 * sr / 44100.f + 0.5f);
    return n < 1 ? 1 : n;
}

void fx_reverb_init(VcReverb *r, float sr) {
    memset(r, 0, sizeof(*r));
    for (int c = 0; c < 2; c++) {
        int spread = c ? STEREO_SPREAD : 0;
        for (int i = 0; i < FX_COMBS; i++) {
            int len = scaled(COMB_LEN[i] + spread, sr);
            r->comb[c][i].len = len > FX_CMAX ? FX_CMAX : len;
        }
        for (int i = 0; i < FX_APS; i++) {
            int len = scaled(AP_LEN[i] + spread, sr);
            r->ap[c][i].len = len > FX_AMAX ? FX_AMAX : len;
        }
    }
    r->feedback = 0.84f;   /* a long, smooth tail */
    r->damp = 0.3f;        /* high frequencies die a little faster */
}

void fx_reverb(VcReverb *r, float *L, float *R, int n, float amount) {
    if (amount <= 0.f) return;
    float wet = amount * 0.35f, dry = 1.f - amount * 0.35f;
    for (int k = 0; k < n; k++) {
        float in = (L[k] + R[k]) * 0.015f;
        float out[2] = {0.f, 0.f};
        for (int c = 0; c < 2; c++) {
            for (int i = 0; i < FX_COMBS; i++) {
                FxComb *cb = &r->comb[c][i];
                float y = cb->buf[cb->idx];
                cb->store = y * (1.f - r->damp) + cb->store * r->damp;
                cb->buf[cb->idx] = in + cb->store * r->feedback;
                if (++cb->idx >= cb->len) cb->idx = 0;
                out[c] += y;
            }
            for (int i = 0; i < FX_APS; i++) {
                FxAllpass *ap = &r->ap[c][i];
                float bufout = ap->buf[ap->idx];
                float y = -out[c] + bufout;
                ap->buf[ap->idx] = out[c] + bufout * 0.5f;
                if (++ap->idx >= ap->len) ap->idx = 0;
                out[c] = y;
            }
        }
        L[k] = L[k] * dry + out[0] * wet;
        R[k] = R[k] * dry + out[1] * wet;
    }
}
