#include "vcmath.h"

float vc_sin_tab[VC_SIN_N + 1];
static int g_init = 0;

double vc_exp(double x) {
    const double LN2 = 0.69314718055994530942;
    if (x > 700.0) x = 700.0;
    if (x < -700.0) return 0.0;
    int n = (int)(x / LN2 + (x >= 0 ? 0.5 : -0.5));
    double r = x - n * LN2; /* |r| <= ~0.35 */
    double term = 1.0, sum = 1.0;
    for (int i = 1; i < 16; i++) { term *= r / i; sum += term; }
    while (n > 0) { sum *= 2.0; n--; }
    while (n < 0) { sum *= 0.5; n++; }
    return sum;
}

void vc_math_init(void) {
    if (g_init) return;
    /* exact-ish cos/sin of the step angle by Taylor series, then rotate */
    const double TWO_PI = 6.283185307179586476925;
    double a = TWO_PI / VC_SIN_N;
    double c = 1.0, s = 0.0, t = 1.0;
    for (int i = 1; i < 20; i++) {
        t *= a / i;
        switch (i & 3) { case 1: s += t; break; case 2: c -= t; break; case 3: s -= t; break; default: c += t; }
    }
    double x = 1.0, y = 0.0; /* cos, sin of current angle */
    for (int i = 0; i <= VC_SIN_N; i++) {
        vc_sin_tab[i] = (float)y;
        double nx = x * c - y * s, ny = x * s + y * c;
        x = nx; y = ny;
        if ((i & 255) == 255) { /* renormalise drift */
            double m = x * x + y * y; double inv = 1.5 - 0.5 * m; x *= inv; y *= inv;
        }
    }
    vc_sin_tab[0] = 0.f; vc_sin_tab[VC_SIN_N / 2] = 0.f; vc_sin_tab[VC_SIN_N] = 0.f;
    vc_sin_tab[VC_SIN_N / 4] = 1.f; vc_sin_tab[3 * VC_SIN_N / 4] = -1.f;
    g_init = 1;
}
