/* VST2 host for Velvet Chords: loads a VST2 plugin (.so) from the MPC's plugin folder and runs it inside Velvet
 * Chords, so its sound can come out of this plugin. Plain C; needs libc and libdl only.
 *
 * Threads: vh_open, vh_probe, vh_close and the parameter calls run on the control thread. vh_process runs on the
 * audio thread and must not be called from anywhere else, and never while the same instance is being opened or closed.
 */
#ifndef VHOST_H
#define VHOST_H
#include <stddef.h>
#include <stdint.h>
#include "vst2_abi.h"

#define VH_MAX_NOTES 128         /* MIDI events handed to a hosted plugin in one call */
#define VH_NAME_LEN 64

typedef struct VHost VHost;

/* One MIDI message for a hosted plugin. frame is the sample offset inside the buffer passed to vh_process. */
typedef struct { int32_t frame; unsigned char status, data1, data2; } VhMidi;

/* What a plugin file says about itself, read by loading it once (see vh_probe). */
typedef struct {
    char name[VH_NAME_LEN];
    int32_t uid;             /* VST unique ID */
    int is_instrument;       /* 1 = synth (makes sound from MIDI), 0 = effect */
    int inputs, outputs;
    int nparams;
} VhInfo;

/* Lists the VST2 candidates in dir: files ending in .so. Writes up to max full paths into paths[][VH_PATH_LEN].
 * Nothing is loaded. Returns the count written, or -1 if dir can't be read. */
#define VH_PATH_LEN 256
int vh_list(const char *dir, char paths[][VH_PATH_LEN], int max);

/* Loads a plugin file just long enough to read its VhInfo, then unloads it. Returns 0 on success, -1 if the file isn't
 * a VST2 plugin or is Velvet Chords itself. Loading an unknown plugin runs its code: scan only plugins you trust. */
int vh_probe(const char *path, VhInfo *info);

/* Loads and starts a plugin for the given sample rate and maximum block size. Returns NULL on failure, including
 * a plugin that is Velvet Chords itself. */
VHost *vh_open(const char *path, double sample_rate, int block_size);
void vh_close(VHost *h);

const VhInfo *vh_info(const VHost *h);

/* Renders n frames. Notes must be sorted by frame, each frame in [0, n). outL and outR receive the plugin's stereo output
 * (a mono plugin is copied to both). Returns 0, or -1 if h is NULL. */
int vh_process(VHost *h, const VhMidi *notes, int nnotes, float *outL, float *outR, int n);

int vh_param_count(const VHost *h);
void vh_param_name(const VHost *h, int index, char *buf, size_t len);
float vh_get_param(const VHost *h, int index);
void vh_set_param(VHost *h, int index, float value);

#endif
