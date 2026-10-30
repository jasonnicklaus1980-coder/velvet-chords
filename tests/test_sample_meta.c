/* Sample metadata tests: builds WAV bytes in memory and checks what the reader reports. */
#include <stdio.h>
#include <string.h>
#include "sample_meta.h"

static int failures = 0;
static void check(int ok, const char *what) { printf("  %s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

static void put32(unsigned char *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(unsigned char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

/* a WAV: fmt, optional smpl (unity note and one loop), data of `frames` frames */
static size_t make_wav(unsigned char *b, int bits, int channels, uint32_t rate, uint32_t frames, int smpl_note, int with_loop) {
    size_t p = 0;
    memcpy(b, "RIFF", 4); p = 8; memcpy(b + p, "WAVE", 4); p += 4;
    memcpy(b + p, "fmt ", 4); put32(b + p + 4, 16); p += 8;
    put16(b + p, 1); put16(b + p + 2, channels); put32(b + p + 4, rate);
    put32(b + p + 8, rate * channels * bits / 8); put16(b + p + 12, channels * bits / 8); put16(b + p + 14, bits); p += 16;
    if (smpl_note >= 0) {
        uint32_t nl = with_loop ? 1 : 0;
        memcpy(b + p, "smpl", 4); put32(b + p + 4, 36 + 24 * nl); p += 8;
        memset(b + p, 0, 36 + 24 * nl);
        put32(b + p + 12, (uint32_t)smpl_note);      /* unity note */
        put32(b + p + 28, nl);                         /* loop count */
        if (with_loop) { put32(b + p + 36 + 4, 0); put32(b + p + 36 + 8, 100); put32(b + p + 36 + 12, 5000); }
        p += 36 + 24 * nl;
    }
    uint32_t dsz = frames * channels * bits / 8;
    memcpy(b + p, "data", 4); put32(b + p + 4, dsz); p += 8;
    memset(b + p, 0, dsz); p += dsz;
    put32(b + 4, (uint32_t)(p - 8));
    return p;
}

int main(void) {
    static unsigned char wav[200000];
    VcSampleInfo si;
    printf("reader\n");
    size_t n = make_wav(wav, 24, 2, 44100, 1000, 48, 1);
    check(vc_sample_info(wav, n, &si) == 0, "a 24-bit stereo WAV with smpl is read");
    check(si.channels == 2 && si.bits == 24 && si.rate == 44100 && si.frames == 1000 && !si.is_float,
          "format: 2 channels, 24 bit, 44.1 kHz, 1000 frames");
    check(si.has_smpl && si.unity_note == 48, "root note comes from smpl (48)");
    check(si.nloops == 1 && si.loops[0].start == 100 && si.loops[0].end == 5000 && si.loops[0].type == 0,
          "one forward loop, 100..5000");

    n = make_wav(wav, 16, 1, 22050, 300, -1, 0);
    check(vc_sample_info(wav, n, &si) == 0 && si.bits == 16 && si.channels == 1 && si.frames == 300, "16-bit mono without smpl");
    check(!si.has_smpl && si.unity_note == 60 && si.nloops == 0, "no smpl: root note defaults to 60 and no loops");

    printf("rejects what it can't read\n");
    n = make_wav(wav, 24, 2, 44100, 10, -1, 0);
    unsigned char bad[300]; memcpy(bad, wav, n); memcpy(bad, "JUNK", 4);
    check(vc_sample_info(bad, n, &si) != 0, "a file that isn't RIFF/WAVE is refused");
    check(vc_sample_info(wav, 20, &si) != 0, "a truncated file is refused");
    n = make_wav(wav, 24, 2, 44100, 10, -1, 0);
    unsigned char cut[64]; memcpy(cut, wav, 60);
    check(vc_sample_info(cut, 60, &si) != 0, "a file cut inside its chunks is refused");
    check(vc_sample_info(NULL, 0, &si) != 0, "no data is refused");

    printf("\n%s\n", failures ? "SAMPLE METADATA TESTS FAILED" : "sample metadata tests passed");
    return failures ? 1 : 0;
}
