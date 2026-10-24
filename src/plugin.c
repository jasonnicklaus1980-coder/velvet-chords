/* Velvet Chords - chord pad instrument for MPC OS (VST2 ABI). */
#include "vst2_abi.h"
#include "engine.h"
#include "export.h"
#include "midiport.h"
#include "presets.h"
#include "progression.h"
#include "audioanalysis.h"
#include "fx.h"
#include "midiimport.h"
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>

#define VC_EXPORT __attribute__((visibility("default")))
#define VC_UID VC_FOURCC('V', 'l', 'v', 'C')
#define VC_VERSION 1000
#define IMP_NAME 96     /* longest path kept for an import file */
#define IMP_MAX_DEPTH 4 /* folder levels below Import that are searched */
#define CHUNK_VERSION 12 /* 4 adds note lists and riffs; 5 the saved progressions; 6 the user presets; 7 the starred presets (appended after the earlier data); 8 Scale is stored against the longer scale list; 9 Mode is stored without the R&B kit; 10 Mode has the chromatic and scale note modes and chromatic chords; 11 Pattern has the genre rhythms; 12 the Types mode is gone (Mode counts five layouts) */
#define VC_USER_PRESETS 4 /* user presets: your own settings, saved and recalled */
#define UPRE_MAX 64       /* values per user preset: the settings below plus the 16 pattern steps */

enum {
    P_MODE, P_SONG, P_SECTION, P_KEY, P_SCALE, P_TRANSPOSE, P_VOICING, P_INVERSION, P_OCTAVE, P_VL,
    P_STRUM, P_STRUM_DIR, P_ARP, P_ARP_RATE, P_VEL, P_SOUND, P_TONE, P_RELEASE, P_VOLUME,
    P_PAD_BASE, P_LEARN, P_MIDI_OUT,
    P_EDIT_PAD, P_EDIT_ROOT, P_EDIT_TYPE, P_EDIT_BASS, P_EDIT_INV, P_EDIT_OCT, P_EDIT_FOLLOW,
    P_COPY, P_EXPORT, P_NOW, P_INFO,
    /* screen-skin helpers (appended so older projects still load) */
    P_VIEW_BANK, P_ACTIVE,
    P_PADNAME0, P_PADFUNC0 = P_PADNAME0 + 16, P_SCALENOTE0 = P_PADFUNC0 + 16,
    P_SONG_PREV = P_SCALENOTE0 + 12, P_SONG_NEXT, P_SEC_PREV, P_SEC_NEXT, P_SCALE_PREV, P_SCALE_NEXT,
    P_TYPE_PREV, P_TYPE_NEXT, P_EPAD_PREV, P_EPAD_NEXT,
    /* ChordPrism-style MIDI output and pattern generator */
    P_MIDI_CH, P_PATTERN, P_PAT_RATE, P_SWING, P_GATE, P_PAT_SYNC,
    P_STEP0, P_PAT_PREV = P_STEP0 + EN_STEPS, P_PAT_NEXT, P_PORT_STATUS,
    /* Smart Scale */
    P_SMART, P_SMART_MODE, P_SMART_OCT,
    /* Tools: hold, repeat, melody, key sync, MIDI import */
    P_HOLD, P_REPEAT, P_MELODY, P_MEL_RATE, P_KEYSYNC,
    P_IMP_FILE, P_IMP_PREV, P_IMP_NEXT, P_IMP_LOAD, P_IMP_STATUS, P_IMP_PADS,
    P_LEARN_ALL,
    /* Presets: Manual or one of the factory presets (performance settings only) */
    P_PRESET, P_PRESET_PREV, P_PRESET_NEXT,
    /* Multi arp: lane 1 is Arp, Arp rate, Arp octaves, Arp transpose; lanes 2-4 are Arp 2-4 */
    P_ARP_OCT, P_ARP2, P_ARP3, P_ARP4, P_ARP2_RATE, P_ARP3_RATE, P_ARP4_RATE, P_ARP2_OCT, P_ARP3_OCT, P_ARP4_OCT,
    P_ARP_TR, P_ARP2_TR, P_ARP3_TR, P_ARP4_TR,
    /* riffs, shift keys and the note-by-note editor */
    P_SHIFT, P_RIFF, P_RIFF_SLOT, P_RIFF0, P_RIFF_END = P_RIFF0 + EN_STEPS - 1, P_NOTE_SEL, P_NOTE_IV,
    /* preset genre and variation: two knobs that pick one of the presets (Manual is genre 0) */
    P_PRESET_GENRE, P_PRESET_VAR,
    P_SMART_MAP, P_PROG, P_PROG_PLAY, P_UPROG_SLOT, P_UPROG_SAVE, P_PAT_LEN, P_PAT_CHANCE, P_PAT_SEED, P_BASS, P_HARM, P_UPRE_SLOT, P_UPRE_SAVE, P_UPRE_LOAD, P_UPRE_EXPORT, P_UPRE_IMPORT, P_UPRE_STATUS, P_UPRE_CLEAR, P_FAV_TOGGLE, P_FAV_PREV, P_FAV_NEXT, P_FAV_STATUS, P_ANALYZE, P_ANA_STATUS, P_UPROG_CLEAR,
    P_BANK_A, P_BANK_B, P_BANK_C, P_BANK_D, /* one button per screen bank, in place of the bank segments */
    P_MIDI_LOG, P_REVERB, P_SOUND_X, P_HUMAN, P_DIRECT,
    /* Detect: the scale that fits the notes played, applied to Key and Scale */
    P_DET_INFO, P_DET_CLEAR, P_DET_APPLY,
    /* Edit: the relative chord of the pad being edited */
    P_RELATIVE,
    /* the notes each pad plays in the bank on screen, for the pads (display only) */
    P_PADNOTES0 = P_RELATIVE + 1, P_PADNOTES_END = P_PADNOTES0 + 15,
    NUM_PARAMS
};
/* the four arpeggiator lanes: the parameter that sets each of order, rate, octaves and transpose */
static const int ARP_ORDER_IDS[EN_ARP_LANES] = {P_ARP, P_ARP2, P_ARP3, P_ARP4};
static const int ARP_RATE_IDS[EN_ARP_LANES] = {P_ARP_RATE, P_ARP2_RATE, P_ARP3_RATE, P_ARP4_RATE};
static const int ARP_OCT_IDS[EN_ARP_LANES] = {P_ARP_OCT, P_ARP2_OCT, P_ARP3_OCT, P_ARP4_OCT};
static const int ARP_TR_IDS[EN_ARP_LANES] = {P_ARP_TR, P_ARP2_TR, P_ARP3_TR, P_ARP4_TR};
static int lane_of(int i, const int *ids) {
    for (int k = 0; k < EN_ARP_LANES; k++) if (ids[k] == i) return k;
    return 0;
}

enum { OUT_OFF = 0, OUT_HOST = 1, OUT_PORT = 2, OUT_BOTH = 3 }; /* bit 0 host, bit 1 port */

static int is_display_only(int i) {
    return (i >= P_PADNOTES0 && i <= P_PADNOTES_END) || i == P_NOW || i == P_DET_INFO || i == P_INFO || i == P_PORT_STATUS || i == P_IMP_STATUS || i == P_UPRE_STATUS || i == P_FAV_STATUS || i == P_ANA_STATUS || (i >= P_PADNAME0 && i < P_SCALENOTE0 + 12);
}

typedef struct { const char *name; int steps; float def; int trigger; } ParamInfo; /* steps 0 = continuous */

static ParamInfo PINFO[NUM_PARAMS];

static void init_param_info(void) {
    static int done = 0;
    if (done) return;
#define P(i, n, s, d, t) PINFO[i].name = n; PINFO[i].steps = s; PINFO[i].def = d; PINFO[i].trigger = t;
    P(P_MODE, "Mode", VC_MODE_COUNT, 1.f / (VC_MODE_COUNT - 1), 0) /* Scale: Song mode is no longer offered */
    P(P_SONG, "Song", VC_NUM_SONGS, 0.f, 0)
    P(P_SECTION, "Section", 8, 0.f, 0)
    P(P_KEY, "Key", 12, 0.f, 0)
    P(P_SCALE, "Scale", VC_NUM_SCALES, 0.f, 0)
    P(P_TRANSPOSE, "Transpose", 25, 0.5f, 0)
    P(P_VOICING, "Voicing", VC_VOICE_COUNT, 0.f, 0)
    P(P_INVERSION, "Inversion", 4, 0.f, 0)
    P(P_OCTAVE, "Octave", 5, 0.5f, 0)
    P(P_VL, "Voice lead", 2, 1.f, 0)
    P(P_STRUM, "Strum", 0, 0.f, 0)
    P(P_STRUM_DIR, "Strum dir", STRUM_COUNT, 0.f, 0)
    P(P_ARP, "Arp", ARP_COUNT, 0.f, 0)
    P(P_ARP_RATE, "Arp rate", RATE_COUNT, 3.f / 5.f, 0)
    P(P_VEL, "Velocity", VEL_COUNT, 0.f, 0)
    P(P_SOUND, "Sound", SY_BASE_COUNT, 0.f, 0)
    P(P_TONE, "Tone", 0, 0.5f, 0)
    P(P_RELEASE, "Release", 0, 0.1f, 0)
    P(P_VOLUME, "Volume", 0, 0.75f, 0)
    P(P_PAD_BASE, "Pad 1 note", 113, 36.f / 112.f, 0)
    P(P_LEARN, "Learn pad 1", 2, 0.f, 1)
    P(P_MIDI_OUT, "MIDI out", 4, 0.f, 0)
    P(P_EDIT_PAD, "Edit pad", VC_PADS, 0.f, 0)
    P(P_EDIT_ROOT, "Edit root", 12, 0.f, 0)
    P(P_EDIT_TYPE, "Edit type", VC_NUM_QUALITIES + 1, 0.f, 0)
    P(P_EDIT_BASS, "Edit bass", 13, 0.f, 0)
    P(P_EDIT_INV, "Edit inversion", 4, 0.f, 0)
    P(P_EDIT_OCT, "Edit octave", 5, 0.5f, 0)
    P(P_EDIT_FOLLOW, "Edit follows pad", 2, 1.f, 0)
    P(P_COPY, "Copy to Custom", 2, 0.f, 1)
    P(P_EXPORT, "Export MIDI", 2, 0.f, 1)
    P(P_NOW, "Now playing", 1, 0.f, 0)
    P(P_INFO, "Info", 1, 0.f, 0)
    P(P_VIEW_BANK, "Screen bank", 4, 0.f, 0)
    P(P_ACTIVE, "Screen pad", 17, 0.f, 0)
    {
        static char names[44][20];
        static const char *NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        for (int k = 0; k < 16; k++) {
            snprintf(names[k], 20, "Pad %d chord", k + 1);
            snprintf(names[16 + k], 20, "Pad %d function", k + 1);
            P(P_PADNAME0 + k, names[k], 1, 0.f, 0)
            P(P_PADFUNC0 + k, names[16 + k], 1, 0.f, 0)
        }
        for (int k = 0; k < 12; k++) {
            snprintf(names[32 + k], 20, "Scale has %s", NOTE[k]);
            P(P_SCALENOTE0 + k, names[32 + k], 2, 0.f, 0)
        }
    }
    {
        static char pnames[16][20];
        for (int k = 0; k < 16; k++) {
            snprintf(pnames[k], 20, "Pad %d notes", k + 1);
            P(P_PADNOTES0 + k, pnames[k], 1, 0.f, 0)
        }
    }
    P(P_SONG_PREV, "Song prev", 2, 0.f, 1)
    P(P_SONG_NEXT, "Song next", 2, 0.f, 1)
    P(P_SEC_PREV, "Section prev", 2, 0.f, 1)
    P(P_SEC_NEXT, "Section next", 2, 0.f, 1)
    P(P_SCALE_PREV, "Scale prev", 2, 0.f, 1)
    P(P_SCALE_NEXT, "Scale next", 2, 0.f, 1)
    P(P_TYPE_PREV, "Edit type prev", 2, 0.f, 1)
    P(P_TYPE_NEXT, "Edit type next", 2, 0.f, 1)
    P(P_EPAD_PREV, "Edit pad prev", 2, 0.f, 1)
    P(P_EPAD_NEXT, "Edit pad next", 2, 0.f, 1)
    P(P_MIDI_CH, "MIDI channel", 16, 1.f, 0) /* 16: keeps output apart from pad input */
    P(P_PATTERN, "Pattern", EN_NUM_PATTERNS + 2, 0.f, 0)
    P(P_PAT_RATE, "Pattern rate", PRATE_COUNT, (float)PRATE_16 / (PRATE_COUNT - 1), 0)
    P(P_SWING, "Swing", 0, 0.f, 0)
    P(P_GATE, "Gate", 0, 0.8889f, 0)
    P(P_PAT_SYNC, "Pattern sync", 2, 1.f, 0)
    {
        static char stepnames[EN_STEPS][12];
        for (int k = 0; k < EN_STEPS; k++) {
            snprintf(stepnames[k], 12, "Step %d", k + 1);
            P(P_STEP0 + k, stepnames[k], 7, 0.f, 0)
        }
    }
    P(P_PAT_PREV, "Pattern prev", 2, 0.f, 1)
    P(P_PAT_NEXT, "Pattern next", 2, 0.f, 1)
    P(P_PORT_STATUS, "MIDI port", 1, 0.f, 0)
    P(P_SMART, "Smart pads", SMART_COUNT, 0.f, 0)
    P(P_SMART_MODE, "Smart notes", SMODE_COUNT, 0.f, 0)
    P(P_SMART_OCT, "Smart octave", 5, 0.5f, 0)
    P(P_HOLD, "Hold", 2, 0.f, 0)
    P(P_REPEAT, "Repeat", REP_COUNT, 0.f, 0)
    P(P_MELODY, "Melody", MEL_COUNT, 0.f, 0)
    P(P_MEL_RATE, "Melody rate", MRATE_COUNT, 0.f, 0)
    P(P_KEYSYNC, "Key sync", 2, 0.f, 0)
    P(P_IMP_FILE, "Import file", 64, 0.f, 0)
    P(P_IMP_PREV, "Import prev", 2, 0.f, 1)
    P(P_IMP_NEXT, "Import next", 2, 0.f, 1)
    P(P_IMP_LOAD, "Import load", 2, 0.f, 1)
    P(P_IMP_STATUS, "Import status", 1, 0.f, 0)
    P(P_IMP_PADS, "Pads play", 2, 0.f, 0)
    P(P_LEARN_ALL, "Learn all pads", 2, 0.f, 1)
    P(P_PRESET, "Preset", VC_NUM_PRESETS + 1, 0.f, 0)
    P(P_PRESET_PREV, "Preset prev", 2, 0.f, 1)
    P(P_PRESET_NEXT, "Preset next", 2, 0.f, 1)
    P(P_ARP_OCT, "Arp octaves", 4, 0.f, 0)
    P(P_ARP2, "Arp 2", ARP_COUNT, 0.f, 0)
    P(P_ARP3, "Arp 3", ARP_COUNT, 0.f, 0)
    P(P_ARP4, "Arp 4", ARP_COUNT, 0.f, 0)
    P(P_ARP2_RATE, "Arp 2 rate", RATE_COUNT, 3.f / 5.f, 0)
    P(P_ARP3_RATE, "Arp 3 rate", RATE_COUNT, 3.f / 5.f, 0)
    P(P_ARP4_RATE, "Arp 4 rate", RATE_COUNT, 3.f / 5.f, 0)
    P(P_ARP2_OCT, "Arp 2 octaves", 4, 0.f, 0)
    P(P_ARP3_OCT, "Arp 3 octaves", 4, 0.f, 0)
    P(P_ARP4_OCT, "Arp 4 octaves", 4, 0.f, 0)
    P(P_ARP_TR, "Arp transpose", 25, 0.5f, 0)
    P(P_ARP2_TR, "Arp 2 transpose", 25, 0.5f, 0)
    P(P_ARP3_TR, "Arp 3 transpose", 25, 0.5f, 0)
    P(P_ARP4_TR, "Arp 4 transpose", 25, 0.5f, 0)
    P(P_SHIFT, "Shift keys", 2, 1.f, 0)
    P(P_RIFF, "Riff", VC_RIFFS + 1, 0.f, 0)
    P(P_RIFF_SLOT, "Riff slot", VC_RIFFS, 0.f, 0)
    P(P_NOTE_SEL, "Edit note", 8, 0.f, 0)
    P(P_NOTE_IV, "Edit note interval", 38, 0.f, 0)
    P(P_PRESET_GENRE, "Preset genre", VC_NUM_PRESETS / VC_PRESET_VARS + 1, 0.f, 0)
    P(P_PRESET_VAR, "Preset variation", VC_PRESET_VARS, 0.f, 0)
    P(P_SMART_MAP, "Smart map", SMAP_COUNT, 0.f, 0)
    P(P_PROG, "Progression", VC_NUM_PROGRESSIONS + VC_USER_PROGS + 1, 0.f, 0)
    P(P_PROG_PLAY, "Progression play", 2, 0.f, 0)
    P(P_UPROG_SLOT, "Progression slot", VC_USER_PROGS, 0.f, 0)
    P(P_UPROG_SAVE, "Save progression", 2, 0.f, 1)
    P(P_PAT_LEN, "Pattern length", 4, 1.f, 0)
    P(P_PAT_CHANCE, "Pattern chance", 4, 1.f, 0)
    P(P_PAT_SEED, "Pattern seed", 4, 0.f, 0)
    P(P_BASS, "Bassline", BASS_COUNT, 0.f, 0)
    P(P_HARM, "Harmony", HARM_COUNT, 0.f, 0)
    P(P_UPRE_SLOT, "User preset slot", VC_USER_PRESETS, 0.f, 0)
    P(P_UPRE_SAVE, "Save user preset", 2, 0.f, 1)
    P(P_UPRE_LOAD, "Load user preset", 2, 0.f, 1)
    P(P_UPRE_EXPORT, "Export user preset", 2, 0.f, 1)
    P(P_UPRE_IMPORT, "Import user preset", 2, 0.f, 1)
    P(P_UPRE_STATUS, "User preset status", 1, 0.f, 0)
    P(P_UPRE_CLEAR, "Clear user preset", 2, 0.f, 1)
    P(P_FAV_TOGGLE, "Star preset", 2, 0.f, 1)
    P(P_FAV_PREV, "Previous starred preset", 2, 0.f, 1)
    P(P_FAV_NEXT, "Next starred preset", 2, 0.f, 1)
    P(P_FAV_STATUS, "Starred preset status", 1, 0.f, 0)
    P(P_ANALYZE, "Analyze sample", 2, 0.f, 1)
    P(P_ANA_STATUS, "Analysis status", 1, 0.f, 0)
    P(P_UPROG_CLEAR, "Clear progression", 2, 0.f, 1)
    P(P_BANK_A, "Bank A", 2, 1.f, 0)
    P(P_BANK_B, "Bank B", 2, 0.f, 0)
    P(P_BANK_C, "Bank C", 2, 0.f, 0)
    P(P_BANK_D, "Bank D", 2, 0.f, 0)
    P(P_MIDI_LOG, "MIDI log", 2, 0.f, 0)
    P(P_REVERB, "Reverb", 0, 0.25f, 0)
    P(P_SOUND_X, "Sound extra", 5, 0.f, 0)
    P(P_HUMAN, "Humanize", 0, 0.f, 0)
    P(P_DIRECT, "Direct", 2, 0.f, 0)
    P(P_DET_INFO, "Detected", 1, 0.f, 0)
    P(P_DET_CLEAR, "Clear detection", 2, 0.f, 1)
    P(P_DET_APPLY, "Use detected scale", 2, 0.f, 1)
    P(P_RELATIVE, "Relative chord", 2, 0.f, 1)
    {
        static char rifnames[EN_STEPS][16];
        for (int k = 0; k < EN_STEPS; k++) {
            snprintf(rifnames[k], 16, "Riff step %d", k + 1);
            P(P_RIFF0 + k, rifnames[k], 38, 0.f, 0)
        }
    }
#undef P
    done = 1;
}

