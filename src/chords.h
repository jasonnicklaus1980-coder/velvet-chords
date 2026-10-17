#ifndef VC_CHORDS_H
#define VC_CHORDS_H

#define VC_MAX_IV 8
#define VC_MAX_NOTES 10

typedef struct {
    const char *suffix; /* canonical chord-symbol suffix, e.g. "m9" */
    const char *label;  /* readable name for menus */
    int n;
    signed char iv[VC_MAX_IV]; /* semitones above root, iv[0] == 0; compound intervals allowed */
} ChordQuality;

extern const ChordQuality VC_QUALITIES[];
extern const int VC_NUM_QUALITIES;

typedef struct {
    int root;    /* pitch class 0..11 */
    int quality; /* index into VC_QUALITIES, or -1 for custom intervals */
    int bass;    /* -1 = root in bass, else pitch class of slash bass */
    int ncustom;
    signed char custom[VC_MAX_IV];
} ChordSpec;

enum { VC_VOICE_BASS_UPPER, VC_VOICE_CLOSE, VC_VOICE_DROP2, VC_VOICE_SPREAD, VC_VOICE_UPPER_ONLY, VC_VOICE_COUNT };
extern const char *VC_VOICE_NAMES[VC_VOICE_COUNT];

typedef struct {
    int n;
    int has_bass; /* notes[0] is a separate bass note */
    unsigned char notes[VC_MAX_NOTES]; /* ascending MIDI notes */
} Voicing;

int vc_pc_parse(const char *s, int *consumed); /* "Eb" -> 3, -1 on error */
const char *vc_pc_name(int pc, int flats);
void vc_note_name(int midi, int flats, char *buf, int len); /* MPC convention: 60 = C3 */
int vc_quality_find(const char *suffix);       /* accepts aliases; -1 if unknown */
int vc_chord_parse(const char *sym, ChordSpec *out);
void vc_chord_name(const ChordSpec *c, int flats, char *buf, int len);
int vc_chord_intervals(const ChordSpec *c, int *iv); /* returns count */
int vc_chord_pcmask(const ChordSpec *c);           /* bit per pitch class incl. bass */
ChordSpec vc_chord_transpose(const ChordSpec *c, int semis);
int vc_quality_match(int ivmask_rel_root, int ncount); /* exact pitch-class-set match, -1 none */

/* Voice a chord. inversion: 0..3 (rotations of the upper structure), octave: -2..+2.
 * prev: previous voicing for voice leading (NULL = no voice leading; inversion is then used). */
void vc_voice(const ChordSpec *c, int style, int inversion, int octave, const Voicing *prev, Voicing *out);

#endif
