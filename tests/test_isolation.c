/* Two Velvet Chords instances playing at once, as two MPC tracks do: a melody on one and drums on the other.
 *
 * The melody instance must produce exactly the same audio whether or not the drum instance is rendering beside it, and
 * a note-off sent to one instance must not touch the other. The block times are reported against the audio deadline.
 * Usage: test_isolation <path to velvetchords.so> */
#include "vst2_abi.h"
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BLOCK 256
#define BLOCKS 240
#define RATE 48000.f

static int checks, failures;
#define CHECK(cond, what) do { checks++; if (!(cond)) { failures++; printf("  FAIL: %s\n", what); } } while (0)

static intptr_t host_cb(AEffect *e, int32_t op, int32_t idx, intptr_t val, void *ptr, float opt) {
    (void)e; (void)idx; (void)val; (void)ptr; (void)opt;
    switch (op) {
    case audioMasterVersion: return 2400;
    case 16: return (intptr_t)RATE;   /* audioMasterGetSampleRate */
    case 15: return BLOCK;            /* audioMasterGetBlockSize */
    default: return 0;
    }
}

typedef AEffect *(*MainProc)(audioMasterCallback);
static MainProc g_main;

static AEffect *load(void) {
    AEffect *fx = g_main(host_cb);
    if (!fx) return NULL;
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0.f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, RATE);
    fx->dispatcher(fx, effSetBlockSize, 0, BLOCK, NULL, 0.f);
    fx->dispatcher(fx, effMainsChanged, 0, 1, NULL, 0.f);
    return fx;
}

/* one MIDI message, delivered at the start of the next block */
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[4]; } Events;
typedef struct { Events hdr; VstMidiEvent ev[4]; } Batch;

static void send_midi(AEffect *fx, unsigned char status, unsigned char note, unsigned char vel) {
    Batch b;
    memset(&b, 0, sizeof b);
    b.hdr.numEvents = 1;
    b.ev[0].type = kVstMidiType;
    b.ev[0].byteSize = sizeof(VstMidiEvent);
    b.ev[0].midiData[0] = (char)status;
    b.ev[0].midiData[1] = (char)note;
    b.ev[0].midiData[2] = (char)vel;
    b.hdr.events[0] = (VstEvent *)&b.ev[0];
    fx->dispatcher(fx, effProcessEvents, 0, 0, (VstEvents *)&b.hdr, 0.f);
}

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

typedef struct { float L[BLOCKS * BLOCK], R[BLOCKS * BLOCK]; double a_max_us, pair_max_us, a_sum_us; } Run;

/* Plays the melody on A for BLOCKS blocks. With drums set, B plays a kick and snare pattern beside it, and sends a
 * note-off for a note it never played (which must be ignored). Keeps A's output in r. */
static void run(Run *r, int drums) {
    AEffect *a = load();
    AEffect *b = drums ? load() : NULL;
    float bl[BLOCK], br[BLOCK];
    float *outA[2] = {r->L, r->R};
    float *outB[2] = {bl, br};
    r->a_max_us = r->pair_max_us = r->a_sum_us = 0;
    for (int k = 0; k < BLOCKS; k++) {
        if (k == 0) send_midi(a, 0x90, 48, 100);            /* melody: C3 on A */
        if (k == 40) send_midi(a, 0x80, 48, 0);             /* and its note off, on A only */
        if (b) {
            if (k % 8 == 0) send_midi(b, 0x90, 36, 110);    /* kick on B */
            if (k % 8 == 4) send_midi(b, 0x90, 38, 100);    /* snare on B */
            if (k == 20) send_midi(b, 0x80, 48, 0);         /* a note-off for C3 that B never started */
        }
        double t0 = now_us();
        a->processReplacing(a, NULL, outA, BLOCK);
        double ta = now_us() - t0;
        if (ta > r->a_max_us) r->a_max_us = ta;
        r->a_sum_us += ta;
        outA[0] += BLOCK; outA[1] += BLOCK;
        if (b) {
            double t1 = now_us();
            b->processReplacing(b, NULL, outB, BLOCK);
            double pair = ta + (now_us() - t1);
            if (pair > r->pair_max_us) r->pair_max_us = pair;
        }
    }
    if (b) { b->dispatcher(b, effClose, 0, 0, NULL, 0.f); }
    a->dispatcher(a, effClose, 0, 0, NULL, 0.f);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <velvetchords.so>\n", argv[0]); return 2; }
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    g_main = (MainProc)dlsym(h, "VSTPluginMain");
    CHECK(g_main != NULL, "VSTPluginMain exported");
    if (!g_main) return 1;
    printf("instance isolation\n");

    Run *alone = calloc(1, sizeof *alone), *beside = calloc(1, sizeof *beside);
    if (!alone || !beside) return 1;
    run(alone, 0);
    run(beside, 1);

    /* the melody must be the same sound, sample for sample, with the drums playing beside it */
    int differs = 0;
    float worst = 0;
    for (int i = 0; i < BLOCKS * BLOCK; i++) {
        float dl = fabsf(alone->L[i] - beside->L[i]), dr = fabsf(alone->R[i] - beside->R[i]);
        if (dl > 0 || dr > 0) differs++;
        if (dl > worst) worst = dl;
        if (dr > worst) worst = dr;
    }
    float peak = 0;
    for (int i = 0; i < BLOCKS * BLOCK; i++) if (fabsf(alone->L[i]) > peak) peak = fabsf(alone->L[i]);
    CHECK(peak > 0.01f, "the melody makes sound on its own");
    CHECK(differs == 0, "the melody is identical with the drums playing beside it");
    printf("  worst difference in the melody: %g (%d samples differ)\n", worst, differs);

    /* the deadline: a 256-frame block at 48 kHz gives 5333 us */
    double budget = BLOCK / RATE * 1e6;
    printf("  block time, melody alone: average %.1f us, worst %.1f us of %.0f us\n",
           alone->a_sum_us / BLOCKS, alone->a_max_us, budget);
    printf("  block time, melody plus drums: worst %.1f us of %.0f us\n", beside->pair_max_us, budget);
    CHECK(beside->pair_max_us < budget, "a melody and a drum track fit inside the block deadline on this machine");

    free(alone);
    free(beside);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