/* MIDI log (diagnostic, Tools page): the audio thread queues each MIDI event with a ring of these,
 * and the helper thread writes them to MIDI log.txt, so the audio thread never touches a file */
#define MLOG_N 1024
typedef struct { double t; char dir; unsigned char d[3]; } MidiLogRec; /* dir: I in, O out, M main/off */

typedef struct {
    AEffect fx;
    audioMasterCallback host;
    Engine *eng;
    float val[NUM_PARAMS];
    int edit_pad;
    /* layout changes are written here and picked up by the audio thread at the next block */
    LayoutParams lp_pending;
    float upre[VC_USER_PRESETS][UPRE_MAX];
    unsigned char upre_ok[VC_USER_PRESETS];
    CustomPad custom_pending[VC_PADS];
    volatile unsigned gen, gen_applied;
    float *scratchL, *scratchR;
    int scratch_n;
    unsigned char *chunk;
    char export_msg[64];
    char upre_msg[64]; /* what the last user preset export or import did */
    unsigned char fav[32]; /* starred factory presets: bit n is preset n (1..230) */
    char ana_msg[160];     /* what the last analysis did */
    VstEvents *outEvents;
    VstMidiEvent outMidi[EN_MIDI_OUT];
    PadChord ui_pads[VC_PADS];   /* layout as seen by the screen (built on the UI thread) */
    volatile int audition_req;   /* pad index requested from the screen, -1 none */
    int audition_note, audition_left;
    int seen_pad;
    MidiPort port;
    int out_mode, flush_routes;
    int riff_sel, note_sel;     /* riff slot shown by the Riff step parameters; note shown by Edit note */
    volatile int refresh_req, refresh_quit;
    volatile int have_audio_thread, opened;
    pthread_t audio_thread;
    float trig_last[NUM_PARAMS];
    double trig_time[NUM_PARAMS];
    int refresh_running;
    pthread_t refresh_thread;
    /* MIDI import */
    ImportChord imp[VC_PADS], imp_engine[VC_PADS];
    int nimp;
    char imp_files[64][IMP_NAME];   /* the .mid files in the Import folder and its subfolders, as paths from it */
    int nfiles;
    char imp_status[96];
    unsigned key_seen;
    MidiLogRec mlog[MLOG_N];
    volatile unsigned mlog_w, mlog_r;   /* audio thread writes mlog_w, helper thread writes mlog_r */
    volatile int mlog_drop, midi_log_on;
    char mlog_path[512];
    FILE *mlog_file;                    /* only the helper thread opens, writes and closes this */
    VcReverb rv;                        /* room reverb on the built-in sound (Reverb parameter) */
    unsigned char port_defer[128];      /* a note-off held to the next block, because its note-on went out in this block */
    int direct_busy, direct_sound, direct_sound_x, direct_out; /* Direct: the sound and MIDI out to put back when it is switched off */
} Plugin;

static int idx_of(const Plugin *p, int i) {
    int s = PINFO[i].steps;
    if (s <= 1) return 0;
    int v = (int)(p->val[i] * (s - 1) + 0.5f);
    return v < 0 ? 0 : v >= s ? s - 1 : v;
}
/* the synth sound: the Sound list, or one of the four extra sounds when Sound extra is set */
static int sound_index(const Plugin *p) {
    int x = idx_of(p, P_SOUND_X);
    return x > 0 ? SY_SILENT + x : idx_of(p, P_SOUND);
}
static float norm_of(int i, int idx) { int s = PINFO[i].steps; return s <= 1 ? 0.f : (float)idx / (float)(s - 1); }
/* Saved projects from before the R&B kit was removed stored Mode as a fraction of 4 (five values: Song, Scale, R&B kit,
 * Types, Custom). R&B kit becomes Scale; Types and Custom move down one. */
/* A saved Mode. Version 10 and earlier counted the layouts with Types in the list, at position 2 (Song, Key, Types, Custom,
 * then the three note and chord modes). Types is gone: a saved Types moves to Chords, and the modes after it move up one. */
static float mode_from_saved(float v, uint32_t ver) {
    int idx;
    if (ver < 9) {
        int old = (int)(v * 4.f + 0.5f);
        if (old < 0) old = 0;
        if (old > 4) old = 4;
        static const int NEW[5] = {0, 1, 1, 2, 3}; /* R&B kit era: Song, Key, Key, Types, Custom */
        idx = NEW[old];
    } else if (ver == 9) {
        idx = (int)(v * 3.f + 0.5f);
        if (idx < 0) idx = 0;
        if (idx > 3) idx = 3;
    } else {
        idx = (int)(v * 6.f + 0.5f); /* versions 10 and 11: seven layouts */
        if (idx < 0) idx = 0;
        if (idx > 6) idx = 6;
    }
    if (ver < 12) { /* the Types-era numbering to the current one */
        if (idx == 2) idx = VC_MODE_KEY;
        else if (idx > 2) idx -= 1;
    }
    return norm_of(P_MODE, idx);
}
/* Saved projects and presets from before the longer scale list stored Scale as a fraction of 16 (17 scales). This maps
 * that value to the same scale in the current list; the built-in scales keep their indices. */
#define SCALES_BEFORE_EXTENSION 17
static float scale_from_old(float v) {
    int old = (int)(v * (float)(SCALES_BEFORE_EXTENSION - 1) + 0.5f);
    if (old < 0) old = 0;
    if (old >= SCALES_BEFORE_EXTENSION) old = SCALES_BEFORE_EXTENSION - 1;
    return norm_of(P_SCALE, old);
}
/* the four bank buttons show which screen bank is up; they follow P_VIEW_BANK whoever changed it */
static void sync_banks(Plugin *p) {
    int cur = idx_of(p, P_VIEW_BANK);
    for (int j = 0; j < 4; j++) p->val[P_BANK_A + j] = j == cur ? 1.f : 0.f;
}

static void copy_str(char *dst, const char *src, int max) {
    int k = 0;
    while (src && src[k] && k < max - 1) { dst[k] = src[k]; k++; }
    dst[k] = 0;
}

static void load_edit_fields(Plugin *p) {
    const CustomPad *c = &p->custom_pending[p->edit_pad];
    p->val[P_EDIT_PAD] = norm_of(P_EDIT_PAD, p->edit_pad);
    p->val[P_EDIT_ROOT] = norm_of(P_EDIT_ROOT, c->root % 12);
    p->val[P_EDIT_TYPE] = norm_of(P_EDIT_TYPE, c->valid ? c->quality + 1 : 0);
    p->val[P_EDIT_BASS] = norm_of(P_EDIT_BASS, c->bass == 255 ? 0 : c->bass % 12 + 1);
    p->val[P_EDIT_INV] = norm_of(P_EDIT_INV, c->inv);
    p->val[P_EDIT_OCT] = norm_of(P_EDIT_OCT, c->oct + 2);
}

static void sync_layout(Plugin *p) {
    LayoutParams *lp = &p->lp_pending;
    lp->mode = idx_of(p, P_MODE);
    lp->song = idx_of(p, P_SONG);
    lp->section = idx_of(p, P_SECTION);
    lp->key = idx_of(p, P_KEY);
    lp->scale = idx_of(p, P_SCALE);
    lp->transpose = idx_of(p, P_TRANSPOSE) - 12;
    lp->prog = idx_of(p, P_PROG);
    lp->voicing = idx_of(p, P_VOICING);
    lp->inversion = idx_of(p, P_INVERSION);
    lp->octave = idx_of(p, P_OCTAVE) - 2;
    lp->voice_lead = idx_of(p, P_VL);
    lp->imp = p->imp;
    lp->nimp = p->nimp;
    if (idx_of(p, P_IMP_PADS) && p->nimp > 0) lp->mode = VC_MODE_IMPORT;
    vc_build_pads(lp, p->custom_pending, p->ui_pads);
    p->gen++;
}

/* save the 16 chords of bank A (the layout as the mode makes it, not a progression) into a user slot */
static void save_user_prog(Plugin *p, int slot) {
    static PadChord pads[VC_PADS];
    LayoutParams lp = p->lp_pending;
    lp.prog = 0;
    vc_build_pads(&lp, p->custom_pending, pads);
    for (int k = 0; k < VC_PROG_MAX; k++) {
        int ok = pads[k].valid && pads[k].spec.quality >= 0 && pads[k].spec.quality < VC_NUM_QUALITIES;
        p->lp_pending.ur[slot][k] = ok ? (unsigned char)pads[k].spec.root : 0;
        p->lp_pending.uq[slot][k] = ok ? (signed char)pads[k].spec.quality : -1; /* -1: an empty step */
    }
    p->lp_pending.un[slot] = VC_PROG_MAX;
    sync_layout(p);
}

/* Screen refresh. Never call the host from here: this runs on the audio thread (pad hits, automation), and a
 * host that redraws inside that call stalls the audio for every track. Just raise a flag; a background thread
 * (refresh_loop) tells the host at most every 40 ms. */
static void refresh_host(Plugin *p) {
    /* From the screen or the host's UI thread: tell the host right away, like any plugin (the MPC may ignore
     * requests from other threads, which froze the screen and one-tap buttons). From the audio thread (pad hits,
     * automation): only raise a flag for the background helper, so the audio is never held up. */
    if (p->opened && p->host && !(p->have_audio_thread && pthread_equal(pthread_self(), p->audio_thread))) {
        p->refresh_req = 0;
        p->host(&p->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.f);
    } else p->refresh_req = 1;
}

static double mono_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void mlog_push(Plugin *p, char dir, const unsigned char *d) {
    if (!p->midi_log_on) return;
    unsigned w = p->mlog_w;
    if (w - p->mlog_r >= MLOG_N) { p->mlog_drop++; return; }
    MidiLogRec *r = &p->mlog[w % MLOG_N];
    r->t = mono_seconds(); r->dir = dir; r->d[0] = d[0]; r->d[1] = d[1]; r->d[2] = d[2];
    __sync_synchronize();
    p->mlog_w = w + 1;
}

static void mlog_service(Plugin *p) {
    if (p->midi_log_on && !p->mlog_file) {
        p->mlog_file = fopen(p->mlog_path, "a");
        if (p->mlog_file) fprintf(p->mlog_file, "--- log started ---\n");
    }
    FILE *f = p->mlog_file;
    if (!f) { p->mlog_r = p->mlog_w; return; }   /* not logging: drop what was queued */
    while (p->mlog_r != p->mlog_w) {
        const MidiLogRec *r = &p->mlog[p->mlog_r % MLOG_N];
        fprintf(f, "%.6f %c %02X %02X %02X\n", r->t, r->dir, r->d[0], r->d[1], r->d[2]);
        __sync_synchronize();
        p->mlog_r = p->mlog_r + 1;
    }
    if (p->mlog_drop) { fprintf(f, "--- %d events dropped: the log could not keep up ---\n", p->mlog_drop); p->mlog_drop = 0; }
    fflush(f);
    if (!p->midi_log_on) { fclose(f); p->mlog_file = 0; }
}

typedef int (*pthread_create_fn)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
typedef int (*pthread_join_fn)(pthread_t, void **);

static void *refresh_loop(void *arg) {
    Plugin *p = (Plugin *)arg;
    while (!p->refresh_quit) {
        struct timespec ts = {0, 40 * 1000000L};
        nanosleep(&ts, 0);
        mlog_service(p);
        if (p->refresh_req && !p->refresh_quit) {
            p->refresh_req = 0;
            if (p->host) p->host(&p->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.f);
        }
    }
    if (p->mlog_file) { fclose(p->mlog_file); p->mlog_file = 0; }
    return 0;
}

static void start_refresh_thread(Plugin *p) {
    /* looked up at runtime: the host process always has threads, and this keeps the link free of libpthread */
    pthread_create_fn create = (pthread_create_fn)dlsym(RTLD_DEFAULT, "pthread_create");
    if (create && create(&p->refresh_thread, 0, refresh_loop, p) == 0) p->refresh_running = 1;
}

static void stop_refresh_thread(Plugin *p) {
    if (!p->refresh_running) return;
    p->refresh_quit = 1;
    pthread_join_fn join = (pthread_join_fn)dlsym(RTLD_DEFAULT, "pthread_join");
    if (join) join(p->refresh_thread, 0);
    else { struct timespec ts = {0, 100 * 1000000L}; nanosleep(&ts, 0); } /* let the loop see refresh_quit */
    p->refresh_running = 0;
}

/* pattern: 0 = off, 1..N = presets, N+1 = custom (the 16 Step parameters) */
static char shown_step(const Plugin *p, int k) {
    int pat = idx_of(p, P_PATTERN);
    if (pat >= 1 && pat <= EN_NUM_PATTERNS) return EN_PATTERNS[pat - 1].steps[k];
    return EN_STEP_CHARS[idx_of(p, P_STEP0 + k)];
}

