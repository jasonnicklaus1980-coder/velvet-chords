#ifndef VC_MIDIIMPORT_H
#define VC_MIDIIMPORT_H
#include "chords.h"

/* One chord read from a MIDI file: its name (spec) and the exact notes from the file. */
typedef struct { ChordSpec spec; int n; unsigned char notes[VC_MAX_NOTES]; } ImportChord;

/* Reads a Standard MIDI File (format 0/1, drums on channel 10 ignored). Notes that start together (within a
 * 32nd note) form a chord; consecutive repeats are skipped. Returns the number of chords, or -1 with err set. */
int vc_midi_import(const char *path, ImportChord *out, int max, char *err, int errlen);
/* names a note set: chord-table match with the root on the bass if possible, slash chord otherwise */
void vc_identify_chord(const unsigned char *notes, int n, ChordSpec *out);
#endif
