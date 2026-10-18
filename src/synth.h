#ifndef VC_SYNTH_H
#define VC_SYNTH_H

#define SY_VOICES 48
#define SY_KS_LEN 2048 /* plucked-string delay line per voice */
/* ORDER IS PART OF SAVED PROJECTS: E. piano, Pad, Organ and Silent keep the positions the old 4-sound list mapped to */
/* positions chosen so projects saved with the 4- and 10-sound lists still pick the same instrument */
enum { SY_EPIANO, SY_RHODES, SY_WURLI, SY_DYNO, SY_CLAV, SY_PIANO, SY_PAD, SY_POLY, SY_STRINGS, SY_SUPERSAW,
       SY_CHOIR, SY_BRASS, SY_HARP, SY_ORGAN, SY_MARIMBA, SY_VIBES, SY_NYLON, SY_PLUCK, SY_MUSICBOX, SY_SILENT,
       SY_FMBELL, SY_SYNBASS, SY_GLASS, SY_SAWLEAD, SY_SOUND_COUNT };
/* the Sound parameter has the first SY_BASE_COUNT of these; the four after Silent are chosen with Sound extra */
#define SY_BASE_COUNT (SY_SILENT + 1)
extern const char *SY_SOUND_NAMES[SY_SOUND_COUNT];
extern const char *SY_SOUND_SHORT[SY_SOUND_COUNT];

typedef struct {
    int active, note, owner, released, sound, stage;
    unsigned age;
    float vel, amp, attK, decMul, relMul, inc, pan;
    float ph[4];
    float modEnv, modMul, tineEnv, tineMul, modDepth;
    float lpA, lp1, lp2;
    float lfo, lfoInc, fenv, fenvMul;  /* vibrato/tremolo, filter envelope */
    float *ks; int kslen, kspos; float ksdamp;
    float php[8], pinc[8], pamp[8], pmul[8];  /* additive partials / extra oscillators */
    float s1l, s1b, s2l, s2b, sf1, sf2, sq;   /* two state-variable filters (choir formants) */
} SVoice;

typedef struct {
    SVoice v[SY_VOICES];
    float ks[SY_VOICES][SY_KS_LEN];
    float sr;
    int sound;
    float tone, release, volume;
    unsigned age, rng;
} Synth;

void sy_init(Synth *s, float sr);
void sy_note_on(Synth *s, int note, float vel, int owner);
void sy_release(Synth *s, int note, int owner); /* note < 0: every voice of owner */
void sy_kill_all(Synth *s);
void sy_render(Synth *s, float *L, float *R, int n); /* adds into L/R */
int sy_active(const Synth *s);
int sy_sound_decays(int sound); /* true for struck/plucked sounds that fade on their own */

#endif