static void apply_pattern(Plugin *p) {
    Engine *e = p->eng;
    for (int k = 0; k < EN_STEPS; k++) e->pat[k] = shown_step(p, k);
    e->pattern_on = idx_of(p, P_PATTERN) != 0 || e->riff != 0; /* a riff plays on the same grid */
}

static void apply_out_mode(Plugin *p, int mode) {
    Engine *e = p->eng;
    if (mode == p->out_mode) return;
    if (p->out_mode != OUT_OFF) { e->midi_out_flush = 1; p->flush_routes = p->out_mode; } /* silence the old routes */
    if (mode & OUT_PORT) vc_port_open(&p->port, "Velvet Chords", "Velvet Chords Out");
    else if (vc_port_is_open(&p->port) && !(p->flush_routes & OUT_PORT)) vc_port_close(&p->port);
    p->out_mode = mode;
    e->midi_out = mode != OUT_OFF;
}

/* move a stepped parameter one step (prev/next buttons on the screen) */
static void set_param(Plugin *p, int i, float v, int from_host);
/* Relative chord: the pad being edited becomes its relative chord (C -> Am, Am -> C); chords without one stay as they are */
static void apply_relative(Plugin *p) {
    ChordSpec c, r;
    c.root = idx_of(p, P_EDIT_ROOT);
    c.quality = idx_of(p, P_EDIT_TYPE) - 1;
    c.bass = -1; c.ncustom = 0;
    if (c.quality < 0 || !vc_relative(&c, &r)) return;
    set_param(p, P_EDIT_ROOT, norm_of(P_EDIT_ROOT, r.root), 0);
    set_param(p, P_EDIT_TYPE, norm_of(P_EDIT_TYPE, r.quality + 1), 0);
    set_param(p, P_EDIT_BASS, norm_of(P_EDIT_BASS, 0), 0);   /* a relative chord has no slash bass */
    refresh_host(p);
}

static int popcount_of(unsigned m) { int n = 0; for (m &= 0xFFF; m; m &= m - 1) n++; return n; }

/* Detect: Key and Scale become the scale that holds every note played; if none holds them all, the one that holds most */
static void apply_detected(Plugin *p) {
    DetectHit h[1];
    if (!vc_detect(p->eng->det_pcs, 1, h, 1) && !vc_detect(p->eng->det_pcs, 0, h, 1)) return;
    set_param(p, P_KEY, norm_of(P_KEY, h[0].key), 0);
    set_param(p, P_SCALE, norm_of(P_SCALE, h[0].scale), 0);
    set_param(p, P_MODE, norm_of(P_MODE, VC_MODE_KEY), 0);
    refresh_host(p);
}

/* Direct: one tap silences the built-in sound and sends the chords out of the Velvet Chords Out port, for a track with
 * its own sound. Tapping it again puts back the sound and MIDI out that were set before. */
static void apply_direct(Plugin *p, int on) {
    p->direct_busy = 1;
    if (on) {
        p->direct_sound = idx_of(p, P_SOUND);
        p->direct_sound_x = idx_of(p, P_SOUND_X);
        p->direct_out = idx_of(p, P_MIDI_OUT);
        set_param(p, P_SOUND_X, 0.f, 0);
        set_param(p, P_SOUND, norm_of(P_SOUND, SY_SILENT), 0);
        set_param(p, P_MIDI_OUT, norm_of(P_MIDI_OUT, OUT_PORT), 0);
    } else {
        set_param(p, P_SOUND, norm_of(P_SOUND, p->direct_sound), 0);
        set_param(p, P_SOUND_X, norm_of(P_SOUND_X, p->direct_sound_x), 0);
        set_param(p, P_MIDI_OUT, norm_of(P_MIDI_OUT, p->direct_out), 0);
    }
    p->direct_busy = 0;
    refresh_host(p);
}

static void step_param(Plugin *p, int i, int dir, int count) {
    if (count < 1) count = 1;
    int x = idx_of(p, i) + dir;
    if (x < 0) x = count - 1;
    if (x >= count) x = 0;
    set_param(p, i, norm_of(i, x), 0);
}

/* Presets: 0 = Manual (nothing changes), 1..N = VC_PRESETS. Only performance settings are set; the chords, key,
 * scale, song, custom pads and the Tools settings stay as they are. */
/* User presets: your own settings (sound, voicing, strum, arpeggio, pattern with its steps, smart pads, key, scale, bass, harmony,
 * progression), saved into a slot and recalled later. Unlike a factory preset, a user preset restores everything it holds,
 * including Silent and an arpeggio, because you saved them. */
static const int UPRE_IDS[] = {
    P_SOUND, P_TONE, P_RELEASE, P_VOLUME, P_VOICING, P_INVERSION, P_OCTAVE, P_VL, P_STRUM, P_STRUM_DIR,
    P_ARP, P_ARP_RATE, P_ARP_OCT, P_ARP_TR, P_ARP2, P_ARP2_RATE, P_ARP2_OCT, P_ARP2_TR,
    P_ARP3, P_ARP3_RATE, P_ARP3_OCT, P_ARP3_TR, P_ARP4, P_ARP4_RATE, P_ARP4_OCT, P_ARP4_TR,
    P_VEL, P_PATTERN, P_PAT_RATE, P_SWING, P_GATE, P_PAT_LEN, P_PAT_CHANCE, P_PAT_SEED,
    P_SMART, P_SMART_MODE, P_SMART_OCT, P_SMART_MAP, P_RIFF, P_KEY, P_SCALE, P_BASS, P_HARM, P_PROG, P_PROG_PLAY,
};
#define UPRE_NIDS ((int)(sizeof(UPRE_IDS) / sizeof(UPRE_IDS[0])))
typedef char upre_fits[(UPRE_NIDS + EN_STEPS) <= UPRE_MAX ? 1 : -1];

static void save_user_preset(Plugin *p, int slot) {
    if (slot < 0 || slot >= VC_USER_PRESETS) return;
    for (int k = 0; k < UPRE_NIDS; k++) p->upre[slot][k] = p->val[UPRE_IDS[k]];
    for (int j = 0; j < EN_STEPS; j++) p->upre[slot][UPRE_NIDS + j] = p->val[P_STEP0 + j];
    p->upre_ok[slot] = 1;
}

static void load_user_preset(Plugin *p, int slot) {
    if (slot < 0 || slot >= VC_USER_PRESETS || !p->upre_ok[slot]) return;
    for (int k = 0; k < UPRE_NIDS; k++) set_param(p, UPRE_IDS[k], p->upre[slot][k], 0);
    for (int j = 0; j < EN_STEPS; j++) set_param(p, P_STEP0 + j, p->upre[slot][UPRE_NIDS + j], 0); /* after the pattern, so the saved steps win */
}

/* Preset files: plain text, one "name=value" line per setting after the header "VELVET-PRESET 1". The names are the plugin's own
 * parameter names and the values its normalised 0..1 values, so a file stays readable and survives added settings: an import skips names
 * it doesn't know and keeps the default for any setting the file doesn't mention. Out-of-range or non-numeric values are skipped. */
static const char *export_dir(void);

/* a saved Pattern: before version 11 it counted the fifteen original rhythms (Custom was the sixteenth value); the genre
 * rhythms come after them, and Custom moves to the end */
static float pattern_from_saved(float v) {
    int old = (int)(v * 16.f + 0.5f);
    if (old < 0) old = 0;
    if (old > 16) old = 16;
    return norm_of(P_PATTERN, old == 16 ? EN_NUM_PATTERNS + 1 : old);
}

/* where Pattern sits in a user preset (its index in the saved settings) */
static int upre_pattern_slot(void) { for (int k = 0; k < UPRE_NIDS; k++) if (UPRE_IDS[k] == P_PATTERN) return k; return -1; }

/* where Scale sits in a user preset (its index in the saved settings) */
static int upre_scale_slot(void) { for (int k = 0; k < UPRE_NIDS; k++) if (UPRE_IDS[k] == P_SCALE) return k; return -1; }
static const char *upre_name(int k) { return k < UPRE_NIDS ? PINFO[UPRE_IDS[k]].name : PINFO[P_STEP0 + (k - UPRE_NIDS)].name; }

static void upre_path(char *out, size_t n, int slot) { snprintf(out, n, "%s/user-preset-%d.vcpreset", export_dir(), slot + 1); }

static void export_user_preset(Plugin *p, int slot) {
    if (slot < 0 || slot >= VC_USER_PRESETS) return;
    if (!p->upre_ok[slot]) { snprintf(p->upre_msg, sizeof(p->upre_msg), "Slot %d is empty: Save first", slot + 1); return; }
    char path[512];
    mkdir(export_dir(), 0775);
    upre_path(path, sizeof(path), slot);
    FILE *f = fopen(path, "w");
    if (!f) { snprintf(p->upre_msg, sizeof(p->upre_msg), "Could not write the file"); return; }
    fprintf(f, "VELVET-PRESET 3\n# Velvet Chords user preset. One setting per line: name=value, with value 0 to 1 (the plugin's own parameter values).\n");
    for (int k = 0; k < UPRE_NIDS + EN_STEPS; k++) fprintf(f, "%s=%.6f\n", upre_name(k), (double)p->upre[slot][k]);
    int ok = fclose(f) == 0;
    if (ok) snprintf(p->upre_msg, sizeof(p->upre_msg), "Exported slot %d", slot + 1);
    else snprintf(p->upre_msg, sizeof(p->upre_msg), "Could not write the file");
}

static void import_user_preset(Plugin *p, int slot) {
    if (slot < 0 || slot >= VC_USER_PRESETS) return;
    char path[512], line[160];
    upre_path(path, sizeof(path), slot);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(p->upre_msg, sizeof(p->upre_msg), "No file for slot %d", slot + 1); return; }
    if (!fgets(line, sizeof(line), f) || strncmp(line, "VELVET-PRESET ", 14)) {
        fclose(f); snprintf(p->upre_msg, sizeof(p->upre_msg), "Not a Velvet preset file"); return;
    }
    int file_ver = atoi(line + 14);
    if (file_ver < 1 || file_ver > 3) { fclose(f); snprintf(p->upre_msg, sizeof(p->upre_msg), "Made by a newer version"); return; }
    float tmp[UPRE_MAX];
    for (int k = 0; k < UPRE_NIDS; k++) tmp[k] = PINFO[UPRE_IDS[k]].def;
    for (int j = 0; j < EN_STEPS; j++) tmp[UPRE_NIDS + j] = norm_of(P_STEP0 + j, en_step_index('.'));
    int applied = 0, skipped = 0;
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (line[0] == '#' || line[0] == '\n' || !eq) continue;
        *eq = 0;
        char *end = 0;
        double v = strtod(eq + 1, &end);
        int k = -1;
        for (int i = 0; i < UPRE_NIDS + EN_STEPS && k < 0; i++) if (!strcmp(upre_name(i), line)) k = i;
        if (k < 0) continue; /* a setting this version doesn't have */
        if (end == eq + 1 || !(v >= 0.0 && v <= 1.0)) { skipped++; continue; } /* also rejects NaN */
        if (file_ver < 2 && k == upre_scale_slot()) v = scale_from_old((float)v);   /* version 1 files: Scale is out of 16 */
        if (file_ver < 3 && k == upre_pattern_slot()) v = pattern_from_saved((float)v); /* before version 3: the old rhythm list */
        tmp[k] = (float)v;
        applied++;
    }
    fclose(f);
    if (!applied) { snprintf(p->upre_msg, sizeof(p->upre_msg), "No settings found in the file"); return; }
    memcpy(p->upre[slot], tmp, sizeof(float) * (size_t)(UPRE_NIDS + EN_STEPS));
    p->upre_ok[slot] = 1;
    if (skipped) snprintf(p->upre_msg, sizeof(p->upre_msg), "Imported %d settings, skipped %d", applied, skipped);
    else snprintf(p->upre_msg, sizeof(p->upre_msg), "Imported %d settings into slot %d", applied, slot + 1);
}

/* Starred presets: star the ones you like, then step through only those. The stars are stored with the project. */
static int fav_get(const Plugin *p, int x) { return x >= 1 && x < 256 && ((p->fav[x >> 3] >> (x & 7)) & 1); }

static int fav_count(const Plugin *p) {
    int n = 0;
    for (int x = 1; x <= VC_NUM_PRESETS && x < 256; x++) n += fav_get(p, x);
    return n;
}

static void fav_toggle(Plugin *p) {
    int x = idx_of(p, P_PRESET);
    if (x < 1 || x > VC_NUM_PRESETS || x >= 256) return;
    p->fav[x >> 3] ^= (unsigned char)(1u << (x & 7));
}

static void fav_step(Plugin *p, int dir) { /* the next starred preset after the current one, wrapping; nothing if none are starred */
    int x = idx_of(p, P_PRESET);
    for (int n = 1; n <= VC_NUM_PRESETS; n++) {
        int y = ((x - 1 + dir * n) % VC_NUM_PRESETS + VC_NUM_PRESETS) % VC_NUM_PRESETS + 1;
        if (fav_get(p, y)) { set_param(p, P_PRESET, norm_of(P_PRESET, y), 0); return; }
    }
}

/* Sample analysis: reads Velvet Chords/Analyze/sample.wav, finds its chords, and writes them into the chosen progression slot
 * (which must be empty; Clear progression first). The result is a user progression, so it plays and maps like any other. */
static void analyze_sample(Plugin *p, int slot) {
    if (slot < 0 || slot >= VC_USER_PROGS) return;
    if (p->lp_pending.un[slot]) { snprintf(p->ana_msg, sizeof(p->ana_msg), "Progression %d holds chords: Clear progression first", slot + 1); return; }
    char dir[512], path[512], msg[96];
    snprintf(dir, sizeof(dir), "%s/Analyze", export_dir());
    mkdir(dir, 0775); /* made on first use, so the folder is there to copy a sample into */
    snprintf(path, sizeof(path), "%s/sample.wav", dir);
    char found[160] = "sample.wav";
    if (access(path, R_OK) != 0) { /* no sample.wav: use the first WAV in the folder, so a renamed export still works */
        DIR *d = opendir(dir);
        struct dirent *de;
        while (d && (de = readdir(d)) != NULL) {
            size_t len = strlen(de->d_name);
            if (len > 4 && !strcasecmp(de->d_name + len - 4, ".wav")) {
                snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
                snprintf(found, sizeof(found), "%s", de->d_name);
                break;
            }
        }
        if (d) closedir(d);
    }
    float *mono = 0;
    int n = 0, rate = 0;
    if (va_read_wav(path, &mono, &n, &rate, msg, sizeof(msg)) != 0) {
        if (!strcmp(msg, "Could not open the file")) snprintf(p->ana_msg, sizeof(p->ana_msg), "No WAV yet: put a .wav file in Velvet Chords/Analyze");
        else snprintf(p->ana_msg, sizeof(p->ana_msg), "Read failed (%s): %s", found, msg);
        return;
    }
    VaResult *r = (VaResult *)malloc(sizeof(VaResult));
    if (!r) { free(mono); snprintf(p->ana_msg, sizeof(p->ana_msg), "Out of memory"); return; }
    int rc = va_analyze(mono, n, rate, r, msg, sizeof(msg));
    free(mono);
    if (rc != 0 || r->nseg == 0) { snprintf(p->ana_msg, sizeof(p->ana_msg), "No chords found in the sample"); free(r); return; }
    for (int k = 0; k < VC_PROG_MAX; k++) {
        if (k < r->nseg) { p->lp_pending.ur[slot][k] = (unsigned char)r->seg[k].root; p->lp_pending.uq[slot][k] = (signed char)r->seg[k].quality; }
        else { p->lp_pending.ur[slot][k] = 0; p->lp_pending.uq[slot][k] = -1; }
    }
    p->lp_pending.un[slot] = VC_PROG_MAX;
    snprintf(p->ana_msg, sizeof(p->ana_msg), "%d chords in progression %d, key %s%s (%d%% sure)%s", r->nseg, slot + 1,
             vc_pc_name(r->key, 0), r->minor ? "m" : "", (int)(r->key_conf * 100 + 0.5f), r->truncated ? ", first 120 s" : "");
    free(r);
    sync_layout(p);
}

static const char *preset_label(int x) { return x == 0 ? "Manual" : VC_PRESETS[x - 1].name; }

