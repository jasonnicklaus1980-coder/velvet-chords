/* Reads an MPC keygroup (XPM) program into a plain structure. Built from the field notes in docs/XPM_FORMAT.md, which
 * are not yet checked against a file made by an MPC. Plain C, libc only. */
#ifndef VC_XPM_READ_H
#define VC_XPM_READ_H
#include <stddef.h>

#define XPM_MAX_LAYERS 16
#define XPM_MAX_INSTRUMENTS 128

typedef struct {
    int vel_start, vel_end;      /* velocity range, 0..127 */
    char sample[256];            /* SampleName, without the .WAV extension */
    int active;                  /* 1 unless Active is present and not "True" */
    double gain_db;              /* from Volume (see xpm_gain_db) */
    double pan;                  /* -1..1, from Pan (see xpm_pan) */
    double tune_cents;           /* Pitch, or TuneCoarse + TuneFine */
    int root_note;               /* the real root note: the file's RootNote minus 1; -1 if absent */
    int key_track;
    long slice_start, slice_end; /* sample start and end in frames; -1 if absent */
} XpmLayer;

typedef struct {
    int number;                  /* the Instrument number attribute: the keygroup or pad */
    int low_note, high_note;     /* LowNote and HighNote (drum programs use number) */
    int zone_play;               /* 0 cycle, 1 velocity, 2 random (ZonePlay) */
    int trigger;                 /* 0 one-shot, 1 release, 2 attack (TriggerMode) */
    int one_shot;
    int nlayers;
    XpmLayer layers[XPM_MAX_LAYERS];
} XpmInstrument;

typedef struct {
    int is_drum;                 /* type attribute "Drum" (else "Keygroup") */
    char name[128];
    int num_keygroups;
    double pitchbend_octaves;
    int ninstruments;
    XpmInstrument instruments[XPM_MAX_INSTRUMENTS];
} XpmProgram;

/* Parses the text of an .xpm file. Returns 0 on success, -1 if it isn't an MPCVObject program the reader understands. */
int xpm_read(const char *xml, size_t len, XpmProgram *out);

/* Gain in dB from a stored Volume value (0.353 is -12 dB, 1.0 is +6 dB). */
double xpm_gain_db(double volume);

/* Pan in -1..1 from a stored Pan value (0.5 is centre). */
double xpm_pan(double pan);

/* The path of a layer's WAV: folder + "/" + SampleName + ".WAV" (upper case, as the reader in use looks for). */
int xpm_sample_path(const char *folder, const char *sample, char *out, size_t len);

#endif
