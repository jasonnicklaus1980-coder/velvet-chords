#ifndef VC_LAYOUTS_H
#define VC_LAYOUTS_H
#include "chords.h"
#include "songs.h"
#include "midiimport.h"
#include "progression.h"

#define VC_USER_PROGS 4 /* saved progressions the user can recall */

#define VC_PADS 64 /* 4 MPC pad banks (A-D) x 16 */

/* the Pad Perform modes: the first four are the original layouts; the last three are the MPC-style note and chord layouts,
 * which remap as soon as the key, scale or octave changes */
enum { VC_MODE_SONG, VC_MODE_KEY, VC_MODE_CUSTOM, VC_MODE_CHROM_NOTES, VC_MODE_SCALE_NOTES, VC_MODE_CHROM_CHORDS, VC_MODE_COUNT };
#define VC_MODE_IMPORT VC_MODE_COUNT /* internal: pads show chords imported from a MIDI file */
extern const char *VC_MODE_NAMES[VC_MODE_COUNT];

typedef struct { const char *name; int n; signed char iv[8]; const char *desc; } Scale; /* desc: a few words, shown with the scale */
extern const Scale VC_SCALES[];
extern const int VC_NUM_SCALES;

/* Detect: a key and scale that fit the played pitch classes (bit k = pitch class k, C = 0), best first.
 * containing != 0: only scales that hold every played pitch class. Returns how many were written to out. */
typedef struct { int key, scale, matches, size; } DetectHit;
int vc_detect(unsigned pcmask, int containing, DetectHit *out, int max);

typedef struct {
    unsigned char valid, root, quality, bass; /* bass 255 = none */
    signed char inv, oct;
    unsigned char nnotes;            /* 0: use quality; >= 2: explicit note list (note by note editing) */
    signed char notes[VC_MAX_IV];   /* semitones above the root, notes[0] == 0 */
} CustomPad;

typedef struct {
    int valid;
    ChordSpec spec;
    unsigned char single, midi; /* single: the pad plays one note, midi, and nothing else */
    int inv, oct;      /* per-pad offsets added to the global settings */
    char name[24];     /* chord symbol shown to the user */
    char roman[16];    /* function in the current key, may be empty */
    int nexact;        /* imported chords keep the file's exact notes */
    unsigned char exact[VC_MAX_NOTES];
} PadChord;

typedef struct {
    int mode, song, section, key, scale, transpose;
    int prog;                /* progression: 0 = off, 1..N templates (progression.h), then the user slots; fills pads 1.. */
    /* the user progressions: saved chords as root and quality (quality -1 = empty step), not transposed */
    unsigned char un[VC_USER_PROGS], ur[VC_USER_PROGS][VC_PROG_MAX];
    signed char uq[VC_USER_PROGS][VC_PROG_MAX];
    int voicing, inversion, octave, voice_lead;
    const ImportChord *imp;  /* VC_MODE_IMPORT */
    int nimp;
} LayoutParams;

/* the progression in use: how many steps, and the chord of step k (0 = empty step) */
int vc_prog_length(const LayoutParams *p);
int vc_prog_step(const LayoutParams *p, int k, ChordSpec *out);
int vc_prefer_flats(int key_pc, int minor);
int vc_scale_is_minor(int scale);
void vc_roman(int key_pc, const ChordSpec *c, char *buf, int len);
void vc_build_pads(const LayoutParams *p, const CustomPad *custom, PadChord out[VC_PADS]);
/* voice every pad; voice leading runs pad to pad inside each bank */
void vc_voice_pads(const LayoutParams *p, const PadChord pads[VC_PADS], Voicing out[VC_PADS]);

/* Song helpers */
int vc_song_section_index(const LayoutParams *p, int bank); /* -1 if none */
int vc_song_flats(const Song *s, int transpose);

#endif