static void apply_preset(Plugin *p, int x) {
    if (x < 1 || x > VC_NUM_PRESETS) return;
    const VcPreset *s = &VC_PRESETS[x - 1];
    set_param(p, P_KEY, norm_of(P_KEY, s->key), 0); /* the genre's key and scale; the layout follows, custom pads stay */
    set_param(p, P_SCALE, norm_of(P_SCALE, s->scale), 0);
    /* a preset never picks the instrument: the Sound and Sound extra you chose stay as they are (s->sound is for reference) */
    /* the arpeggio stays as it is unless you already have one running (then the preset sets its order, rate and octaves) */
    set_param(p, P_TONE, s->tone, 0);
    set_param(p, P_RELEASE, s->release, 0);
    set_param(p, P_VOLUME, s->volume, 0);
    set_param(p, P_VOICING, norm_of(P_VOICING, s->voicing), 0);
    set_param(p, P_INVERSION, norm_of(P_INVERSION, s->inversion), 0);
    set_param(p, P_OCTAVE, norm_of(P_OCTAVE, s->octave + 2), 0);
    set_param(p, P_VL, norm_of(P_VL, s->voice_lead), 0);
    set_param(p, P_STRUM, s->strum_ms / 150.f, 0);
    set_param(p, P_STRUM_DIR, norm_of(P_STRUM_DIR, s->strum_dir), 0);
    if (en_arp_on(p->eng)) {
        set_param(p, P_ARP, norm_of(P_ARP, s->arp), 0);
        set_param(p, P_ARP_RATE, norm_of(P_ARP_RATE, s->arp_rate), 0);
        set_param(p, P_ARP_OCT, norm_of(P_ARP_OCT, s->arp_oct - 1), 0);
        set_param(p, P_ARP2, 0.f, 0); /* the extra lanes go off: a preset sets the arpeggio itself */
        set_param(p, P_ARP3, 0.f, 0);
        set_param(p, P_ARP4, 0.f, 0);
    }
    set_param(p, P_VEL, norm_of(P_VEL, s->vel), 0);
    /* the rhythm is yours: a preset never switches a pattern on (it would play the chord as a run, like an arpeggio), so
     * its pattern, rate, swing and gate (s->pattern, s->pat_rate, s->swing, s->gate) stay in the data for reference only */
    set_param(p, P_SMART, norm_of(P_SMART, s->smart), 0);
    set_param(p, P_SMART_MODE, norm_of(P_SMART_MODE, s->smart_mode), 0);
    set_param(p, P_SMART_OCT, norm_of(P_SMART_OCT, s->smart_oct + 2), 0);
    /* the riff is yours: a preset never turns it on or off (s->melody is kept in the data for reference only) */
}

/* the preset list is genre by variation: the genre knob sets the group, the variation knob the preset in it */
static void sync_preset_knobs(Plugin *p, int x) {
    if (x < 1) { p->val[P_PRESET_GENRE] = 0.f; return; } /* Manual: the variation knob keeps its place */
    p->val[P_PRESET_GENRE] = norm_of(P_PRESET_GENRE, (x - 1) / VC_PRESET_VARS + 1);
    p->val[P_PRESET_VAR] = norm_of(P_PRESET_VAR, (x - 1) % VC_PRESET_VARS);
}
static void preset_from_knobs(Plugin *p, int from_host) {
    int g = idx_of(p, P_PRESET_GENRE);
    int x = g == 0 ? 0 : (g - 1) * VC_PRESET_VARS + idx_of(p, P_PRESET_VAR) + 1;
    set_param(p, P_PRESET, norm_of(P_PRESET, x), 0);
    if (from_host) refresh_host(p);
}

/* riffs: 0 = Off, 1..6 = the riff in slot; the 16 steps shown are the slot chosen in Riff slot */
static void load_riff_fields(Plugin *p) {
    for (int k = 0; k < EN_STEPS; k++) {
        int x = p->eng->rif[p->riff_sel][k];
        p->val[P_RIFF0 + k] = norm_of(P_RIFF0 + k, x == RIFF_REST ? 0 : x + 13);
    }
}

/* the Edit note interval shows the selected note of the edited pad (0 = none / whole chord) */
static void load_note_field(Plugin *p) {
    const CustomPad *c = &p->custom_pending[p->edit_pad];
    int v = 0;
    if (p->note_sel > 0 && p->note_sel < c->nnotes) v = c->notes[p->note_sel] + 13;
    p->val[P_NOTE_IV] = norm_of(P_NOTE_IV, v);
}

/* the note list starts from the chord as named, the first time a note is edited */
static void notes_from_chord(CustomPad *c) {
    if (!c->valid || c->quality >= VC_NUM_QUALITIES) return;
    ChordSpec s;
    memset(&s, 0, sizeof(s));
    s.root = c->root % 12;
    s.quality = c->quality;
    s.bass = -1;
    int iv[VC_MAX_IV], n = vc_chord_intervals(&s, iv);
    if (n > VC_MAX_IV) n = VC_MAX_IV;
    c->nnotes = (unsigned char)n;
    for (int i = 0; i < n; i++) c->notes[i] = (signed char)iv[i];
}

/* Edit note interval: sets, adds or removes the selected note (0 = Remove; 1..37 = -12..+24 semitones) */
static void set_note_interval(Plugin *p, int x) {
    if (p->note_sel <= 0) return; /* the whole chord is edited with Edit type / root / bass */
    CustomPad *c = &p->custom_pending[p->edit_pad];
    if (c->nnotes < 2) notes_from_chord(c);
    int s = p->note_sel;
    if (s < c->nnotes) {
        if (x == 0) { /* remove: the notes above move down */
            for (int i = s; i + 1 < c->nnotes; i++) c->notes[i] = c->notes[i + 1];
            c->nnotes--;
        } else c->notes[s] = (signed char)(x - 13);
    } else if (s == c->nnotes && x != 0 && c->nnotes < VC_MAX_IV) {
        c->notes[c->nnotes++] = (signed char)(x - 13); /* the next free slot */
    }
    if (c->nnotes < 2) c->nnotes = 0; /* only the root is left: back to the named chord */
    p->val[P_MODE] = norm_of(P_MODE, VC_MODE_CUSTOM); /* edits are heard in Custom mode */
    load_note_field(p);
    sync_layout(p);
}

/* Shift keys (MIDI notes in the octave below the pads), applied on the host thread after each block */
static void apply_shift(Plugin *p) {
    Engine *e = p->eng;
    for (int a = 0; a < SHIFT_COUNT; a++) {
        while (e->shift_pending[a] > 0) {
            e->shift_pending[a]--;
            if (a == SHIFT_KEY_DOWN) step_param(p, P_KEY, -1, 12);
            else if (a == SHIFT_KEY_UP) step_param(p, P_KEY, 1, 12);
            else if (a == SHIFT_SCALE_PREV) step_param(p, P_SCALE, -1, VC_NUM_SCALES);
            else if (a == SHIFT_SCALE_NEXT) step_param(p, P_SCALE, 1, VC_NUM_SCALES);
            else step_param(p, P_INVERSION, 1, 4);
            refresh_host(p);
        }
    }
}

static void build_local(const Plugin *p, PadChord *pads, Voicing *v) {
    vc_build_pads(&p->lp_pending, p->custom_pending, pads);
    vc_voice_pads(&p->lp_pending, pads, v);
}

static void do_copy_to_custom(Plugin *p) {
    static PadChord pads[VC_PADS];
    static Voicing v[VC_PADS];
    build_local(p, pads, v);
    int addOct = 0;
    for (int i = 0; i < VC_PADS; i++) {
        CustomPad *c = &p->custom_pending[i];
        memset(c, 0, sizeof(*c));
        c->bass = 255;
        if (!pads[i].valid || pads[i].spec.quality < 0) continue; /* scale-stacked chords without a name are skipped */
        c->valid = 1;
        c->root = (unsigned char)pads[i].spec.root;
        c->quality = (unsigned char)pads[i].spec.quality;
        c->bass = pads[i].spec.bass < 0 ? 255 : (unsigned char)pads[i].spec.bass;
        c->oct = (signed char)(pads[i].oct + addOct);
        c->inv = (signed char)pads[i].inv;
    }
    p->val[P_MODE] = norm_of(P_MODE, VC_MODE_CUSTOM);
    load_edit_fields(p);
    sync_layout(p);
}

static const char *export_dir(void) {
    const char *d = getenv("VC_EXPORT_DIR");
    if (d && *d) return d;
    /* MPC OS internal drive (where MPC.settings lives); falls back to /sdcard like the plugin itself.
     * mkdir+errno instead of stat(): stat() binds to GLIBC_2.33 on new toolchains. */
    static const char *internal = "/media/az01-internal/Velvet Chords";
    if (mkdir(internal, 0775) == 0 || errno == EEXIST) return internal;
    return "/sdcard/Velvet Chords";
}

static const char *import_dir(void) {
    static char d[300];
    snprintf(d, sizeof(d), "%s/Import", export_dir());
    return d;
}

static int ends_with_mid(const char *n) {
    size_t l = strlen(n);
    const char *e = l >= 4 ? n + l - 4 : "";
    const char *e5 = l >= 5 ? n + l - 5 : "";
    return (!strcmp(e, ".mid") || !strcmp(e, ".MID") || !strcmp(e, ".Mid") || !strcmp(e5, ".midi") || !strcmp(e5, ".MIDI"));
}

/* walks the Import folder and its subfolders (up to IMP_MAX_DEPTH levels) and collects every .mid file by its path from
 * Import, such as "Songs/blues.mid". Links and hidden entries are skipped. The folder order is arbitrary, so the walk
 * collects up to IMP_SCAN_MAX files, then the list is sorted and the first 64 are kept: a big library never hides the
 * files that sort first. */
#define IMP_SCAN_MAX 512
static char imp_scan[IMP_SCAN_MAX][IMP_NAME];
static int imp_scan_n;

static void scan_import_dir(const char *rel, int depth) {
    char path[600];
    if (rel[0]) snprintf(path, sizeof(path), "%s/%s", import_dir(), rel); else copy_str(path, import_dir(), sizeof(path));
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && imp_scan_n < IMP_SCAN_MAX) {
        if (de->d_name[0] == '.') continue;
        char child_rel[IMP_NAME], child[600];
        if (rel[0]) snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name);
        else copy_str(child_rel, de->d_name, sizeof(child_rel));
        if (strlen(child_rel) >= IMP_NAME - 1) continue;
        snprintf(child, sizeof(child), "%s/%s", import_dir(), child_rel);
        /* no stat() here: a folder is one that opens, a file is one with a .mid name (stat is not safe on this libc) */
        DIR *sub = opendir(child);
        if (sub) {
            closedir(sub);
            if (depth < IMP_MAX_DEPTH) scan_import_dir(child_rel, depth + 1);
        } else if (ends_with_mid(de->d_name)) copy_str(imp_scan[imp_scan_n++], child_rel, IMP_NAME);
    }
    closedir(d);
}

static int cmp_import_names(const void *x, const void *y) { return strcmp((const char *)x, (const char *)y); }

static void scan_import_files(Plugin *p) {
    mkdir(export_dir(), 0775);
    mkdir(import_dir(), 0775);
    imp_scan_n = 0;
    scan_import_dir("", 0);
    if (imp_scan_n == 0) { /* say where it looked, so an empty list can be traced */
        DIR *d = opendir(import_dir());
        int entries = 0;
        if (d) { while (readdir(d)) entries++; closedir(d); }
        snprintf(p->imp_status, sizeof(p->imp_status), "Checked %s: %d entries, no .mid", import_dir(), entries);
    }
    qsort(imp_scan, (size_t)imp_scan_n, IMP_NAME, cmp_import_names); /* alphabetical, so a folder's files sit together */
    p->nfiles = imp_scan_n < 64 ? imp_scan_n : 64;
    for (int i = 0; i < p->nfiles; i++) copy_str(p->imp_files[i], imp_scan[i], IMP_NAME);
}

static void do_import(Plugin *p) {
    scan_import_files(p);
    int i = idx_of(p, P_IMP_FILE);
    if (p->nfiles == 0) { snprintf(p->imp_status, sizeof(p->imp_status), "No .mid files in Velvet Chords/Import"); return; }
    if (i >= p->nfiles) i = p->nfiles - 1;
    char path[400], err[96];
    snprintf(path, sizeof(path), "%s/%s", import_dir(), p->imp_files[i]);
    static ImportChord tmp[VC_PADS];
    int n = vc_midi_import(path, tmp, VC_PADS, err, sizeof(err));
    if (n < 0) { snprintf(p->imp_status, sizeof(p->imp_status), "%s", err); return; }
    memcpy(p->imp, tmp, sizeof(ImportChord) * (size_t)n);
    p->nimp = n;
    snprintf(p->imp_status, sizeof(p->imp_status), "Loaded %d chord%s from %s", n, n == 1 ? "" : "s", p->imp_files[i]);
    p->val[P_IMP_PADS] = 1.f; /* show them on the pads */
    sync_layout(p);
}

static void do_export(Plugin *p, double hostTempo) {
    static PadChord pads[VC_PADS];
    static Voicing v[VC_PADS];
    static MfChord ch[256];
    char fname[160], path[512], text[700], title[160];
    const char *dir = export_dir();
    mkdir(dir, 0775);
    LayoutParams *lp = &p->lp_pending;
    int ok = 0, n = 0;
    build_local(p, pads, v);
    if (lp->mode == VC_MODE_SONG && lp->song < VC_NUM_SONGS) {
        const Song *s = &VC_SONGS[lp->song];
        int sec = vc_song_section_index(lp, 0);
        if (sec >= 0) {
            n = vc_section_chords(s, sec, lp, ch, 256);
            snprintf(title, sizeof(title), "%s - %s - %s%s", s->artist, s->title, s->sections[sec].name, lp->transpose ? " (transposed)" : "");
            vc_safe_filename(title, fname, sizeof(fname));
            vc_provenance_text(s, lp, text, sizeof(text));
            double bpm = s->bpm > 0 ? s->bpm : hostTempo;
            snprintf(path, sizeof(path), "%s/%s.mid", dir, fname);
            ok = vc_midi_write(path, title, text, ch, n, bpm, s->ts_num, s->ts_den);
            int pn = vc_bank_chords(pads, v, 0, ch, 16);
            snprintf(path, sizeof(path), "%s/%s.progression", dir, fname);
            ok = ok && vc_progression_write(path, title, vc_pc_name(s->key_root + lp->transpose, vc_song_flats(s, lp->transpose)), ch, pn);
        }
    } else {
        Engine *e = p->eng;
        int bank = e->last_pad >= 0 ? e->last_pad / 16 : 0;
        snprintf(title, sizeof(title), "Velvet Chords - %s - %s %s - Bank %c", VC_MODE_NAMES[lp->mode], vc_pc_name(lp->key, 0),
                 VC_SCALES[lp->scale].name, 'A' + bank);
        vc_safe_filename(title, fname, sizeof(fname));
        n = vc_bank_chords(pads, v, bank, ch, 16);
        snprintf(path, sizeof(path), "%s/%s.mid", dir, fname);
        ok = n > 0 && vc_midi_write(path, title, "Velvet Chords pad bank, one chord per bar. Original/stylistic chords, not song data.", ch, n, hostTempo, 4, 4);
        snprintf(path, sizeof(path), "%s/%s.progression", dir, fname);
        ok = ok && vc_progression_write(path, title, vc_pc_name(lp->key, 0), ch, n);
    }
    if (ok) snprintf(p->export_msg, sizeof(p->export_msg), "Saved %d chords", n);
    else snprintf(p->export_msg, sizeof(p->export_msg), "Save failed: %s", dir);
}

enum { kVstPpqPosValid = 1 << 9 };
static double host_tempo_pos(Plugin *p, double *ppq, int *playing) {
    if (ppq) *ppq = 0.0;
    if (playing) *playing = 0;
    if (!p->host) return 90.0;
    VstTimeInfo *ti = (VstTimeInfo *)p->host(&p->fx, audioMasterGetTime, 0, kVstTempoValid | kVstPpqPosValid, 0, 0.f);
    if (!ti) return 90.0;
    if (ppq && (ti->flags & kVstPpqPosValid)) *ppq = ti->ppqPos;
    if (playing) *playing = (ti->flags & kVstTransportPlaying) && (ti->flags & kVstPpqPosValid) ? 1 : 0;
    if ((ti->flags & kVstTempoValid) && ti->tempo > 20.0 && ti->tempo < 400.0) return ti->tempo;
    return 90.0;
}
static double host_tempo(Plugin *p) { return host_tempo_pos(p, 0, 0); }

