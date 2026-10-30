/* Host tests: loads a VST2 instrument the way the plugin will, and checks what comes out of it.
 * Usage: vhost_test <test instrument .so> <Velvet Chords .so> <folder with both> [--no-scan]
 * --no-scan skips the folder-listing checks. Use it under qemu-arm: the emulator returns an empty folder listing,
 * so those checks can't be trusted there (they pass natively, and must be checked on the device). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vhost.h"

static int failures = 0;
static void check(int ok, const char *what) {
    printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static void note(VhMidi *m, int *n, int frame, unsigned char status, unsigned char d1, unsigned char d2) {
    m[*n].frame = frame; m[*n].status = status; m[*n].data1 = d1; m[*n].data2 = d2; ++*n;
}

static int zero_crossings(const float *x, int n) {
    int z = 0;
    for (int i = 1; i < n; ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++z;
    return z;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s instrument.so velvetchords.so folder [--no-scan]\n", argv[0]); return 2; }
    const char *synth = argv[1], *own = argv[2], *dir = argv[3];
    int scan = !(argc > 4 && strcmp(argv[4], "--no-scan") == 0);

    printf("scan\n");
    char paths[16][VH_PATH_LEN];
    if (scan) {
        int found = vh_list(dir, paths, 16);
        check(found >= 2, "the folder lists both .so files without loading them");
        check(vh_list("/no/such/folder", paths, 16) == -1, "an unreadable folder is reported, not guessed");
    } else {
        printf("  skip: folder listing not checked here (emulator); run on the device or natively\n");
    }

    printf("probe\n");
    VhInfo info;
    check(vh_probe(synth, &info) == 0, "the test instrument is recognised as a VST2 plugin");
    check(strcmp(info.name, "Fake Sine") == 0, "its name is read from the plugin");
    check(info.is_instrument == 1, "it is reported as an instrument");
    check(info.nparams == 1, "its parameter count is read");
    check(info.uid == VC_FOURCC('F', 'k', 'S', 'n'), "its unique ID is read");
    check(vh_probe(own, &info) == -1, "Velvet Chords is refused: it can't host itself");
    check(vh_probe("/no/such/plugin.so", &info) == -1, "a missing file is refused");

    printf("open and play\n");
    VHost *h = vh_open(synth, 44100.0, 256);
    check(h != NULL, "the instrument opens at 44.1 kHz with 256-frame blocks");
    if (!h) { printf("\nHOST TESTS FAILED\n"); return 1; }
    check(vh_info(h)->is_instrument == 1, "the open instance reports the same kind");
    check(vh_open(own, 44100.0, 256) == NULL, "opening Velvet Chords itself returns NULL");

    /* one note: A4 on at frame 100, off at frame 1400, in a 2000-frame render */
    enum { N = 2000 };
    static float L[N], R[N];
    VhMidi ev[4]; int ne = 0;
    note(ev, &ne, 100, 0x90, 69, 100);
    note(ev, &ne, 1400, 0x80, 69, 0);
    check(vh_process(h, ev, ne, L, R, N) == 0, "a 2000-frame block renders");

    int silent_before = 1, sounded = 0, silent_after = 1;
    for (int i = 0; i < 100; ++i) if (L[i] != 0.f) silent_before = 0;
    for (int i = 100; i < 1400; ++i) if (L[i] != 0.f) sounded = 1;
    for (int i = 1400; i < N; ++i) if (L[i] != 0.f) silent_after = 0;
    check(silent_before, "silent before the note-on");
    check(sounded, "the note sounds between note-on and note-off");
    check(silent_after, "silent after the note-off");

    float peak = 0.f; double sum = 0;
    for (int i = 100; i < 1400; ++i) { float a = fabsf(L[i]); if (a > peak) peak = a; sum += (double)L[i] * L[i]; }
    check(fabsf(peak - 0.2f) < 0.01f, "peak level matches the instrument's 0.2 amplitude");
    int zc = zero_crossings(L + 100, 1300);
    double expected = 440.0 * 2.0 * 1300.0 / 44100.0;     /* crossings of a 440 Hz sine over 1300 frames */
    check(fabs(zc - expected) < 2.0, "pitch is A4 (440 Hz) when checked by zero crossings");
    int same = 1;
    for (int i = 0; i < N; ++i) if (L[i] != R[i]) same = 0;
    check(same, "left and right are the same for this mono-style instrument");

    printf("blocks join cleanly\n");
    /* a note that starts before a block boundary and runs across several 256-frame blocks: no click at the joins */
    VhMidi ev2[2]; int ne2 = 0;
    note(ev2, &ne2, 0, 0x90, 60, 90);
    static float A[3000];
    vh_process(h, ev2, ne2, A, A, 3000);
    float worst = 0.f;
    for (int i = 1; i < 3000; ++i) { float d = fabsf(A[i] - A[i - 1]); if (d > worst) worst = d; }
    check(worst < 0.03f, "no step larger than the sine allows, so the 256-frame blocks join without a click");

    printf("parameters\n");
    char name[64];
    vh_param_name(h, 0, name, sizeof name);
    check(strcmp(name, "Gain") == 0, "parameter 0 is named Gain");
    vh_set_param(h, 0, 0.5f);
    check(fabsf(vh_get_param(h, 0) - 0.5f) < 1e-6f, "a set parameter reads back");
    static float G[1000];
    VhMidi ev3[1]; int ne3 = 0;
    note(ev3, &ne3, 0, 0x90, 69, 100);
    vh_process(h, ev3, ne3, G, G, 1000);
    float halfpeak = 0.f;
    for (int i = 0; i < 1000; ++i) { float a = fabsf(G[i]); if (a > halfpeak) halfpeak = a; }
    check(fabsf(halfpeak - 0.1f) < 0.01f, "gain 0.5 halves the peak");
    vh_set_param(h, 0, 1.f);

    printf("close\n");
    check(vh_process(NULL, NULL, 0, G, G, 10) == -1, "a closed or missing instance is refused, not used");
    vh_close(h);
    check(1, "closes without a crash");

    printf("\n%s\n", failures ? "HOST TESTS FAILED" : "host tests passed");
    return failures ? 1 : 0;
}
