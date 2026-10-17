/* Prints "index<TAB>name<TAB>display" for every plugin parameter, loading the .so like a host.
 * Used by tools/make_skin.py so the skin binds to parameters by name, never by hand-typed numbers. */
#include "../src/vst2_abi.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
static intptr_t cb(AEffect *fx, int32_t op, int32_t i, intptr_t v, void *p, float o) { (void)fx; (void)i; (void)v; (void)p; (void)o; return op == audioMasterVersion ? 2400 : 0; }
int main(int argc, char **argv) {
    void *h = dlopen(argc > 1 ? argv[1] : "./velvetchords.so", RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    AEffect *(*m)(audioMasterCallback) = (AEffect * (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    AEffect *fx = m(cb);
    for (int a = 2; a < argc; a++) { /* optional "index=value" settings applied first (used for previews) */
        int idx; float val;
        if (sscanf(argv[a], "%d=%f", &idx, &val) == 2) fx->setParameter(fx, idx, val);
    }
    { /* run a few blocks so screen taps (auditions) are processed */
        static float L[512], R[512];
        float *o[2] = {L, R};
        fx->dispatcher(fx, effSetSampleRate, 0, 0, 0, 48000.f);
        fx->dispatcher(fx, effSetBlockSize, 0, 512, 0, 0);
        for (int b = 0; b < 4; b++) fx->processReplacing(fx, 0, o, 512);
    }
    for (int i = 0; i < fx->numParams; i++) {
        char n[256] = {0}, d[256] = {0};
        fx->dispatcher(fx, effGetParamName, i, 0, n, 0);
        fx->dispatcher(fx, effGetParamDisplay, i, 0, d, 0);
        printf("%d\t%s\t%s\t%.6f\n", i, n, d, fx->getParameter(fx, i));
    }
    fx->dispatcher(fx, effClose, 0, 0, 0, 0);
    return 0;
}