static void set_param(Plugin *p, int i, float v, int from_host) {
    if (i < 0 || i >= NUM_PARAMS) return;
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    Engine *e = p->eng;
    if (PINFO[i].trigger) {
        /* a tap counts whether the host sends 1 again, or flips its cached 1 back to 0 (hosts that didn't re-read
         * our reset); a 0 right after a 1 is the release of the same tap and is ignored */
        float prev = p->trig_last[i];
        p->trig_last[i] = v;
        int press = v >= 0.5f;
        if (!press && prev >= 0.5f && mono_seconds() - p->trig_time[i] > 0.25) press = 1;
        if (press) p->trig_time[i] = mono_seconds();
        if (press) {
            if (i == P_LEARN) { e->learn = 1; e->learn_all = 0; p->val[i] = 1.f; return; }
            if (i == P_LEARN_ALL) {
                memset(e->pad_map, -1, sizeof(e->pad_map));
                e->map_set = 0; e->learn = 0; e->learn_all = 1;
                p->val[i] = 0.f;
                refresh_host(p);
                return;
            }
            if (i == P_COPY) do_copy_to_custom(p);
            if (i == P_UPROG_SAVE) save_user_prog(p, idx_of(p, P_UPROG_SLOT));
            if (i == P_UPRE_SAVE) save_user_preset(p, idx_of(p, P_UPRE_SLOT));
            if (i == P_UPRE_LOAD) load_user_preset(p, idx_of(p, P_UPRE_SLOT));
            if (i == P_UPRE_EXPORT) export_user_preset(p, idx_of(p, P_UPRE_SLOT));
            if (i == P_UPRE_IMPORT) import_user_preset(p, idx_of(p, P_UPRE_SLOT));
            if (i == P_UPRE_CLEAR) { int sl = idx_of(p, P_UPRE_SLOT); p->upre_ok[sl] = 0; snprintf(p->upre_msg, sizeof(p->upre_msg), "Cleared slot %d", sl + 1); }
            if (i == P_FAV_TOGGLE) fav_toggle(p);
            if (i == P_DET_CLEAR) e->det_pcs = 0;
            if (i == P_DET_APPLY) apply_detected(p);
            if (i == P_RELATIVE) apply_relative(p);
            if (i == P_ANALYZE) analyze_sample(p, idx_of(p, P_UPROG_SLOT));
            if (i == P_UPROG_CLEAR) { int sl = idx_of(p, P_UPROG_SLOT); if (sl >= 0 && sl < VC_USER_PROGS) { p->lp_pending.un[sl] = 0; snprintf(p->ana_msg, sizeof(p->ana_msg), "Cleared progression %d", sl + 1); sync_layout(p); } }
            if (i == P_FAV_PREV) fav_step(p, -1);
            if (i == P_FAV_NEXT) fav_step(p, 1);
            if (i == P_EXPORT) do_export(p, host_tempo(p));
            if (i == P_SONG_PREV || i == P_SONG_NEXT) {
                step_param(p, P_SONG, i == P_SONG_NEXT ? 1 : -1, VC_NUM_SONGS);
                set_param(p, P_SECTION, 0.f, 0);
            }
            if (i == P_SEC_PREV || i == P_SEC_NEXT) {
                int n = VC_SONGS[idx_of(p, P_SONG)].nsections;
                int cur = idx_of(p, P_SECTION);
                if (cur >= n) cur = n - 1;
                cur += (i == P_SEC_NEXT) ? 1 : -1;
                if (cur < 0) cur = n - 1;
                if (cur >= n) cur = 0;
                set_param(p, P_SECTION, norm_of(P_SECTION, cur), 0);
            }
            if (i == P_SCALE_PREV || i == P_SCALE_NEXT) step_param(p, P_SCALE, i == P_SCALE_NEXT ? 1 : -1, VC_NUM_SCALES);
            if (i == P_PAT_PREV || i == P_PAT_NEXT) step_param(p, P_PATTERN, i == P_PAT_NEXT ? 1 : -1, EN_NUM_PATTERNS + 2);
            if (i == P_IMP_PREV || i == P_IMP_NEXT) {
                scan_import_files(p);
                if (p->nfiles > 0) step_param(p, P_IMP_FILE, i == P_IMP_NEXT ? 1 : -1, p->nfiles);
            }
            if (i == P_IMP_LOAD) do_import(p);
            if (i == P_TYPE_PREV || i == P_TYPE_NEXT) step_param(p, P_EDIT_TYPE, i == P_TYPE_NEXT ? 1 : -1, VC_NUM_QUALITIES + 1);
            if (i == P_EPAD_PREV || i == P_EPAD_NEXT) {
                step_param(p, P_EDIT_PAD, i == P_EPAD_NEXT ? 1 : -1, VC_PADS);
                p->val[P_VIEW_BANK] = norm_of(P_VIEW_BANK, p->edit_pad / 16);
                sync_banks(p);
            }
            if (i == P_TYPE_PREV || i == P_TYPE_NEXT) { p->val[P_MODE] = norm_of(P_MODE, VC_MODE_CUSTOM); sync_layout(p); }
            if (i == P_PRESET_PREV || i == P_PRESET_NEXT) step_param(p, P_PRESET, i == P_PRESET_NEXT ? 1 : -1, VC_NUM_PRESETS + 1);
            if (i != P_COPY && i != P_EXPORT && i != P_LEARN) refresh_host(p);
            if (i == P_IMP_LOAD) refresh_host(p);
        }
        p->val[i] = 0.f;
        return;
    }
    if (is_display_only(i)) return;
    if (i >= P_BANK_A && i <= P_BANK_D) { /* pressing a bank shows it; pressing the one already up does nothing */
        if (from_host && v >= 0.5f) set_param(p, P_VIEW_BANK, norm_of(P_VIEW_BANK, i - P_BANK_A), 1);
        sync_banks(p);
        refresh_host(p);
        return;
    }
    if (i == P_ACTIVE) { /* tapping a pad on the screen auditions it */
        p->val[i] = v;
        int k = idx_of(p, i);
        if (from_host && k > 0) p->audition_req = idx_of(p, P_VIEW_BANK) * 16 + k - 1;
        return;
    }
    if (i == P_VIEW_BANK) { p->val[i] = v; p->val[P_ACTIVE] = 0.f; sync_banks(p); refresh_host(p); return; }
    /* a preset overwrites the sound and everything else, so only a change of the knob applies it:
     * a host that re-sends its stored values (on a mode or song change, say) must not undo your edits */
    if ((i == P_PRESET || i == P_PRESET_GENRE || i == P_PRESET_VAR || i == P_PROG) && p->val[i] == v) return;
    if (i == P_DIRECT) { /* acts on a change only: a host that re-sends the stored value must not switch again */
        int on = v >= 0.5f;
        if (on != (p->val[i] >= 0.5f)) apply_direct(p, on);
    }
    /* a manual change of the sound or MIDI out takes Direct off, so switching it off later can't undo that choice */
    if ((i == P_SOUND || i == P_SOUND_X || i == P_MIDI_OUT) && !p->direct_busy && p->val[P_DIRECT] >= 0.5f) p->val[P_DIRECT] = 0.f;
    p->val[i] = v;
    switch (i) {
    case P_MIDI_LOG:
        p->midi_log_on = idx_of(p, i);
        if (p->midi_log_on) snprintf(p->mlog_path, sizeof(p->mlog_path), "%s/MIDI log.txt", export_dir());
        break;
    case P_MODE: case P_SONG: case P_SECTION: case P_KEY: case P_SCALE: case P_TRANSPOSE:
    case P_VOICING: case P_INVERSION: case P_OCTAVE: case P_VL:
        /* picking a mode, song, section, key or scale shows that layout again (not the imported chords) */
        if (i == P_MODE || i == P_SONG || i == P_SECTION || i == P_KEY || i == P_SCALE) p->val[P_IMP_PADS] = 0.f;
        sync_layout(p);
        if ((i == P_KEY || i == P_SCALE) && from_host && !idx_of(p, P_KEYSYNC)) { /* share with copies that follow */
            g_vc_key.key = idx_of(p, P_KEY); g_vc_key.scale = idx_of(p, P_SCALE);
            g_vc_key.source = e->instance_id; g_vc_key.version++;
        }
        /* the host redraws the pads: the new key, scale or mode is on the screen at once, like the MPC's Pad Perform */
        if (from_host) refresh_host(p);
        break;
    case P_STRUM: e->strum_ms = v * 150.f; break;
    case P_STRUM_DIR: e->strum_dir = idx_of(p, i); break;
    case P_ARP: case P_ARP2: case P_ARP3: case P_ARP4: e->lanes[lane_of(i, ARP_ORDER_IDS)].order = idx_of(p, i); break;
    case P_ARP_RATE: case P_ARP2_RATE: case P_ARP3_RATE: case P_ARP4_RATE: e->lanes[lane_of(i, ARP_RATE_IDS)].rate = idx_of(p, i); break;
    case P_ARP_OCT: case P_ARP2_OCT: case P_ARP3_OCT: case P_ARP4_OCT: e->lanes[lane_of(i, ARP_OCT_IDS)].oct = idx_of(p, i) + 1; break;
    case P_ARP_TR: case P_ARP2_TR: case P_ARP3_TR: case P_ARP4_TR: e->lanes[lane_of(i, ARP_TR_IDS)].transpose = idx_of(p, i) - 12; break;
    case P_VEL: e->vel_curve = idx_of(p, i); break;
    case P_SOUND:  /* a base sound picked on the screen replaces an extra one */
        if (from_host && idx_of(p, P_SOUND_X)) set_param(p, P_SOUND_X, 0.f, 0);
        e->synth.sound = sound_index(p); break;
    case P_SOUND_X: e->synth.sound = sound_index(p); break;
    case P_HUMAN: e->humanize = v; break;
    case P_TONE: e->synth.tone = v; break;
    case P_RELEASE: e->synth.release = v; break;
    case P_VOLUME: e->synth.volume = v; break;
    case P_PAD_BASE: e->pad_base = idx_of(p, i); break;
    case P_MIDI_OUT: apply_out_mode(p, idx_of(p, i)); if (from_host) refresh_host(p); break;
    case P_MIDI_CH: e->out_channel = idx_of(p, i); break;
    case P_PATTERN: apply_pattern(p); if (from_host) refresh_host(p); break;
    case P_PAT_RATE: e->pat_rate = idx_of(p, i); break;
    case P_SWING: e->swing = v; break;
    case P_GATE: e->gate = 0.1f + 0.9f * v; break;
    case P_PAT_SYNC: e->pat_sync = idx_of(p, i); break;
    case P_SMART: e->smart = idx_of(p, i); e->dirty = 1; if (from_host) refresh_host(p); break;
    case P_HOLD: e->hold = idx_of(p, i); break;
    case P_REPEAT: e->repeat = idx_of(p, i); break;
    case P_MELODY: e->mel_mode = idx_of(p, i); break;
    case P_MEL_RATE: e->mel_rate = idx_of(p, i); break;
    case P_KEYSYNC: e->dirty = 1; p->key_seen = 0; break; /* adopt the shared key at the next block */
    case P_IMP_FILE: refresh_host(p); break;
    case P_IMP_PADS: sync_layout(p); refresh_host(p); break;
    case P_SMART_MODE: e->smart_mode = idx_of(p, i); e->dirty = 1; if (from_host) refresh_host(p); break;
    case P_SMART_OCT: e->smart_oct = idx_of(p, i) - 2; e->dirty = 1; if (from_host) refresh_host(p); break;
    case P_SMART_MAP: e->smart_map = idx_of(p, i); e->dirty = 1; if (from_host) refresh_host(p); break;
    case P_PROG_PLAY: e->prog_play = idx_of(p, i); if (from_host) refresh_host(p); break;
    case P_HARM: e->harmony = idx_of(p, i); if (from_host) refresh_host(p); break;
    case P_BASS: break;   /* the bassline style was removed; its slot stays so saved projects keep their place */
    case P_PAT_LEN: e->pat_len = (idx_of(p, i) + 1) * 4; e->dirty = 1; if (from_host) refresh_host(p); break;
    case P_PAT_CHANCE: e->pat_chance = (idx_of(p, i) + 1) * 25; e->dirty = 1; if (from_host) refresh_host(p); break;
    case P_PAT_SEED: e->pat_seed = idx_of(p, i) + 1; e->dirty = 1; if (from_host) refresh_host(p); break;
    case P_PROG: if (idx_of(p, i) != 0) p->val[P_IMP_PADS] = 0.f; /* choosing a progression replaces imported pads; Off doesn't */
        sync_layout(p); if (from_host) refresh_host(p); break;
    case P_PRESET: apply_preset(p, idx_of(p, i)); sync_preset_knobs(p, idx_of(p, i)); if (from_host) refresh_host(p); break;
    case P_PRESET_GENRE: case P_PRESET_VAR: preset_from_knobs(p, from_host); break;
    case P_SHIFT: e->shift_keys = idx_of(p, i); break;
    case P_RIFF: e->riff = idx_of(p, i); apply_pattern(p); if (from_host) refresh_host(p); break;
    case P_RIFF_SLOT: p->riff_sel = idx_of(p, i); load_riff_fields(p); if (from_host) refresh_host(p); break;
    case P_NOTE_SEL: p->note_sel = idx_of(p, i); load_note_field(p); if (from_host) refresh_host(p); break;
    case P_NOTE_IV: set_note_interval(p, idx_of(p, i)); if (from_host) refresh_host(p); break;
    case P_EDIT_FOLLOW: e->edit_follow = idx_of(p, i); break;
    default:
        if (i >= P_RIFF0 && i <= P_RIFF_END) { /* a riff step: a rest, or an offset from the chord root */
            int x = idx_of(p, i);
            p->eng->rif[p->riff_sel][i - P_RIFF0] = (signed char)(x == 0 ? RIFF_REST : x - 13);
            break;
        }
        if (i >= P_STEP0 && i < P_STEP0 + EN_STEPS) {
            int pat = idx_of(p, P_PATTERN);
            if (from_host && pat != EN_NUM_PATTERNS + 1) { /* editing a preset (or Off) turns it into your Custom pattern */
                char cur[EN_STEPS];
                for (int k = 0; k < EN_STEPS; k++) cur[k] = shown_step(p, k);
                float keep = v;
                for (int k = 0; k < EN_STEPS; k++) p->val[P_STEP0 + k] = norm_of(P_STEP0 + k, en_step_index(cur[k]));
                p->val[i] = keep;
                p->val[P_PATTERN] = norm_of(P_PATTERN, EN_NUM_PATTERNS + 1);
                refresh_host(p);
            }
            apply_pattern(p);
        }
        break;
    case P_EDIT_PAD:
        p->edit_pad = idx_of(p, i);
        load_edit_fields(p);
        if (from_host) refresh_host(p);
        break;
    case P_EDIT_ROOT: case P_EDIT_TYPE: case P_EDIT_BASS: case P_EDIT_INV: case P_EDIT_OCT: {
        CustomPad *c = &p->custom_pending[p->edit_pad];
        if (i == P_EDIT_ROOT || i == P_EDIT_TYPE || i == P_EDIT_BASS) { c->nnotes = 0; load_note_field(p); } /* a new chord replaces the note list */
        int t = idx_of(p, P_EDIT_TYPE);
        c->valid = t > 0;
        c->quality = (unsigned char)(t > 0 ? t - 1 : 0);
        c->root = (unsigned char)idx_of(p, P_EDIT_ROOT);
        int b = idx_of(p, P_EDIT_BASS);
        c->bass = b == 0 ? 255 : (unsigned char)(b - 1);
        c->inv = (signed char)idx_of(p, P_EDIT_INV);
        c->oct = (signed char)(idx_of(p, P_EDIT_OCT) - 2);
        if (from_host) p->val[P_MODE] = norm_of(P_MODE, VC_MODE_CUSTOM); /* edits are heard in Custom mode */
        sync_layout(p);
        break;
    }
    }
}

/* ---------- display text ---------- */

