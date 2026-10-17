#ifndef VC_EXPORT_H
#define VC_EXPORT_H
#include "layouts.h"
#include <stdint.h>

typedef struct {
    float start_beats, dur_beats;
    int n;
    unsigned char notes[VC_MAX_NOTES];
    unsigned char vel;
    char name[24];
} MfChord;

/* Standard MIDI File, format 1: conductor track (tempo, time signature, provenance text) + chord track
 * (note events and a marker with each chord's name). */
int vc_midi_write(const char *path, const char *title, const char *text, const MfChord *ch, int n,
                  double bpm, int tsn, int tsd);
/* MPC Pad Perform progression (JSON). Format follows commonly shared .progression files; verify on your MPC. */
int vc_progression_write(const char *path, const char *name, const char *rootName, const MfChord *ch, int n);

int vc_section_chords(const Song *s, int sec, const LayoutParams *lp, MfChord *out, int max);
int vc_bank_chords(const PadChord *pads, const Voicing *v, int bank, MfChord *out, int max);
void vc_safe_filename(const char *in, char *out, int len);
void vc_provenance_text(const Song *s, const LayoutParams *lp, char *buf, int len);

#endif
