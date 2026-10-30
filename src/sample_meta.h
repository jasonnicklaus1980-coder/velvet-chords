/* Sample metadata for keygroup work: reads a WAV held in memory and reports its format, root note (from a smpl chunk)
 * and loop points. Plain C, libc only. Works on the bytes of a file; nothing is written. */
#ifndef VC_SAMPLE_META_H
#define VC_SAMPLE_META_H
#include <stddef.h>
#include <stdint.h>

#define VC_MAX_LOOPS 8

typedef struct { uint32_t start, end, type; } VcLoop;   /* type 0 = forward, as in the WAV smpl chunk */

typedef struct {
    int channels, bits, is_float;
    uint32_t rate, frames;
    int has_smpl;            /* 1 when a smpl chunk was present */
    int unity_note;          /* root note (0..127) from smpl; 60 when there is no smpl chunk */
    int nloops;
    VcLoop loops[VC_MAX_LOOPS];
} VcSampleInfo;

/* Reads the WAV in buf[0..len). Returns 0 on success, -1 if it isn't a WAV this reader understands
 * (PCM 16/24/32 or IEEE float, one or more channels) or the chunks run past the end of the data. */
int vc_sample_info(const unsigned char *buf, size_t len, VcSampleInfo *out);

#endif