/* what a smart note is, relative to the chord it follows */
static const char *smart_degree(const Engine *e, int note) {
    static const char *DEG[12] = {"root", "b9", "9th", "b3", "3rd", "11th", "#11", "5th", "b13", "6th", "b7", "7th"};
    const SmartChord *s = &e->smart_src;
    int pc = note % 12, in_chord = 0;
    for (int i = 0; i < s->npcs; i++) if (s->pcs[i] == pc) in_chord = 1;
    int iv = (pc - s->root + 12) % 12;
    if (in_chord && iv == 3 && s->npcs >= 3) return "m3";
    if (in_chord && iv == 8) return "#5";
    if (in_chord && iv == 9 && s->npcs >= 4) { int has7 = 0; for (int i = 0; i < s->npcs; i++) if ((s->pcs[i] - s->root + 12) % 12 == 10 || (s->pcs[i] - s->root + 12) % 12 == 11) has7 = 1; if (!has7) return "6th"; return "13th"; }
    return in_chord ? DEG[iv] : (iv == 2 ? "scale 9" : iv == 5 ? "scale 11" : iv == 9 ? "scale 13" : "scale");
}

static int getParameterValueInScale(const Plugin *p, int pc) {
    const Scale *sc = &VC_SCALES[idx_of(p, P_SCALE)];
    int key = idx_of(p, P_KEY);
    for (int j = 0; j < sc->n; j++) if ((key + sc->iv[j]) % 12 == pc) return 1;
    return 0;
}

static void pad_label(int idx, char *buf) { buf[0] = (char)('A' + idx / 16); buf[1] = (char)('0' + (idx % 16 + 1) / 10); buf[2] = (char)('0' + (idx % 16 + 1) % 10); buf[3] = 0; }

static int view_bank_of(const Plugin *p) { return idx_of(p, P_VIEW_BANK); }

