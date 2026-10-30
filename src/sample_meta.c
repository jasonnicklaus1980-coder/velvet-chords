#include "sample_meta.h"
#include <string.h>

static uint32_t rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int vc_sample_info(const unsigned char *buf, size_t len, VcSampleInfo *out) {
    if (!buf || !out || len < 12) return -1;
    memset(out, 0, sizeof *out);
    out->unity_note = 60;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return -1;

    int have_fmt = 0, have_data = 0;
    size_t pos = 12;
    while (pos + 8 <= len) {
        uint32_t csize = rd32(buf + pos + 4);
        const unsigned char *body = buf + pos + 8;
        size_t avail = len - (pos + 8);
        if (csize > avail) return -1;                       /* chunk runs past the end */
        if (memcmp(buf + pos, "fmt ", 4) == 0) {
            if (csize < 16) return -1;
            unsigned tag = rd16(body);
            out->channels = rd16(body + 2);
            out->rate = rd32(body + 4);
            out->bits = rd16(body + 14);
            if (tag == 0xFFFE && csize >= 26) tag = rd16(body + 24);   /* extensible: the real tag is the sub-format */
            out->is_float = tag == 3;
            if (!(tag == 1 || tag == 3)) return -1;
            if (out->channels < 1 || out->rate == 0) return -1;
            if (out->is_float ? out->bits != 32 : !(out->bits == 16 || out->bits == 24 || out->bits == 32)) return -1;
            have_fmt = 1;
        } else if (memcmp(buf + pos, "data", 4) == 0) {
            size_t frame = (size_t)(out->bits / 8) * (size_t)(out->channels > 0 ? out->channels : 1);
            out->frames = frame ? (uint32_t)(csize / frame) : 0;
            have_data = 1;
        } else if (memcmp(buf + pos, "smpl", 4) == 0 && csize >= 36) {
            out->has_smpl = 1;
            out->unity_note = (int)rd32(body + 12) & 127;
            uint32_t n = rd32(body + 28);
            if (n > VC_MAX_LOOPS) n = VC_MAX_LOOPS;
            if (csize < 36 + 24 * n) return -1;
            out->nloops = (int)n;
            for (uint32_t i = 0; i < n; i++) {
                const unsigned char *l = body + 36 + 24 * i;
                out->loops[i].type = rd32(l + 4);
                out->loops[i].start = rd32(l + 8);
                out->loops[i].end = rd32(l + 12);
            }
        }
        pos += 8 + (size_t)csize + (csize & 1);
    }
    return have_fmt && have_data ? 0 : -1;
}
