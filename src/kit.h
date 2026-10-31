/* A drum kit (XPM) played from its WAV samples. The kit is loaded in the background: kit_new reads the program, then
 * kit_load_instrument loads one instrument's samples and marks it ready. The audio thread only plays instruments that
 * are ready. It never loads, allocates or frees.
 *
 * Instrument number N plays MIDI note KIT_NOTE0 + N - 1, so a 64-pad program covers notes 36 to 99.
 * Samples are kept as 16-bit and played with linear interpolation when the rates differ. Pan is linear. */
#ifndef VC_KIT_H
#define VC_KIT_H
#include <stddef.h>
#include <stdint.h>
#include "xpm_read.h"

#define KIT_NOTE0 36       /* the note that plays instrument 1 */
#define KIT_VOICES 32      /* samples that can sound at once; the oldest is taken when all are busy */
#define KIT_PATH_LEN 256

typedef struct {
    int16_t *data;         /* interleaved; NULL if the sample didn't load */
    size_t frames;
    int channels;          /* 1 or 2 */
    int rate;
} KitSample;

typedef struct {
    int vel_start, vel_end;
    int one_shot;
    float amp;             /* linear gain from the layer's Volume */
    float pan;             /* 0 left .. 1 right */
    int root_note;         /* the note the sample plays at its own pitch; -1 if the file has none */
    float tune;            /* tuning in cents, coarse and fine together */
    KitSample s;
} KitLayer;

typedef struct {
    int ready;             /* 1 once the instrument's samples are loaded (set with release, read with acquire) */
    int nlayers;
    int low_note, high_note;   /* the keys this instrument answers to (a drum pad answers to one) */
    KitLayer layers[XPM_MAX_LAYERS];
} KitInst;

typedef struct {
    char dir[KIT_PATH_LEN];                 /* the folder the program is in: its WAV files are looked for here first */
    char name[128];
    XpmProgram *prog;
    char **wavs;                            /* every WAV file under the kit's root, for samples kept in another folder */
    int nwavs;
    KitInst inst[XPM_MAX_INSTRUMENTS];      /* indexed by instrument number - 1 */
} Kit;

/* Reads the program at xpm_path. root is the folder the program was found in (an expansion or the kit folder); the WAV
 * files under it are indexed, so samples kept in another folder of the expansion are found too. Pass NULL to look only
 * beside the program. The samples are not loaded yet. Returns NULL if the file can't be read or isn't a program the
 * reader understands. */
Kit *kit_new(const char *xpm_path, const char *root);

/* Loads the WAV samples of program instrument i (0-based, in file order) and marks its slot ready. Returns 0 on
 * success. A layer whose WAV is missing is left out; the instrument still becomes ready. */
int kit_load_instrument(Kit *k, int i);

void kit_free(Kit *k);

/* Decodes a WAV file to 16-bit. Accepts PCM of 16, 24 or 32 bits and 32-bit float, mono or stereo. Returns 0 on
 * success, -1 otherwise. Free the result with kit_sample_free. */
int kit_wav_load(const char *path, KitSample *out);
void kit_sample_free(KitSample *s);

typedef struct {
    const KitLayer *layer;
    int inst;              /* instrument slot (number - 1) */
    int note;              /* the MIDI note that started it */
    double pos, step;      /* position in the sample, frames per output frame */
    float gl, gr;          /* gain per channel, velocity and layer included */
    unsigned long age;
    int active;
} KitVoice;

typedef struct {
    KitVoice v[KIT_VOICES];
    unsigned long clock;
    unsigned rr[XPM_MAX_INSTRUMENTS];   /* round robin: which layer plays next, per instrument */
    const Kit *seen;       /* the kit the voices were started from; a different kit silences them */
    float gain;            /* the kit's level: volume, or 0 when muted. Set by the plugin before each block. */
} KitPlayer;

void kit_player_reset(KitPlayer *kp);

/* Starts every instrument whose keys include note, at velocity 1..127, pitched from the layer's root note. Does nothing
 * for an instrument that isn't ready or has no layer covering the velocity. sr is the output sample rate. */
void kit_note_on(KitPlayer *kp, const Kit *k, int note, int vel, double sr);

/* Ends the sustained voices started by note. One-shot samples play out. */
void kit_note_off(KitPlayer *kp, int note);

/* Makes the player follow kit k: if it's a different kit (or none), the voices are silenced first. Call it before
 * starting notes on a kit, so a note at the start of a block isn't lost to the change. */
void kit_player_follow(KitPlayer *kp, const Kit *k);

/* Adds n frames of the kit's output to L and R. A null kit silences the player. */
void kit_render(KitPlayer *kp, const Kit *k, float *L, float *R, int n);

#endif