static void param_display(Plugin *p, int i, char *out, int max) {
    char b[160];
    b[0] = 0;
    int x = idx_of(p, i);
    Engine *e = p->eng;
    if (i >= P_PADNOTES0 && i <= P_PADNOTES_END) { /* the notes this pad plays, from the layout the screen shows */
        static Voicing v[VC_PADS];
        int k = view_bank_of(p) * 16 + (i - P_PADNOTES0);
        const LayoutParams *lp = &p->lp_pending;
        vc_voice_pads(lp, p->ui_pads, v);
        int flats = vc_prefer_flats(lp->key, vc_scale_is_minor(lp->scale));
        b[0] = 0;
        int si = en_smart_index(e, k);
        if (si >= 0) {   /* a smart pad plays one note: the one it is set to */
            vc_note_name(e->smart_notes[si], e->smart_src.flats, b, sizeof(b));
        } else if (p->ui_pads[k].valid) {
            for (int j = 0; j < v[k].n; j++) {
                char nn[8];
                vc_note_name(v[k].notes[j], flats, nn, sizeof(nn));
                size_t used = strlen(b);
                snprintf(b + used, sizeof(b) - used, "%s%s", j ? " " : "", nn);
            }
        }
        copy_str(out, b, max);
        return;
    }
    if (i >= P_RIFF0 && i <= P_RIFF_END) { /* riff step: Rest, or semitones from the chord root */
        if (x == 0) copy_str(out, "Rest", max); else snprintf(out, (size_t)max, "%+d st", x - 13);
        return;
    }
    const LayoutParams *lp = &p->lp_pending;
    static const char *KEYS[12] = {"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
    static const char *INV[4] = {"Root", "1st", "2nd", "3rd"};
    static const char *ONOFF[2] = {"Off", "On"};
    static const char *SDIR[STRUM_COUNT] = {"Up", "Down", "Alternate"};
    static const char *ARPS[ARP_COUNT] = {"Off", "Up", "Down", "Up-down", "Random", "Converge", "Diverge"};
    static const char *RATES[RATE_COUNT] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
    static const char *VELS[VEL_COUNT] = {"Linear", "Soft", "Hard", "Fixed"};
    switch (i) {
    case P_MODE: copy_str(b, VC_MODE_NAMES[x], sizeof(b)); break;
    case P_SONG:
        if (!strcmp(VC_SONGS[x].status, "original")) { /* "Title - original (style)" with the style before " (original" */
            const char *g = VC_SONGS[x].genre, *cut = strstr(g, " (");
            snprintf(b, sizeof(b), "%s - original (%.*s)", VC_SONGS[x].title, cut ? (int)(cut - g) : (int)strlen(g), g);
        }
        else snprintf(b, sizeof(b), "%s - %s (%d)", VC_SONGS[x].title, VC_SONGS[x].artist, VC_SONGS[x].year);
        break;
    case P_SECTION: {
        const Song *s = &VC_SONGS[lp->song];
        int si = vc_song_section_index(lp, 0);
        if (si < 0) copy_str(b, "-", sizeof(b));
        else snprintf(b, sizeof(b), "%s (%d/%d)", s->sections[si].name, si + 1, s->nsections);
        break;
    }
    case P_KEY: copy_str(b, KEYS[x], sizeof(b)); break;
    case P_SCALE: copy_str(b, VC_SCALES[x].name, sizeof(b)); break;
    case P_TRANSPOSE: snprintf(b, sizeof(b), "%+d st", x - 12); break;
    case P_VOICING: copy_str(b, VC_VOICE_NAMES[x], sizeof(b)); break;
    case P_INVERSION: case P_EDIT_INV: copy_str(b, INV[x], sizeof(b)); break;
    case P_OCTAVE: case P_EDIT_OCT: snprintf(b, sizeof(b), "%+d", x - 2); break;
    case P_VL: case P_EDIT_FOLLOW: copy_str(b, ONOFF[x], sizeof(b)); break;
    case P_MIDI_OUT: { static const char *M[4] = {"Off", "To MPC host", "Virtual port", "Host + port"}; copy_str(b, M[x], sizeof(b)); break; }
    case P_MIDI_CH: snprintf(b, sizeof(b), "Ch %d", x + 1); break;
    case P_PATTERN: copy_str(b, x == 0 ? "Off" : x <= EN_NUM_PATTERNS ? EN_PATTERNS[x - 1].name : "Custom", sizeof(b)); break;
    case P_PAT_RATE: { static const char *PR[PRATE_COUNT] = {"1/8", "1/8T", "1/16", "1/16T", "1/32"}; copy_str(b, PR[x], sizeof(b)); break; }
    case P_SWING: snprintf(b, sizeof(b), "%d%%", (int)(p->val[i] * 100.f + 0.5f)); break;
    case P_GATE: snprintf(b, sizeof(b), "%d%%", (int)((0.1f + 0.9f * p->val[i]) * 100.f + 0.5f)); break;
    case P_PAT_SYNC: copy_str(b, x ? "Follow MPC" : "Free", sizeof(b)); break;
    case P_PAT_PREV: copy_str(b, "<", sizeof(b)); break;
    case P_PAT_NEXT: copy_str(b, ">", sizeof(b)); break;
    case P_SMART: { static const char *SM[SMART_COUNT] = {"Off", "Pads 9-16", "Follow other"}; copy_str(b, SM[x], sizeof(b)); break; }
    case P_SMART_MODE: { static const char *SN[SMODE_COUNT] = {"Chord tones", "Scale", "Bass"}; copy_str(b, SN[x], sizeof(b)); break; }
    case P_SMART_OCT: snprintf(b, sizeof(b), "%+d", x - 2); break;
    case P_PROG: {
        if (x == 0) copy_str(b, "Off", sizeof(b));
        else if (x <= VC_NUM_PROGRESSIONS) copy_str(b, VC_PROGRESSIONS[x - 1].name, sizeof(b));
        else { int slot = x - VC_NUM_PROGRESSIONS - 1; int nn = p->lp_pending.un[slot];
            if (nn) snprintf(b, sizeof(b), "User %d (saved)", slot + 1); else snprintf(b, sizeof(b), "User %d (empty)", slot + 1); }
        break;
    }
    case P_UPROG_SLOT: snprintf(b, sizeof(b), "Slot %d", x + 1); break;
    case P_ANA_STATUS: copy_str(b, p->ana_msg[0] ? p->ana_msg : "Put a WAV in Velvet Chords/Analyze/sample.wav, then Analyze", sizeof(b)); break;
    case P_FAV_STATUS: {
        int cur = idx_of(p, P_PRESET), nf = fav_count(p);
        if (cur < 1) snprintf(b, sizeof(b), "Manual. %d starred", nf);
        else snprintf(b, sizeof(b), "%s. %d starred", fav_get(p, cur) ? "Starred" : "Not starred", nf);
        break;
    }
    case P_UPRE_STATUS: copy_str(b, p->upre_msg[0] ? p->upre_msg : "Export writes a file you can copy", sizeof(b)); break;
    case P_UPRE_SLOT: snprintf(b, sizeof(b), "Slot %d (%s)", x + 1, p->upre_ok[x] ? "saved" : "empty"); break;
    case P_PAT_LEN: snprintf(b, sizeof(b), "%d steps", (x + 1) * 4); break;
    case P_PAT_CHANCE: snprintf(b, sizeof(b), "%d%%", (x + 1) * 25); break;
    case P_PAT_SEED: snprintf(b, sizeof(b), "Seed %d", x + 1); break;
    case P_HARM: { static const char *HN[HARM_COUNT] = {"Off", "Third up", "Sixth up", "Third down", "Octave up"}; copy_str(b, HN[x], sizeof(b)); break; }
    case P_BASS: { static const char *BN[BASS_COUNT] = {"Off", "Root", "Root + fifth", "Octaves", "Walking"}; copy_str(b, BN[x], sizeof(b)); break; }
    case P_PROG_PLAY: copy_str(b, x ? "On: one chord per bar" : "Off", sizeof(b)); break;
    case P_SMART_MAP: { static const char *MN[SMAP_COUNT] = {"Follow mode", "Relative", "Absolute", "Chromatic"}; copy_str(b, MN[x], sizeof(b)); break; }
    case P_PRESET: copy_str(b, preset_label(x), sizeof(b)); break;
    case P_PRESET_GENRE: copy_str(b, x == 0 ? "Manual" : VC_PRESETS[(x - 1) * VC_PRESET_VARS].genre, sizeof(b)); break;
    case P_PRESET_VAR: {
        int g = idx_of(p, P_PRESET_GENRE);
        if (g == 0) { copy_str(b, "-", sizeof(b)); break; }
        const VcPreset *s = &VC_PRESETS[(g - 1) * VC_PRESET_VARS + x]; /* the variation is the name after the genre */
        copy_str(b, s->name + strlen(s->genre) + 1, sizeof(b));
        break;
    }
    case P_ARP_OCT: case P_ARP2_OCT: case P_ARP3_OCT: case P_ARP4_OCT: snprintf(b, sizeof(b), "%d oct", x + 1); break;
    case P_ARP2: case P_ARP3: case P_ARP4: copy_str(b, ARPS[x], sizeof(b)); break;
    case P_ARP2_RATE: case P_ARP3_RATE: case P_ARP4_RATE: copy_str(b, RATES[x], sizeof(b)); break;
    case P_ARP_TR: case P_ARP2_TR: case P_ARP3_TR: case P_ARP4_TR: snprintf(b, sizeof(b), "%+d st", x - 12); break;
    case P_SHIFT: copy_str(b, ONOFF[x], sizeof(b)); break;
    case P_RIFF: copy_str(b, x == 0 ? "Off" : EN_RIFF_NAMES[x - 1], sizeof(b)); break;
    case P_RIFF_SLOT: copy_str(b, EN_RIFF_NAMES[x], sizeof(b)); break;
    case P_NOTE_SEL: if (x == 0) copy_str(b, "Whole chord", sizeof(b)); else snprintf(b, sizeof(b), "Note %d", x + 1); break;
    case P_NOTE_IV: if (x == 0) copy_str(b, "Remove", sizeof(b)); else snprintf(b, sizeof(b), "%+d st", x - 13); break;
    case P_HOLD: case P_DIRECT: copy_str(b, x ? "On" : "Off", sizeof(b)); break;
    case P_REPEAT: { static const char *R[REP_COUNT] = {"Off", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"}; copy_str(b, R[x], sizeof(b)); break; }
    case P_MELODY: { static const char *M2[MEL_COUNT] = {"Off", "Simple", "Busy"}; copy_str(b, M2[x], sizeof(b)); break; }
    case P_MEL_RATE: { static const char *M3[MRATE_COUNT] = {"1/8", "1/8T", "1/16"}; copy_str(b, M3[x], sizeof(b)); break; }
    case P_KEYSYNC: copy_str(b, x ? "Follow other copy" : "Off", sizeof(b)); break;
    case P_IMP_FILE: copy_str(b, p->nfiles ? p->imp_files[x < p->nfiles ? x : p->nfiles - 1] : "No .mid files in Velvet Chords/Import", sizeof(b)); break;
    case P_IMP_PREV: copy_str(b, "<", sizeof(b)); break;
    case P_IMP_NEXT: copy_str(b, ">", sizeof(b)); break;
    case P_IMP_LOAD: copy_str(b, "Load chords", sizeof(b)); break;
    case P_IMP_STATUS: copy_str(b, p->imp_status[0] ? p->imp_status : (p->nimp ? "Imported chords ready" : "Put .mid files in Velvet Chords/Import"), sizeof(b)); break;
    case P_IMP_PADS: copy_str(b, x && p->nimp ? "Imported chords" : (x ? "Imported (none loaded)" : "Layout"), sizeof(b)); break;
    case P_PORT_STATUS: copy_str(b, (p->out_mode & OUT_PORT) || vc_port_is_open(&p->port) ? p->port.status : "Virtual port off", sizeof(b)); break;
    case P_STRUM: if (p->val[i] < 0.004f) copy_str(b, "Off", sizeof(b)); else snprintf(b, sizeof(b), "%d ms", (int)(p->val[i] * 150.f + 0.5f)); break;
    case P_STRUM_DIR: copy_str(b, SDIR[x], sizeof(b)); break;
    case P_ARP: copy_str(b, ARPS[x], sizeof(b)); break;
    case P_ARP_RATE: copy_str(b, RATES[x], sizeof(b)); break;
    case P_VEL: copy_str(b, VELS[x], sizeof(b)); break;
    case P_SOUND: copy_str(b, SY_SOUND_NAMES[x], sizeof(b)); break;
    case P_SOUND_X: copy_str(b, x == 0 ? "-" : SY_SOUND_NAMES[SY_SILENT + x], sizeof(b)); break;
    case P_TONE: case P_RELEASE: case P_VOLUME: case P_REVERB: case P_HUMAN: snprintf(b, sizeof(b), "%d%%", (int)(p->val[i] * 100.f + 0.5f)); break;
    case P_PAD_BASE:
        if (e->map_set) { copy_str(b, "Learned layout", sizeof(b)); break; }
        { char nn[8]; x = e->pad_base; vc_note_name(x, 0, nn, sizeof(nn)); snprintf(b, sizeof(b), "%s (%d)", nn, x); }
        break;
    case P_LEARN: copy_str(b, e->learn ? "Hit pad 1 now" : "Press to learn", sizeof(b)); break;
    case P_LEARN_ALL:
        if (e->learn_all > 0) snprintf(b, sizeof(b), "Hit pad %d of 16", e->learn_all);
        else copy_str(b, e->map_set ? "Pads learned" : "Learn all 16 pads", sizeof(b));
        break;
    case P_EDIT_PAD: pad_label(x, b); break;
    case P_EDIT_ROOT: copy_str(b, KEYS[x], sizeof(b)); break;
    case P_EDIT_TYPE:
        if (x == 0) copy_str(b, "Empty", sizeof(b));
        else snprintf(b, sizeof(b), "%s (%s)", VC_QUALITIES[x - 1].suffix[0] ? VC_QUALITIES[x - 1].suffix : "maj", VC_QUALITIES[x - 1].label);
        break;
    case P_EDIT_BASS: if (x == 0) copy_str(b, "Root", sizeof(b)); else snprintf(b, sizeof(b), "/%s", KEYS[x - 1]); break;
    case P_COPY: copy_str(b, "Press to copy", sizeof(b)); break;
    case P_EXPORT: copy_str(b, p->export_msg[0] ? p->export_msg : "Press to save", sizeof(b)); break;
    case P_DET_INFO: {
        DetectHit h[1];
        if (e->det_pcs == 0) { copy_str(b, "Play notes to detect a scale", sizeof(b)); break; }
        if (!vc_detect(e->det_pcs, 0, h, 1)) { copy_str(b, "No scale fits these notes", sizeof(b)); break; }
        snprintf(b, sizeof(b), "%s %s: %d of %d notes", vc_pc_name(h[0].key, 0), VC_SCALES[h[0].scale].name, h[0].matches, popcount_of(e->det_pcs));
        break;
    }
    case P_NOW: {
        if (e->learn_all > 0) { snprintf(b, sizeof(b), "Learning: hit MPC pad %d of 16", e->learn_all); break; }
        int k = e->last_pad;
        if (k < 0 || k >= VC_PADS) {
            if (e->last_note_in < 0) copy_str(b, "Play a pad", sizeof(b));
            else if (e->map_set) snprintf(b, sizeof(b), "Note %d isn't one of the learned pads", e->last_note_in);
            else snprintf(b, sizeof(b), "Note %d isn't a chord pad (Pad 1 note is %d)", e->last_note_in, e->pad_base);
            break;
        }
        char lab[4];
        pad_label(k, lab);
        if (en_smart_index(e, k) >= 0) {
            int note = e->smart_notes[en_smart_index(e, k)];
            char nn[8];
            vc_note_name(note, e->smart_src.flats, nn, sizeof(nn));
            snprintf(b, sizeof(b), "%s %s: %s of %s", lab, nn, smart_degree(e, note), e->smart_src.name);
            break;
        }
        if (!e->pads[k].valid) { snprintf(b, sizeof(b), "%s empty", lab); break; }
        int n = e->pads[k].roman[0] ? snprintf(b, sizeof(b), "%s %s (%s):", lab, e->pads[k].name, e->pads[k].roman)
                                     : snprintf(b, sizeof(b), "%s %s:", lab, e->pads[k].name);
        int fl = lp->mode == VC_MODE_SONG ? vc_song_flats(&VC_SONGS[lp->song], lp->transpose) : vc_prefer_flats(lp->key, vc_scale_is_minor(lp->scale));
        for (int j = 0; j < e->voicings[k].n && n < (int)sizeof(b) - 6; j++) {
            char nn[8];
            vc_note_name(e->voicings[k].notes[j], fl, nn, sizeof(nn));
            n += snprintf(b + n, sizeof(b) - (size_t)n, " %s", nn);
        }
        break;
    }
    case P_VIEW_BANK: b[0] = (char)('A' + x); b[1] = 0; break;
    case P_ACTIVE: if (x == 0) copy_str(b, "-", sizeof(b)); else { pad_label(idx_of(p, P_VIEW_BANK) * 16 + x - 1, b); } break;
    case P_SONG_PREV: case P_SEC_PREV: case P_SCALE_PREV: case P_TYPE_PREV: case P_EPAD_PREV: copy_str(b, "<", sizeof(b)); break;
    case P_SONG_NEXT: case P_SEC_NEXT: case P_SCALE_NEXT: case P_TYPE_NEXT: case P_EPAD_NEXT: copy_str(b, ">", sizeof(b)); break;
    case P_INFO: {
        if (lp->mode == VC_MODE_SONG) {
            const Song *s = &VC_SONGS[lp->song];
            if (!strcmp(s->status, "original")) snprintf(b, sizeof(b), "Original progression, %s, %d BPM", s->key, (int)(s->bpm + 0.5f));
            else snprintf(b, sizeof(b), "%s, confidence %s, key %s", s->status, s->confidence, s->key);
        } else {
            const Scale *sc = &VC_SCALES[lp->scale];
            int n = snprintf(b, sizeof(b), "Scale:");
            int fl = vc_prefer_flats(lp->key, vc_scale_is_minor(lp->scale));
            for (int j = 0; j < sc->n; j++) n += snprintf(b + n, sizeof(b) - (size_t)n, " %s", vc_pc_name(lp->key + sc->iv[j], fl));
            if (sc->desc) snprintf(b + n, sizeof(b) - (size_t)n, "  |  %s", sc->desc);   /* the scale's character, as a few words */
        }
        break;
    }
    default:
        if (i >= P_PADNAME0 && i < P_PADFUNC0 + 16 && en_smart_index(e, idx_of(p, P_VIEW_BANK) * 16 + (i - P_PADNAME0) % 16) >= 0) {
            int k = (i - P_PADNAME0) % 16, isFunc = i >= P_PADFUNC0;
            int si = en_smart_index(e, k), note = e->smart_notes[si];
            if (!isFunc) vc_note_name(note, e->smart_src.flats, b, sizeof(b));
            else copy_str(b, smart_degree(e, note), sizeof(b));
        } else if (i >= P_PADNAME0 && i < P_PADFUNC0 + 16) {
            int k = (i - P_PADNAME0) % 16, isFunc = i >= P_PADFUNC0;
            const PadChord *pc = &p->ui_pads[idx_of(p, P_VIEW_BANK) * 16 + k];
            if (!pc->valid) b[0] = 0; /* empty pad: blank key on screen */
            else if (!isFunc) copy_str(b, pc->name, sizeof(b));
            else if (pc->roman[0]) copy_str(b, pc->roman, sizeof(b));
            else if (pc->spec.quality >= 0) copy_str(b, VC_QUALITIES[pc->spec.quality].label, sizeof(b));
        } else if (i >= P_STEP0 && i < P_STEP0 + EN_STEPS) {
            static const char *ST[7] = {"Rest", "Chord", "Accent", "Bass", "Upper", "Up acc", "Tie"};
            copy_str(b, ST[en_step_index(shown_step(p, i - P_STEP0))], sizeof(b));
        } else if (i >= P_SCALENOTE0 && i < P_SCALENOTE0 + 12) {
            copy_str(b, getParameterValueInScale(p, i - P_SCALENOTE0) ? "In scale" : "-", sizeof(b));
        }
        break;
    }
    copy_str(out, b, max);
}

/* ---------- chunk (saved inside the MPC project) ---------- */

#define IMP_REC 23
static int chunk_size(void) { return 12 + NUM_PARAMS * 4 + 4 + VC_PADS * 6 + 4 + 4 + VC_PADS * IMP_REC + 4 + 128 + VC_PADS * (1 + VC_MAX_IV) + VC_RIFFS * EN_STEPS + VC_USER_PROGS * (1 + VC_PROG_MAX * 2) + 1 + VC_USER_PRESETS * (1 + UPRE_MAX * 4) + 32; }

static void w32(unsigned char *b, int *k, uint32_t v) { b[(*k)++] = v & 255; b[(*k)++] = (v >> 8) & 255; b[(*k)++] = (v >> 16) & 255; b[(*k)++] = v >> 24; }
static uint32_t r32(const unsigned char *b, int *k) { uint32_t v = b[*k] | (b[*k + 1] << 8) | (b[*k + 2] << 16) | ((uint32_t)b[*k + 3] << 24); *k += 4; return v; }

static int get_chunk(Plugin *p, void **data) {
    int n = chunk_size(), k = 0;
    if (!p->chunk) p->chunk = (unsigned char *)malloc((size_t)n);
    if (!p->chunk) return 0;
    unsigned char *b = p->chunk;
    memcpy(b, "VLVC", 4); k = 4;
    w32(b, &k, CHUNK_VERSION);
    w32(b, &k, NUM_PARAMS);
    for (int i = 0; i < NUM_PARAMS; i++) { union { float f; uint32_t u; } u; u.f = p->val[i]; w32(b, &k, u.u); }
    w32(b, &k, VC_PADS);
    for (int i = 0; i < VC_PADS; i++) {
        const CustomPad *c = &p->custom_pending[i];
        b[k++] = c->valid; b[k++] = c->root; b[k++] = c->quality; b[k++] = c->bass;
        b[k++] = (unsigned char)c->inv; b[k++] = (unsigned char)c->oct;
    }
    w32(b, &k, (uint32_t)p->edit_pad);
    w32(b, &k, (uint32_t)p->nimp);
    for (int i = 0; i < p->nimp; i++) {
        const ImportChord *ic = &p->imp[i];
        b[k++] = (unsigned char)ic->spec.root; b[k++] = (unsigned char)(signed char)ic->spec.quality; b[k++] = (unsigned char)(signed char)ic->spec.bass;
        b[k++] = (unsigned char)ic->spec.ncustom;
        for (int j = 0; j < 8; j++) b[k++] = (unsigned char)ic->spec.custom[j];
        b[k++] = (unsigned char)ic->n;
        for (int j = 0; j < 10; j++) b[k++] = j < ic->n ? ic->notes[j] : 0;
    }
    w32(b, &k, (uint32_t)p->eng->map_set);
    for (int n = 0; n < 128; n++) b[k++] = (unsigned char)p->eng->pad_map[n];
    for (int i = 0; i < VC_PADS; i++) { /* version 4: note lists */
        const CustomPad *c = &p->custom_pending[i];
        b[k++] = c->nnotes;
        for (int j = 0; j < VC_MAX_IV; j++) b[k++] = (unsigned char)c->notes[j];
    }
    for (int s = 0; s < VC_RIFFS; s++)
        for (int j = 0; j < EN_STEPS; j++) b[k++] = (unsigned char)p->eng->rif[s][j];
    for (int s = 0; s < VC_USER_PROGS; s++) { /* version 5: saved progressions */
        b[k++] = p->lp_pending.un[s];
        for (int j = 0; j < VC_PROG_MAX; j++) { b[k++] = p->lp_pending.ur[s][j]; b[k++] = (unsigned char)p->lp_pending.uq[s][j]; }
    }
    b[k++] = UPRE_MAX; /* version 6: user presets, each a valid flag and UPRE_MAX floats */
    for (int s = 0; s < VC_USER_PRESETS; s++) {
        b[k++] = p->upre_ok[s];
        for (int j = 0; j < UPRE_MAX; j++) { union { float f; uint32_t u; } u; u.f = p->upre[s][j]; w32(b, &k, u.u); }
    }
    for (int j = 0; j < 32; j++) b[k++] = p->fav[j]; /* version 7: starred presets */
    *data = b;
    return k;
}

static int set_chunk(Plugin *p, const void *data, int size) {
    const unsigned char *b = (const unsigned char *)data;
    if (!b || size < 16 || memcmp(b, "VLVC", 4)) return 0;
    int k = 4;
    uint32_t ver = r32(b, &k), np = r32(b, &k);
    if (ver < 1 || ver > CHUNK_VERSION || np > 512 || size < (int)(12 + np * 4 + 4)) return 0;
    for (uint32_t i = 0; i < np; i++) {
        union { float f; uint32_t u; } u;
        u.u = r32(b, &k);
        if (ver < 8 && i == P_SCALE) u.f = scale_from_old(u.f);
        if (ver < 12 && i == P_MODE) u.f = mode_from_saved(u.f, ver);
        if (ver < 11 && i == P_PATTERN) u.f = pattern_from_saved(u.f);
        if (i == P_PRESET || i == P_PRESET_GENRE || i == P_PRESET_VAR) p->val[i] = u.f; /* shown only: the parameters saved with it already hold the preset's values */
        else if (i >= P_RIFF0 && i <= P_RIFF_END) p->val[i] = u.f; /* riff steps are stored as data below */
        else if (i == P_NOTE_IV) p->val[i] = u.f;                  /* the note list is stored as data below */
        else if (i < NUM_PARAMS && !PINFO[i].trigger) set_param(p, (int)i, u.f, 0);
    }
    uint32_t npads = r32(b, &k);
    if (npads > VC_PADS || size < k + (int)npads * 6) return 0;
    for (uint32_t i = 0; i < npads; i++) {
        CustomPad *c = &p->custom_pending[i];
        c->valid = b[k++] ? 1 : 0; c->root = b[k++] % 12; c->quality = b[k++]; c->bass = b[k++];
        c->inv = (signed char)b[k++]; c->oct = (signed char)b[k++];
        if (c->quality >= VC_NUM_QUALITIES) c->valid = 0;
        if (c->bass != 255) c->bass %= 12;
        if (c->inv < 0 || c->inv > 3) c->inv = 0;
        if (c->oct < -2 || c->oct > 2) c->oct = 0;
    }
    if (size >= k + 4) { p->edit_pad = (int)r32(b, &k); if (p->edit_pad < 0 || p->edit_pad >= VC_PADS) p->edit_pad = 0; }
    p->nimp = 0;
    if (ver >= 2 && size >= k + 4) {
        uint32_t ni = r32(b, &k);
        if (ni > VC_PADS || size < k + (int)ni * IMP_REC) ni = 0;
        for (uint32_t i = 0; i < ni; i++) {
            ImportChord *ic = &p->imp[i];
            memset(ic, 0, sizeof(*ic));
            ic->spec.root = b[k++] % 12; ic->spec.quality = (signed char)b[k++]; ic->spec.bass = (signed char)b[k++];
            ic->spec.ncustom = b[k++] % 9;
            for (int j = 0; j < 8; j++) ic->spec.custom[j] = (signed char)b[k++];
            ic->n = b[k++]; if (ic->n > 10) ic->n = 10;
            for (int j = 0; j < 10; j++) { unsigned char v = b[k++]; if (j < ic->n) ic->notes[j] = v & 127; }
            if (ic->spec.quality >= VC_NUM_QUALITIES || ic->spec.quality < -1) ic->spec.quality = -1;
            if (ic->spec.bass > 11 || ic->spec.bass < -1) ic->spec.bass = -1;
        }
        p->nimp = (int)ni;
    }
    if (ver >= 3 && size >= k + 4 + 128) {
        int ms = (int)r32(b, &k);
        int ok = 1;
        signed char m[128];
        for (int n = 0; n < 128; n++) { m[n] = (signed char)b[k++]; if (m[n] < -1 || m[n] > 15) ok = 0; }
        if (ok) { memcpy(p->eng->pad_map, m, 128); p->eng->map_set = ms ? 1 : 0; p->eng->learn_all = 0; }
    }
    if (ver >= 4 && size >= k + VC_PADS * (1 + VC_MAX_IV) + VC_RIFFS * EN_STEPS) {
        for (int i = 0; i < VC_PADS; i++) {
            CustomPad *c = &p->custom_pending[i];
            c->nnotes = b[k++];
            for (int j = 0; j < VC_MAX_IV; j++) c->notes[j] = (signed char)b[k++];
            if (c->nnotes > VC_MAX_IV) c->nnotes = 0;
            for (int j = 1; j < c->nnotes; j++) if (c->notes[j] < RIFF_MIN || c->notes[j] > RIFF_MAX) c->nnotes = 0;
            if (c->nnotes < 2) c->nnotes = 0;
            if (c->nnotes) c->notes[0] = 0;
        }
        for (int s = 0; s < VC_RIFFS; s++)
            for (int j = 0; j < EN_STEPS; j++) {
                signed char v = (signed char)b[k++];
                p->eng->rif[s][j] = (v == RIFF_REST || (v >= RIFF_MIN && v <= RIFF_MAX)) ? v : RIFF_REST;
            }
    }
    if (ver >= 5 && size >= k + VC_USER_PROGS * (1 + VC_PROG_MAX * 2)) {
        for (int s = 0; s < VC_USER_PROGS; s++) {
            p->lp_pending.un[s] = b[k++] <= VC_PROG_MAX ? b[k - 1] : 0;
            for (int j = 0; j < VC_PROG_MAX; j++) {
                p->lp_pending.ur[s][j] = b[k++] % 12;
                signed char q = (signed char)b[k++];
                p->lp_pending.uq[s][j] = (q >= 0 && q < VC_NUM_QUALITIES) ? q : -1;
            }
        }
    }
    if (ver >= 6 && size >= k + 1) {
        int per = b[k++];
        for (int s = 0; s < VC_USER_PRESETS && per > 0 && size >= k + 1 + per * 4; s++) {
            p->upre_ok[s] = b[k++] ? 1 : 0;
            for (int j = 0; j < per; j++) {
                union { float f; uint32_t u; } u;
                u.u = r32(b, &k);
                if (j < UPRE_MAX) {
                    float v = (u.f >= 0.f && u.f <= 1.f) ? u.f : 0.f; /* also rejects NaN */
                    if (ver < 8 && j == upre_scale_slot()) v = scale_from_old(v);
                    if (ver < 11 && j == upre_pattern_slot()) v = pattern_from_saved(v);
                    p->upre[s][j] = v;
                }
            }
        }
    }
    if (ver >= 7 && size >= k + 32) { for (int j = 0; j < 32; j++) p->fav[j] = b[k++]; p->fav[0] &= 0xFE; } /* bit 0 is not a preset */
    if (idx_of(p, P_MODE) == VC_MODE_SONG) set_param(p, P_MODE, norm_of(P_MODE, VC_MODE_KEY), 0); /* Song mode is gone: an old project moves to Scale */
    load_riff_fields(p);
    load_note_field(p);
    load_edit_fields(p);
    sync_banks(p); /* a project saved before the bank buttons existed has none of their values */
    sync_layout(p);
    refresh_host(p); /* the pads on screen follow the project's key, scale and modes at once */
    return 1;
}

/* ---------- audio ---------- */

static void ensure_scratch(Plugin *p, int n) {
    if (n <= p->scratch_n) return;
    if (n < 16384) n = 16384; /* generous, so a host's block size never makes the audio thread allocate */
    free(p->scratchL); free(p->scratchR);
    p->scratchL = (float *)calloc((size_t)n, sizeof(float));
    p->scratchR = (float *)calloc((size_t)n, sizeof(float));
    p->scratch_n = (p->scratchL && p->scratchR) ? n : 0;
}

static void apply_pending(Plugin *p) {
    Engine *e = p->eng;
    unsigned g = p->gen;
    if (g == p->gen_applied) return;
    e->lp = p->lp_pending;
    memcpy(p->imp_engine, p->imp, sizeof(p->imp_engine));
    e->lp.imp = p->imp_engine;
    memcpy(e->custom, p->custom_pending, sizeof(e->custom));
    p->gen_applied = g;
    en_rebuild(e);
}

static void send_midi_out(Plugin *p) {
    Engine *e = p->eng;
    int routes = p->out_mode | p->flush_routes;
    p->flush_routes = 0;
    if (!(p->out_mode & OUT_PORT) && vc_port_is_open(&p->port) && !e->nout) vc_port_close(&p->port); /* after its flush */
    if ((routes & OUT_PORT) && vc_port_is_open(&p->port))  /* note-offs held from the last block go out first */
        for (int n = 0; n < 128; n++)
            if (p->port_defer[n]) { unsigned char m[3] = {(unsigned char)(0x80 | (e->out_channel & 15)), (unsigned char)n, 0}; vc_port_send(&p->port, m); p->port_defer[n] = 0; }
    if (!e->nout) return;
    for (int i = 0; i < e->nout; i++) mlog_push(p, 'O', e->out[i].d);
    if ((routes & OUT_PORT) && vc_port_is_open(&p->port)) {
        /* the port sends everything at once, with no timing inside the block: a note-off that follows its own
         * note-on in the same block would cancel it before it sounds, so that note-off waits for the next block */
        unsigned char started[128] = {0};
        for (int i = 0; i < e->nout; i++) {
            const unsigned char *d = e->out[i].d;
            int note = d[1] & 127;
            if ((d[0] & 0xF0) == 0x90 && d[2] > 0) { started[note] = 1; vc_port_send(&p->port, d); }
            else if (started[note]) p->port_defer[note] = 1;
            else vc_port_send(&p->port, d);
        }
    }
    if (!(routes & OUT_HOST) || !p->host) { e->nout = 0; return; }
    int n = e->nout;
    if (!p->outEvents) { e->nout = 0; return; } /* allocated when the plugin is created: nothing is allocated while processing audio */
    for (int i = 0; i < n; i++) {
        VstMidiEvent *m = &p->outMidi[i];
        memset(m, 0, sizeof(*m));
        m->type = kVstMidiType;
        m->byteSize = (int32_t)sizeof(VstMidiEvent);
        m->deltaFrames = e->out[i].frame;
        m->midiData[0] = (char)e->out[i].d[0];
        m->midiData[1] = (char)e->out[i].d[1];
        m->midiData[2] = (char)e->out[i].d[2];
        p->outEvents->events[i] = (VstEvent *)m;
    }
    p->outEvents->numEvents = n;
    p->host(&p->fx, audioMasterProcessEvents, 0, 0, p->outEvents, 0.f);
    e->nout = 0;
}

static void render(Plugin *p, float **out, int n, int accumulate) {
    Engine *e = p->eng;
    p->audio_thread = pthread_self(); /* every block: the host may move audio between threads */
    p->have_audio_thread = 1;
    apply_pending(p);
    e->tempo = host_tempo_pos(p, &e->ppq, &e->playing);
    ensure_scratch(p, n); /* normally sized once in effSetBlockSize */
    if (!p->scratch_n) {
        if (!accumulate) { memset(out[0], 0, sizeof(float) * (size_t)n); memset(out[1], 0, sizeof(float) * (size_t)n); }
        e->nin = 0;
        return;
    }
    /* key sync: adopt the key and scale another copy published */
    if (idx_of(p, P_KEYSYNC) && g_vc_key.version && g_vc_key.version != p->key_seen && g_vc_key.source != e->instance_id) {
        p->key_seen = g_vc_key.version;
        p->val[P_KEY] = norm_of(P_KEY, g_vc_key.key);
        p->val[P_SCALE] = norm_of(P_SCALE, g_vc_key.scale);
        sync_layout(p);
        refresh_host(p);
    }
    /* screen audition: a tapped on-screen pad plays for ~0.9 s through the normal pad path */
    int req = p->audition_req;
    if (req >= 0) {
        p->audition_req = -1;
        if (p->audition_note >= 0) en_push_midi(e, 0, 0x80, (unsigned char)p->audition_note, 0);
        int note = -1; /* the note that this pad sends, in the learned layout if there is one */
        for (int n = 0; n < 128 && note < 0; n++) if (en_pad_index(e, n) == req) note = n;
        if (note < 0) note = 128;
        if (note <= 127) {
            en_push_midi(e, 0, 0x90, (unsigned char)note, 100);
            p->audition_note = note;
            p->audition_left = (int)(e->sr * 0.9f);
        }
    } else if (p->audition_note >= 0) {
        p->audition_left -= n;
        if (p->audition_left <= 0) { en_push_midi(e, 0, 0x80, (unsigned char)p->audition_note, 0); p->audition_note = -1; }
    }
    en_process(e, p->scratchL, p->scratchR, n);
    fx_reverb(&p->rv, p->scratchL, p->scratchR, n, p->val[P_REVERB]);
    apply_shift(p);
    if (e->events_changed && e->last_pad >= 0) { /* screen follows the played pad */
        p->seen_pad = e->last_pad;
        p->val[P_VIEW_BANK] = norm_of(P_VIEW_BANK, e->last_pad / 16);
        sync_banks(p);
        p->val[P_ACTIVE] = norm_of(P_ACTIVE, e->last_pad % 16 + 1);
        e->events_changed = 0;
        refresh_host(p);
    }
    e->events_changed = 0;
    if (e->smart_changed) { e->smart_changed = 0; if (e->smart != SMART_OFF) refresh_host(p); } /* smart labels follow the chord */
    for (int k = 0; k < n; k++) {
        if (accumulate) { out[0][k] += p->scratchL[k]; out[1][k] += p->scratchR[k]; }
        else { out[0][k] = p->scratchL[k]; out[1][k] = p->scratchR[k]; }
    }
    if (idx_of(p, P_PAD_BASE) != e->pad_base) p->val[P_PAD_BASE] = norm_of(P_PAD_BASE, e->pad_base); /* after Learn */
    if (e->edit_pad_request >= 0) { /* "Edit follows pad" in Custom mode */
        p->edit_pad = e->edit_pad_request;
        e->edit_pad_request = -1;
        load_edit_fields(p);
    }
    send_midi_out(p);
}

static void processReplacing(AEffect *fx, float **in, float **out, int32_t n) { (void)in; render((Plugin *)fx->object, out, n, 0); }
static void processAccumulating(AEffect *fx, float **in, float **out, int32_t n) { (void)in; render((Plugin *)fx->object, out, n, 1); }

static void setParameter(AEffect *fx, int32_t i, float v) { set_param((Plugin *)fx->object, i, v, 1); }
static float getParameter(AEffect *fx, int32_t i) {
    Plugin *p = (Plugin *)fx->object;
    if (i < 0 || i >= NUM_PARAMS) return 0.f;
    if (i == P_LEARN) return p->eng->learn ? 1.f : 0.f;
    if (i == P_PAD_BASE) return norm_of(P_PAD_BASE, p->eng->pad_base); /* may change via Learn */
    if (i >= P_SCALENOTE0 && i < P_SCALENOTE0 + 12) return getParameterValueInScale(p, i - P_SCALENOTE0) ? 1.f : 0.f;
    if (i >= P_STEP0 && i < P_STEP0 + EN_STEPS) return norm_of(i, en_step_index(shown_step(p, i - P_STEP0)));
    return p->val[i];
}

/* outside the audio callback (deactivate/close): send note-offs for anything still held straight to the port */
static void flush_port_now(Plugin *p) {
    Engine *e = p->eng;
    if (!vc_port_is_open(&p->port)) return;
    for (int n = 0; n < 128; n++) {
        if (!e->out_ref[n] && !p->port_defer[n]) continue;
        unsigned char m[3] = {(unsigned char)(0x80 | (e->out_channel & 15)), (unsigned char)n, 0};
        vc_port_send(&p->port, m);
        p->port_defer[n] = 0;
    }
}

static void destroy(Plugin *p) {
    stop_refresh_thread(p);
    flush_port_now(p);
    vc_port_close(&p->port);
    free(p->scratchL); free(p->scratchR); free(p->chunk); free(p->outEvents); free(p->eng); free(p);
}

static intptr_t dispatcher(AEffect *fx, int32_t op, int32_t index, intptr_t value, void *ptr, float opt) {
    Plugin *p = (Plugin *)fx->object;
    switch (op) {
    case effOpen: p->opened = 1; if (!p->refresh_running) start_refresh_thread(p); scan_import_files(p); return 0; /* host knows us now */
    case effClose: destroy(p); return 0;
    case effSetSampleRate: en_set_sr(p->eng, opt); fx_reverb_init(&p->rv, p->eng->sr); return 0;
    case effSetBlockSize: ensure_scratch(p, (int)value); return 0;
    case effMainsChanged: {
        unsigned char mv[3] = {(unsigned char)(value != 0), 0, 0};
        mlog_push(p, 'M', mv);
        if (!value) { flush_port_now(p); en_reset(p->eng); p->eng->nout = 0; }
        return 0;
    }
    case effGetParamName: if (index >= 0 && index < NUM_PARAMS && ptr) copy_str((char *)ptr, PINFO[index].name, 32); return 0;
    case effGetParamLabel: if (ptr) ((char *)ptr)[0] = 0; return 0;
    case effGetParamDisplay: if (index >= 0 && index < NUM_PARAMS && ptr) param_display(p, index, (char *)ptr, 64); return 0;
    case effCanBeAutomated: return (index >= 0 && index < NUM_PARAMS && !PINFO[index].trigger && !is_display_only(index) && index != P_ACTIVE && index != P_VIEW_BANK && !(index >= P_BANK_A && index <= P_BANK_D)) ? 1 : 0;
    /* the host's preset box: program 0 is Manual, programs 1..N are the factory presets (the same list as the Preset knob) */
    case effGetProgram: return idx_of(p, P_PRESET);
    case effSetProgram: if (value >= 0 && value <= VC_NUM_PRESETS) set_param(p, P_PRESET, norm_of(P_PRESET, (int)value), 1); return 0;
    case effGetProgramName: if (ptr) copy_str((char *)ptr, preset_label(idx_of(p, P_PRESET)), 24); return 0;
    case effGetProgramNameIndexed:
        if (index < 0 || index > VC_NUM_PRESETS || !ptr) return 0;
        copy_str((char *)ptr, preset_label(index), 24);
        return 1;
    case effSetProgramName: return 0; /* the factory names are fixed */
    case effGetChunk: return ptr ? get_chunk(p, (void **)ptr) : 0;
    case effSetChunk: return set_chunk(p, ptr, (int)value);
    case effProcessEvents: {
        VstEvents *ev = (VstEvents *)ptr;
        if (!ev) return 0;
        for (int i = 0; i < ev->numEvents; i++) {
            VstEvent *e = ev->events[i];
            if (!e || e->type != kVstMidiType) continue;
            VstMidiEvent *m = (VstMidiEvent *)e;
            int st = (unsigned char)m->midiData[0];
            mlog_push(p, 'I', (const unsigned char *)m->midiData);
            /* feedback guard: with the virtual port on, notes arriving on our own output channel are our chords
             * coming back (a track listening to all inputs), not pad hits */
            if ((p->out_mode & OUT_PORT) && ((st & 0xE0) == 0x80) && (st & 0x0F) == p->eng->out_channel) continue;
            en_push_midi(p->eng, m->deltaFrames, (unsigned char)m->midiData[0], (unsigned char)m->midiData[1], (unsigned char)m->midiData[2]);
        }
        return 1;
    }
    case effGetPlugCategory: return kPlugCategSynth;
    case effGetEffectName: if (ptr) copy_str((char *)ptr, "Velvet Chords", 32); return 1;
    case effGetVendorString: if (ptr) copy_str((char *)ptr, "Velvet Chords", 64); return 1;
    case effGetProductString: if (ptr) copy_str((char *)ptr, "Velvet Chords", 64); return 1;
    case effGetVendorVersion: return VC_VERSION;
    case effCanDo: {
        const char *s = (const char *)ptr;
        if (!s) return 0;
        if (!strcmp(s, "receiveVstEvents") || !strcmp(s, "receiveVstMidiEvent") || !strcmp(s, "sendVstEvents") || !strcmp(s, "sendVstMidiEvent")) return 1;
        return 0;
    }
    case effGetVstVersion: return 2400;
    case effVendorSpecific: /* test hook: 'VCdb' -> active synth voices (value 0) or held chords (value 1) */
        if (index == VC_FOURCC('V', 'C', 'd', 'b')) return value ? en_owner_count(p->eng) : sy_active(&p->eng->synth);
        return 0;
    case effGetTailSize: return 0;
    default: return 0;
    }
}

VC_EXPORT AEffect *VSTPluginMain(audioMasterCallback host) {
    if (host && !host(0, audioMasterVersion, 0, 0, 0, 0.f)) return 0;
    init_param_info();
    Plugin *p = (Plugin *)calloc(1, sizeof(Plugin));
    if (!p) return 0;
    p->eng = (Engine *)calloc(1, sizeof(Engine));
    if (!p->eng) { free(p); return 0; }
    p->outEvents = (VstEvents *)calloc(1, sizeof(VstEvents) + sizeof(VstEvent *) * EN_MIDI_OUT);
    if (!p->outEvents) { free(p->eng); free(p); return 0; }
    ensure_scratch(p, 16384);
    if (!p->scratch_n) { free(p->outEvents); free(p->eng); free(p); return 0; }
    p->host = host;
    en_init(p->eng, 44100.f);
    fx_reverb_init(&p->rv, 44100.f);
    p->audition_req = -1;
    p->audition_note = -1;
    p->seen_pad = -1;
    for (int k = 0; k < EN_STEPS; k++) p->val[P_STEP0 + k] = norm_of(P_STEP0 + k, en_step_index('.'));
    memcpy(p->custom_pending, p->eng->custom, sizeof(p->custom_pending));
    for (int i = 0; i < NUM_PARAMS; i++) p->val[i] = PINFO[i].def;
    for (int i = 0; i < NUM_PARAMS; i++)
        if (!PINFO[i].trigger && !(i >= P_EDIT_PAD && i <= P_EDIT_OCT) && !(i >= P_STEP0 && i < P_STEP0 + EN_STEPS)
            && !(i >= P_RIFF0 && i <= P_RIFF_END) && i != P_NOTE_IV && i != P_PRESET
            && i != P_PRESET_GENRE && i != P_PRESET_VAR) set_param(p, i, PINFO[i].def, 0);
    load_edit_fields(p); /* edit fields mirror the seeded custom pad A01 */
    load_riff_fields(p);
    load_note_field(p);
    sync_layout(p);

    AEffect *fx = &p->fx;
    p->refresh_req = 0;
    fx->magic = kEffectMagic;
    fx->dispatcher = dispatcher;
    fx->process = processAccumulating;
    fx->processReplacing = processReplacing;
    fx->setParameter = setParameter;
    fx->getParameter = getParameter;
    fx->numPrograms = VC_NUM_PRESETS + 1;
    fx->numParams = NUM_PARAMS;
    fx->numInputs = 0;
    fx->numOutputs = 2;
    fx->flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
    fx->ioRatio = 1.f;
    fx->object = p;
    fx->uniqueID = VC_UID;
    fx->version = VC_VERSION;
    return fx;
}

/* some hosts look for "main" instead of "VSTPluginMain" */
VC_EXPORT AEffect *vc_main_alias(audioMasterCallback host) __asm__("main");
AEffect *vc_main_alias(audioMasterCallback host) { return VSTPluginMain(host); }
