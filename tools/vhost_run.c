/* vhost_run: loads VST2 plugins with the Velvet Chords host, reports what each is, and for each instrument plays one
 * note (A4) for a second and reports the level it makes.
 * Usage: vhost_run <folder>            (every .so in the folder)
 *        vhost_run <file.so> [...]     (the named files; use this under qemu, whose folder listing is empty) */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "vhost.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s folder | file.so ...\n", argv[0]); return 2; }
    static char paths[64][VH_PATH_LEN];
    int n = 0;
    size_t alen = strlen(argv[1]);
    if (alen > 3 && strcmp(argv[1] + alen - 3, ".so") == 0) {
        for (int a = 1; a < argc && n < 64; a++) snprintf(paths[n++], VH_PATH_LEN, "%s", argv[a]);
    } else {
        n = vh_list(argv[1], paths, 64);
        if (n < 0) { printf("cannot read %s\n", argv[1]); return 1; }
        printf("%d .so files in %s\n", n, argv[1]);
    }
    for (int i = 0; i < n; i++) {
        VhInfo info;
        const char *base = strrchr(paths[i], '/');
        printf("- %s\n", base ? base + 1 : paths[i]);
        if (vh_probe(paths[i], &info) != 0) { printf("  not a usable VST2 plugin here\n"); continue; }
        printf("  name '%s', uid %08x, %s, %d in / %d out, %d params\n", info.name, (unsigned)info.uid,
               info.is_instrument ? "instrument" : "effect", info.inputs, info.outputs, info.nparams);
        if (!info.is_instrument) continue;
        VHost *h = vh_open(paths[i], 44100.0, 512);
        if (!h) { printf("  could not open\n"); continue; }
        static float L[44100], R[44100];
        VhMidi on = {0, 0x90, 69, 100}, off = {44100 - 1, 0x80, 69, 0};
        VhMidi ev[2] = {on, off};
        vh_process(h, ev, 2, L, R, 44100);
        float peak = 0.f; int finite = 1;
        for (int k = 0; k < 44100; k++) {
            if (!(L[k] == L[k]) || !(R[k] == R[k])) finite = 0;
            float a = fabsf(L[k]); if (a > peak) peak = a;
        }
        printf("  A4 for 1 s: peak %.4f%s\n", peak, finite ? "" : " (NaN in output!)");
        vh_close(h);
    }
    return 0;
}
