#ifndef VC_ENGINE_H
#define VC_ENGINE_H
#include "layouts.h"
#include "synth.h"
#include <stdint.h>

#define EN_QUEUE 1024
#define EN_OWNERS 128
#define EN_MIDI_IN 1024
#define EN_MIDI_OUT 1024

enum { STRUM_UP, STRUM_DOWN, STRUM_ALT, STRUM_COUNT };
enum { ARP_OFF, ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_PLAYED /* random order */, ARP_CONVERGE, ARP_DIVERGE, ARP_COUNT };
enum { RATE_4, RATE_8, RATE_8T, RATE_16, RATE_16T, RATE_32, RATE_COUNT };
enum { VEL_LINEAR, VEL_SOFT, VEL_HARD, VEL_FIXED, VEL_COUNT };

/* Pattern generator. Step characters:
 *   .  rest (silence)      x  chord          X  accented chord
 *   b  bass note only      u  upper notes    U  accented upper notes
 *   -  tie (keep the previous step sounding) */
#define EN_STEPS 16
enum { PRATE_8, PRATE_8T, PRATE_16, PRATE_16T, PRATE_32, PRATE_COUNT };
typedef struct { const char *name; const char *steps; } PatternPreset;

extern const PatternPreset EN_PATTERNS[];
extern const int EN_NUM_PATTERNS;
extern const char EN_STEP_CHARS[]; /* index -> step character, for editing */
int en_step_index(char c);

/* Melodic patterns: six user slots of 16 steps. Each step is a semitone offset from the held chord's root
 * (RIFF_REST = silence). The melody plays one octave above the chord's lowest note, so it follows every chord. */
#define VC_RIFFS 6
#define RIFF_REST (-128)
#define RIFF_MIN (-12)
#define RIFF_MAX 24
extern const signed char EN_RIFF_DEFAULTS[VC_RIFFS][EN_STEPS];
extern const char *EN_RIFF_NAMES[VC_RIFFS];

/* Shift keys: the black keys in the octave below the default pads. Each press changes the layout or the pattern
 * on the spot, so the chords and the pads stay where they are. */
enum { SHIFT_KEY_DOWN, SHIFT_KEY_UP, SHIFT_SCALE_PREV, SHIFT_SCALE_NEXT, SHIFT_INVERSION, SHIFT_COUNT };
int en_shift_action(int note); /* -1 when the note is not a shift key */

/* Smart Scale pads: single notes that follow the current chord */
enum { SMART_OFF, SMART_SPLIT /* pads 9-16 */, SMART_FOLLOW /* all pads, chord from another instance */, SMART_COUNT };
enum { SMODE_CHORD, SMODE_SCALE, SMODE_BASS, SMODE_COUNT };
/* Harmony: a second voice added to each smart-pad note, in the key's scale (octave is a plain 12 semitones) */
enum { HARM_OFF, HARM_THIRD_UP, HARM_SIXTH_UP, HARM_THIRD_DOWN, HARM_OCTAVE, HARM_COUNT };
/* Bassline styles, four beats per bar: R root, F fifth, O octave, T third, A approach to the next chord's root, - hold */
enum { BASS_OFF, BASS_ROOT /* R--- */, BASS_ROOT_FIFTH /* R-F- */, BASS_OCTAVES /* RORO */, BASS_WALKING /* RTFA */, BASS_COUNT };
/* Smart map: how the smart pads are laid out. FOLLOW uses the Smart mode above and the chord; the others ignore the chord:
 * RELATIVE walks the key's scale from the key root, ABSOLUTE walks the scale from C, CHROMATIC walks all twelve semitones from the key. */
enum { SMAP_FOLLOW, SMAP_RELATIVE, SMAP_ABSOLUTE, SMAP_CHROMATIC, SMAP_COUNT };
typedef struct {
    int valid, root, bass, npcs, flats;
    int pcs[12];     /* chord pitch classes, ordered upward from the root */
    char name[24];
} SmartChord;
/* the chord most recently played by any Velvet Chords instance in this process (MPC loads every instance into
 * the same program, so this is how a second instance follows the first) */
typedef struct { volatile unsigned version; int source; SmartChord chord; } SharedChord;
extern SharedChord g_vc_shared;
/* key + scale shared between copies (instance sync) */
typedef struct { volatile unsigned version; int source, key, scale; } SharedKey;
extern SharedKey g_vc_key;

enum { MEL_OFF, MEL_SIMPLE, MEL_BUSY, MEL_COUNT };
enum { MRATE_8, MRATE_8T, MRATE_16, MRATE_COUNT };
enum { REP_OFF, REP_4, REP_8, REP_8T, REP_16, REP_16T, REP_32, REP_COUNT };

typedef struct { int64_t when; int owner; unsigned char slot, vel; } QEvent;
typedef struct { int frame; unsigned char d[3]; } MidiMsg;

typedef struct {
    int owner, held, sustained;
    int n;
    unsigned char notes[VC_MAX_NOTES];
    unsigned char sounding[VC_MAX_NOTES];
    float vel, vel_base;
    int root;                                   /* pitch class of the held chord's root */
    int base_n;                                 /* the chord as played; riff steps replace notes[] temporarily */
    unsigned char base_notes[VC_MAX_NOTES];
} OwnerRec;

/* Multi arp: four lanes that run at the same time, each with its own order, rate, octave range and transpose,
 * all playing the held chord. */
