/* Small math helpers that avoid libm entirely. glibc 2.27 re-versioned expf/powf/logf/exp2f,
 * so a plugin built with a modern toolchain could fail to load on older MPC OS images.
 * Everything here only needs + - * /. */
#ifndef VC_MATH_H
#define VC_MATH_H

#define VC_SIN_N 4096
extern float vc_sin_tab[VC_SIN_N + 1];
void vc_math_init(void);

double vc_exp(double x);                     /* e^x */
static inline double vc_exp2(double x) { return vc_exp(x * 0.69314718055994530942); }

/* sine of a phase given in cycles (any real value) */
static inline float vc_sinc(float ph) {
    ph -= (float)(int)ph;
    if (ph < 0.f) ph += 1.f;
    float f = ph * (float)VC_SIN_N;
    int i = (int)f;
    float t = f - (float)i;
    return vc_sin_tab[i] + (vc_sin_tab[i + 1] - vc_sin_tab[i]) * t;
}

static inline float vc_wrap1(float ph) {
    if (ph >= 1.f) ph -= (float)(int)ph;
    return ph;
}

/* per-sample multiplier that decays by 1/e over tau seconds */
static inline float vc_decay_mul(double tau, double sr) {
    if (tau < 1e-5) tau = 1e-5;
    return (float)vc_exp(-1.0 / (tau * sr));
}

static inline float vc_softclip(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

static inline float vc_midi_hz(int note) { return (float)(440.0 * vc_exp2((note - 69) / 12.0)); }

#endif