#define EN_ARP_LANES 4
typedef struct {
    int order, rate, oct, transpose;   /* ARP_* (ARP_OFF = lane off), RATE_*, 1..4 octaves, -12..+12 semitones */
    int owner, cnt, dirn, note;        /* running state: the held chord, step counter, direction, sounding note */
    int64_t next;                      /* sample time of the next step */
} ArpLane;

typedef struct Engine {
    Synth synth;
    float sr;
    int64_t now; /* absolute sample clock at the start of the current block */

    /* layout */
    LayoutParams lp;
    CustomPad custom[VC_PADS];
    PadChord pads[VC_PADS];
    Voicing voicings[VC_PADS];
    volatile int dirty;

    /* performance settings */
    int pad_base, learn, midi_out, midi_out_flush, edit_follow;
    signed char pad_map[128];      /* learned: incoming note -> pad 0..15 of bank A, -1 = not a learned pad */
    int map_set;                   /* a full 16-pad layout has been learned */
    int learn_all;                 /* 1..16 = waiting for that pad; 0 = not learning */
    float strum_ms;
    int strum_dir, strum_flip;
    int vel_curve;
    int out_channel; /* 0-15 */
    double tempo;

    /* pattern generator */
    int pattern_on, pat_rate, pat_sync;
    float swing, gate; /* swing 0..1 (fraction of half a step), gate 0.1..1 */
    char pat[EN_STEPS];
    double ppq;        /* host song position at block start (quarter notes) */
    int playing;       /* host transport running */
    int pat_owner, pat_is_repeat;
    int pat_len;                   /* steps before the pattern starts again: 4, 8, 12 or 16 */
    int pat_chance;                /* % of pattern hits that play: 25, 50, 75 or 100 */
    int pat_seed;                  /* 1..4: picks which steps the chance skips */
    int hold, latched_note;        /* Hold: the latched pad's input note, -1 none */
    unsigned det_pcs;              /* Detect: pitch classes played since Clear (bit k = pitch class k) */
    int repeat;                    /* REP_*: retrigger the chord at this rate */
    int mel_mode, mel_rate;        /* melody generator */
    int mel_owner, mel_parent, mel_prev;
    int64_t mel_k, mel_last_k, mel_off_at;
    double mel_grid;
    unsigned mel_rng;
    int64_t pat_k;     /* absolute index of the next step */
    double pat_grid;   /* sample time of the next step before swing */
    int64_t pat_off_at;
    int64_t pat_last_k;

    /* state */
    int pad_owner[128];
    OwnerRec owners[EN_OWNERS];
    int next_owner;
    /* progression play: one chord per bar while the host plays (the Progression control picks the progression) */
    int prog_play;   /* 1 = on */
    int prog_bar;    /* the bar the progression chord started in, -1 none */
    int prog_owner;  /* owner sounding the current bar's chord, 0 none */
    /* bassline: follows the progression chord on the beat grid while Progression play runs */
    int bass_style;  /* BASS_*: 0 = off */
    int bass_beat;   /* the beat a bass note last started on, -1 none */
    int bass_owner;  /* owner sounding the current bass note, 0 none */
    int sustain;
    QEvent queue[EN_QUEUE];
    int nqueue;
    ArpLane lanes[EN_ARP_LANES];

    /* riffs and shift keys */
    int riff;                                   /* 0 off, else slot 1..VC_RIFFS (plays on the pattern grid) */
    signed char rif[VC_RIFFS][EN_STEPS];
    int shift_keys;
    unsigned char shift_pending[SHIFT_COUNT];   /* presses waiting for the host thread */
    unsigned rng;
    float humanize;          /* 0..1: how much each pad hit's velocity wanders (Humanize parameter) */
    unsigned hum_rng;
    int out_ref[128];

    MidiMsg in[EN_MIDI_IN];
    int nin;
    MidiMsg out[EN_MIDI_OUT];
    int nout;

    /* smart scale */
    int smart, smart_mode, smart_oct, smart_map, harmony, instance_id;
    int scale_pcs_mask;          /* key scale for "Scale" mode */
    SmartChord own_chord;        /* last chord this instance played */
    SmartChord smart_src;        /* chord the smart pads currently follow */
    unsigned shared_seen;
    unsigned char smart_notes[16];
    volatile int smart_changed;

    int last_pad; /* -1 none */
    int last_note_in;
    int events_changed; /* set when last_pad / pad_base / edit pad changed (for host display refresh) */
    int edit_pad_request; /* -1 none, else pad index to select for editing */
} Engine;

void en_init(Engine *e, float sr);
void en_set_sr(Engine *e, float sr);
void en_rebuild(Engine *e);
void en_reset(Engine *e); /* all notes off, clear queues */
void en_push_midi(Engine *e, int frame, unsigned char s, unsigned char d1, unsigned char d2);
void en_process(Engine *e, float *L, float *R, int n); /* replaces L/R */
int en_owner_count(const Engine *e);
float en_vel_curve(int curve, int vel);
int en_pad_index(const Engine *e, int note); /* pad 0..63 for an incoming note, -1 if it isn't a pad */
int en_smart_index(const Engine *e, int pad); /* -1 if the pad is a chord pad */
void en_smart_update(Engine *e);
int en_arp_on(const Engine *e); /* any arpeggiator lane is on */

#endif
