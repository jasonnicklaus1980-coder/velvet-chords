/* Minimal VST2 host used to test velvetchords.so through its binary interface:
 * load, enumerate parameters, play pads, check audio/stuck notes, save/restore the chunk,
 * trigger Export MIDI, and render a demo WAV of a song section. Runs natively or under qemu-arm. */
#include "../src/vst2_abi.h"
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/chords.h"

static int fails = 0, checks = 0;
/* Edit type has 0 (none) plus one step per chord quality, so quality q is step q + 1 of VC_NUM_QUALITIES */
static float edit_type_of(const char *suffix) { return (vc_quality_find(suffix) + 1) / (float)VC_NUM_QUALITIES; }
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static VstTimeInfo g_time;
static pthread_t g_main_thread;
static volatile int g_refresh_calls = 0, g_refresh_on_audio_thread = 0;
static int g_midi_out_events = 0;

static intptr_t host_cb(AEffect *fx, int32_t op, int32_t index, intptr_t value, void *ptr, float opt) {
    (void)fx; (void)index; (void)value; (void)opt;
    switch (op) {
    case audioMasterVersion: return 2400;
    case audioMasterGetTime: return (intptr_t)&g_time;
    case audioMasterProcessEvents: g_midi_out_events += ((VstEvents *)ptr)->numEvents; return 1;
    case audioMasterUpdateDisplay: /* the test's audio thread is the main thread */
        g_refresh_calls++;
        if (pthread_equal(pthread_self(), g_main_thread)) g_refresh_on_audio_thread++;
        return 1;
    default: return 0;
    }
}

typedef AEffect *(*MainProc)(audioMasterCallback);
static MainProc g_main;

static AEffect *open_fx(void) {
    AEffect *fx = g_main(host_cb);
    if (!fx) return 0;
    fx->dispatcher(fx, effOpen, 0, 0, 0, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, 0, 48000.f);
    fx->dispatcher(fx, effSetBlockSize, 0, 256, 0, 0);
    fx->dispatcher(fx, effMainsChanged, 0, 1, 0, 0);
    /* the reverb keeps ringing after the notes end; the note tests look at the notes, so they run dry (see the reverb test) */
    for (int i = 0; ; i++) {  /* parameter lookup by name, as the helpers below do */
        char nm[40] = "";
        fx->dispatcher(fx, effGetParamName, i, 0, nm, 0);
        if (!strcmp(nm, "Reverb")) { fx->setParameter(fx, i, 0.f); break; }
        if (i > 400) break;
    }
    return fx;
}

static int find_param(AEffect *fx, const char *name) {
    char buf[256];
    for (int i = 0; i < fx->numParams; i++) {
        buf[0] = 0;
        fx->dispatcher(fx, effGetParamName, i, 0, buf, 0);
        if (!strcmp(buf, name)) return i;
    }
    return -1;
}

static void display(AEffect *fx, int i, char *buf) { buf[0] = 0; fx->dispatcher(fx, effGetParamDisplay, i, 0, buf, 0); }

/* same layout as VstEvents but with room for many pointers (VstEvents::events is a variable-length tail) */
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[80]; } BigEvents;
typedef struct { BigEvents hdr; VstMidiEvent ev[80]; } EvBuf;

/* the allocation probe (tests/alloc_probe.c, preloaded by `make test`): counts heap calls inside the audio and MIDI entry points */
static volatile int *g_probe_on;
static volatile long *g_probe_count;
static long g_probe_blocks;
#define PROBE_ON() do { if (g_probe_on) *g_probe_on = 1; } while (0)
#define PROBE_OFF() do { if (g_probe_on) *g_probe_on = 0; } while (0)

/* the normalised value whose display text is `name` (scales and other lists grow; don't assume their count) */
static float value_of(AEffect *fx, int i, const char *name) {
    char buf[96];
    for (int k = 0; k <= 4000; k++) {
        fx->setParameter(fx, i, (float)k / 4000.f);
        display(fx, i, buf);
        if (!strcmp(buf, name)) return (float)k / 4000.f;
    }
    return 0.f;
}

static void send(AEffect *fx, EvBuf *b, int n) {
    b->hdr.numEvents = n;
    for (int i = 0; i < n; i++) { b->ev[i].type = kVstMidiType; b->ev[i].byteSize = sizeof(VstMidiEvent); b->hdr.events[i] = (VstEvent *)&b->ev[i]; }
    PROBE_ON();
    fx->dispatcher(fx, effProcessEvents, 0, 0, (VstEvents *)&b->hdr, 0);
    PROBE_OFF();
}

static void midi(VstMidiEvent *e, int frame, int s, int d1, int d2) { memset(e, 0, sizeof(*e)); e->deltaFrames = frame; e->midiData[0] = (char)s; e->midiData[1] = (char)d1; e->midiData[2] = (char)d2; }

static double block(AEffect *fx, float *L, float *R, int n) {
    float *outs[2] = {L, R};
    PROBE_ON();
    fx->processReplacing(fx, 0, outs, n);
    PROBE_OFF();
    g_probe_blocks++;
    double e = 0;
    for (int i = 0; i < n; i++) { e += L[i] * L[i] + R[i] * R[i]; if (!(L[i] == L[i]) || fabsf(L[i]) > 1.f) return -1; }
    return e;
}

static void wav_write(const char *path, const short *pcm, int frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int data = frames * 4, sr = 48000;
    unsigned char h[44] = {'R','I','F','F'};
    int v = 36 + data; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    short s = 1; memcpy(h + 20, &s, 2); s = 2; memcpy(h + 22, &s, 2);
    memcpy(h + 24, &sr, 4); v = sr * 4; memcpy(h + 28, &v, 4); s = 4; memcpy(h + 32, &s, 2); s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4); memcpy(h + 40, &data, 4);
    fwrite(h, 1, 44, f); fwrite(pcm, 4, (size_t)frames, f); fclose(f);
}

static void *audio_once_fn(void *a) {
    AEffect *x = *(AEffect **)a;
    static float l[256], r[256];
    float *o[2] = {l, r};
    x->processReplacing(x, 0, o, 256);
    return 0;
}

int main(int argc, char **argv) {
    const char *so = argc > 1 ? argv[1] : "./velvetchords.so";
    const char *demo = argc > 2 ? argv[2] : 0;
    g_time.tempo = 90.0; g_time.sampleRate = 48000.0; g_time.flags = kVstTempoValid;
    g_main_thread = pthread_self();
    void *h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    g_main = (MainProc)dlsym(h, "VSTPluginMain");
    g_probe_on = (volatile int *)dlsym(RTLD_DEFAULT, "vc_probe_on");
    g_probe_count = (volatile long *)dlsym(RTLD_DEFAULT, "vc_probe_count");
    CHECK(g_main != 0, "VSTPluginMain exported");
    CHECK(dlsym(h, "main") != 0, "main alias exported");
    if (!g_main) return 1;

    AEffect *fx = open_fx();
    CHECK(fx && fx->magic == kEffectMagic, "magic");
    CHECK(fx->numOutputs == 2 && fx->numInputs == 0, "stereo instrument");
    CHECK(fx->flags & effFlagsIsSynth, "is synth");
    CHECK(fx->uniqueID == VC_FOURCC('V', 'l', 'v', 'C'), "uid VlvC = 0x%08x", (unsigned)fx->uniqueID);
    CHECK(fx->dispatcher(fx, effGetPlugCategory, 0, 0, 0, 0) == kPlugCategSynth, "category synth");
    CHECK(fx->dispatcher(fx, effCanDo, 0, 0, (void *)"receiveVstMidiEvent", 0) == 1, "receives MIDI");

    char buf[256], name[256];
    printf("parameters (%d):\n", fx->numParams);
    for (int i = 0; i < fx->numParams; i++) {
        name[0] = 0;
        fx->dispatcher(fx, effGetParamName, i, 0, name, 0);
        display(fx, i, buf);
        CHECK(name[0] != 0, "param %d has a name", i);
        CHECK(strlen(buf) < 64, "param %d display fits", i);
        printf("  %2d %-17s %s\n", i, name, buf);
    }

    int P_MODE = find_param(fx, "Mode"), P_SONG = find_param(fx, "Song"), P_NOW = find_param(fx, "Now playing");
    int P_EDIT_PAD = find_param(fx, "Edit pad"), P_EDIT_TYPE = find_param(fx, "Edit type"), P_EDIT_ROOT = find_param(fx, "Edit root");
    int P_EXPORT = find_param(fx, "Export MIDI"), P_SOUND = find_param(fx, "Sound"), P_LEARN = find_param(fx, "Learn pad 1"), P_BASE = find_param(fx, "Pad 1 note");
    int P_SECTION = find_param(fx, "Section"), P_MIDI_OUT = find_param(fx, "MIDI out");
    CHECK(P_MODE >= 0 && P_SONG >= 0 && P_NOW >= 0 && P_EDIT_PAD >= 0 && P_EXPORT >= 0, "named params found");

    static float L[48000], R[48000];
    EvBuf eb;
    memset(&eb, 0, sizeof(eb));

    /* every song: select it, hit pad 1, expect sound */
    int nsongs = 0;
    for (float v = 0.f;; ) {
        fx->setParameter(fx, P_SONG, v);
        display(fx, P_SONG, buf);
        midi(&eb.ev[0], 0, 0x90, 36, 100); send(fx, &eb, 1);
        double e = block(fx, L, R, 256) + block(fx, L, R, 256);
        CHECK(e > 0.01, "song '%s' pad 1 makes sound (%f)", buf, e);
        display(fx, P_NOW, name);
        if (nsongs < 3) printf("  %s -> %s\n", buf, name);
        midi(&eb.ev[0], 0, 0x80, 36, 0); send(fx, &eb, 1);
        block(fx, L, R, 256);
        nsongs++;
        if (v >= 1.f) break;
        float before = v;
        char cur[256]; strcpy(cur, buf);
        do { v += 0.001f; if (v > 1.f) v = 1.f; fx->setParameter(fx, P_SONG, v); display(fx, P_SONG, buf); } while (!strcmp(buf, cur) && v < 1.f);
        if (!strcmp(buf, cur)) break;
        (void)before;
    }
    printf("  played pad 1 of %d songs\n", nsongs);

    /* silence after release */
    for (int i = 0; i < 1000; i++) block(fx, L, R, 256);
    CHECK(block(fx, L, R, 256) == 0.0, "silent after all pads released");

    /* rapid overlapping pads with strum + arp + sustain, then all off */
    fx->setParameter(fx, P_MODE, 1.f / 3.f); /* Smart scale */
    fx->setParameter(fx, P_MIDI_OUT, 1.f);
    int P_STRUM = find_param(fx, "Strum"), P_ARP = find_param(fx, "Arp");
    for (int r = 0; r < 300; r++) {
        if (r == 100) fx->setParameter(fx, P_STRUM, 0.3f);
        if (r == 200) fx->setParameter(fx, P_ARP, 0.25f);
        int n = 0;
        midi(&eb.ev[n++], (r * 7) % 256, 0x90, 36 + (r * 5) % 32, 30 + r % 90);
        midi(&eb.ev[n++], (r * 11) % 256, 0x80, 36 + (r * 3) % 32, 0);
        if (r % 50 == 0) midi(&eb.ev[n++], 10, 0xB0, 64, (r / 50) % 2 ? 0 : 127);
        send(fx, &eb, n);
        CHECK(block(fx, L, R, 256) >= 0, "finite audio under rapid triggering");
    }
    int n = 0;
    for (int k = 0; k < 64; k++) midi(&eb.ev[n++], 0, 0x80, 36 + k, 0);
    midi(&eb.ev[n++], 0, 0xB0, 64, 0);
    send(fx, &eb, n);
    block(fx, L, R, 256);
    int held = (int)fx->dispatcher(fx, effVendorSpecific, VC_FOURCC('V', 'C', 'd', 'b'), 1, 0, 0);
    CHECK(held == 0, "no held chords after all note-offs (%d)", held);
    for (int i = 0; i < 1500; i++) block(fx, L, R, 256);
    int voices = (int)fx->dispatcher(fx, effVendorSpecific, VC_FOURCC('V', 'C', 'd', 'b'), 0, 0, 0);
    CHECK(voices == 0 && block(fx, L, R, 256) == 0.0, "no stuck notes after rapid triggering (%d voices)", voices);
    CHECK(g_midi_out_events > 0, "MIDI out events delivered to host (%d)", g_midi_out_events);

    /* learn pad 1 */
    fx->setParameter(fx, P_LEARN, 1.f);
    midi(&eb.ev[0], 0, 0x90, 48, 100); send(fx, &eb, 1);
    block(fx, L, R, 256);
    display(fx, P_BASE, buf);
    CHECK(strstr(buf, "(48)") != 0, "learned pad 1 = note 48 (%s)", buf);
    midi(&eb.ev[0], 0, 0x80, 48, 0); send(fx, &eb, 1);
    block(fx, L, R, 256);

    /* custom edit + chunk round trip */
    fx->setParameter(fx, P_EDIT_PAD, 5.f / 63.f);
    fx->setParameter(fx, P_EDIT_ROOT, 2.f / 11.f); /* D */
    int nq = (int)(1.f / (1.f / 1.f)); (void)nq;
    fx->setParameter(fx, P_EDIT_TYPE, edit_type_of("m9")); /* m9, whatever its place in the quality list */
    display(fx, P_EDIT_TYPE, buf);
    printf("  edit type -> %s\n", buf);
    display(fx, P_MODE, name);
    CHECK(!strcmp(name, "Custom"), "editing switches to Custom (%s)", name);
    fx->setParameter(fx, P_SOUND, 1.f / 3.f);
    void *chunk = 0;
    int size = (int)fx->dispatcher(fx, effGetChunk, 0, 0, &chunk, 0);
    CHECK(size > 0 && chunk, "chunk saved (%d bytes)", size);
    float vals[64];
    char disp[64][64];
    for (int i = 0; i < fx->numParams && i < 64; i++) { vals[i] = fx->getParameter(fx, i); display(fx, i, disp[i]); }
    void *copy = malloc((size_t)size);
    memcpy(copy, chunk, (size_t)size);

    AEffect *fx2 = open_fx();
    CHECK(fx2->dispatcher(fx2, effSetChunk, 0, size, copy, 0) == 1, "chunk loaded");
    int P_NOW2 = P_NOW, P_EXPORT2 = P_EXPORT;
    for (int i = 0; i < fx->numParams && i < 64; i++) {
        if (i == P_NOW2 || i == P_EXPORT2) continue;
        char d2[64];
        display(fx2, i, d2);
        CHECK(fabsf(fx2->getParameter(fx2, i) - vals[i]) < 1e-6f && !strcmp(d2, disp[i]), "param %d restored ('%s' vs '%s')", i, d2, disp[i]);
    }
    /* the edited pad plays the edited chord on the restored instance */
    midi(&eb.ev[0], 0, 0x90, 48 + 5, 100); send(fx2, &eb, 1);
    block(fx2, L, R, 256);
    display(fx2, P_NOW, buf);
    CHECK(strstr(buf, "Dm9") != 0, "restored custom pad plays Dm9 (%s)", buf);
    printf("  restored pad: %s\n", buf);
    CHECK(fx2->dispatcher(fx2, effSetChunk, 0, 5, (void *)"junk!", 0) == 0, "rejects a bad chunk");
    free(copy);

    /* export */
    fx2->setParameter(fx2, P_MODE, 0.f);
    fx2->setParameter(fx2, P_SECTION, 0.f);
    fx2->setParameter(fx2, P_EXPORT, 1.f);
    display(fx2, P_EXPORT, buf);
    CHECK(strstr(buf, "Saved") != 0, "export reports success (%s)", buf);
    fx2->setParameter(fx2, P_MODE, 1.f / 3.f);
    fx2->setParameter(fx2, P_EXPORT, 1.f);
    display(fx2, P_EXPORT, buf);
    CHECK(strstr(buf, "Saved") != 0, "kit export (%s)", buf);

    /* MIDI log: while it is on, Velvet writes what it receives and sends to MIDI log.txt */
    {
        static EvBuf lb; static float LL[256], RR[256];
        char lpath[512], text[8192] = "";
        snprintf(lpath, sizeof(lpath), "%s/MIDI log.txt", getenv("VC_EXPORT_DIR"));
        remove(lpath);
        AEffect *m = open_fx();
        int P_LOG = find_param(m, "MIDI log"), P_MO = find_param(m, "MIDI out");
        CHECK(P_LOG > 0, "MIDI log parameter exists");
        m->setParameter(m, P_MO, 1.f);
        m->setParameter(m, P_LOG, 1.f);
        midi(&lb.ev[0], 0, 0x90, 36, 100); send(m, &lb, 1); block(m, LL, RR, 256);
        midi(&lb.ev[0], 0, 0x80, 36, 0); send(m, &lb, 1); block(m, LL, RR, 256);
        struct timespec ts = {0, 300 * 1000000L}; nanosleep(&ts, 0); /* the helper thread writes every 40 ms */
        m->setParameter(m, P_LOG, 0.f);
        block(m, LL, RR, 256);
        nanosleep(&ts, 0);
        FILE *f = fopen(lpath, "r");
        size_t nread = f ? fread(text, 1, sizeof(text) - 1, f) : 0;
        if (f) fclose(f);
        text[nread] = 0;
        CHECK(strstr(text, "I 90 24 64") != 0, "the incoming note on is logged");
        CHECK(strstr(text, "I 80 24 00") != 0, "the incoming note off is logged");
        CHECK(strstr(text, " O 9F 24 64") != 0 && strstr(text, " O 8F 24 00") != 0, "the chord notes Velvet sends are logged on channel 16");
        CHECK(strstr(text, "log started") != 0, "the log file starts with a header");
        m->dispatcher(m, effClose, 0, 0, 0, 0);
    }

    /* screen helpers used by the MPC skin */
    {
        AEffect *s = open_fx();
        int P_SNEXT = find_param(s, "Song next"), P_SPAD = find_param(s, "Screen pad"), P_SBANK = find_param(s, "Screen bank");
        int P_PN1 = find_param(s, "Pad 1 chord"), P_PF1 = find_param(s, "Pad 1 function"), P_KEY = find_param(s, "Key"), P_SCALE = find_param(s, "Scale");
        int P_HAS_CS = find_param(s, "Scale has C#"), P_TNEXT = find_param(s, "Edit type next"), P_EPNEXT = find_param(s, "Edit pad next");
        CHECK(P_SNEXT > 0 && P_SPAD > 0 && P_PN1 > 0 && P_HAS_CS > 0 && P_TNEXT > 0, "screen params exist");
        char a[64], b2[64];
        display(s, P_SONG, a);
        s->setParameter(s, P_SNEXT, 1.f);
        display(s, P_SONG, b2);
        CHECK(strcmp(a, b2) != 0 && s->getParameter(s, P_SNEXT) == 0.f, "Song next steps the song (%s -> %s) and resets", a, b2);
        s->setParameter(s, P_MODE, 1.f / 3.f); /* Smart scale, C major */
        display(s, P_PN1, a); display(s, P_PF1, b2);
        CHECK(!strcmp(a, "Cmaj7") && !strcmp(b2, "Imaj7"), "pad 1 label Cmaj7 / Imaj7 (%s / %s)", a, b2);
        CHECK(s->getParameter(s, P_HAS_CS) == 0.f, "C# not in C major");
        s->setParameter(s, P_KEY, 2.f / 11.f); /* D major */
        CHECK(s->getParameter(s, P_HAS_CS) == 1.f, "C# in D major");
        display(s, P_PN1, a);
        CHECK(!strcmp(a, "Dmaj7"), "pad label follows key (%s)", a);
        s->setParameter(s, P_SBANK, 1.f / 3.f); /* bank B: triads */
        display(s, P_PN1, a);
        CHECK(!strcmp(a, "D"), "screen bank B shows triads (%s)", a);
        /* the four bank buttons: pressing one shows its bank, the others follow, and a press on the bank already up changes nothing */
        int P_BA = find_param(s, "Bank A"), P_BB = find_param(s, "Bank B"), P_BC = find_param(s, "Bank C"), P_BD = find_param(s, "Bank D");
        CHECK(P_BA > 0 && P_BB > 0 && P_BC > 0 && P_BD > 0, "bank button params exist");
        s->setParameter(s, P_BC, 1.f);
        display(s, P_SBANK, a);
        CHECK(!strcmp(a, "C") && s->getParameter(s, P_BC) == 1.f && s->getParameter(s, P_BA) == 0.f, "bank C button shows C and clears A (%s)", a);
        s->setParameter(s, P_BC, 0.f); /* releasing the lit bank */
        CHECK(!strcmp(a, "C") && s->getParameter(s, P_BC) == 1.f, "a press on the lit bank keeps it (%s)", a);
        s->setParameter(s, P_BA, 1.f);
        display(s, P_SBANK, a);
        CHECK(!strcmp(a, "A") && s->getParameter(s, P_BA) == 1.f && s->getParameter(s, P_BC) == 0.f, "bank A button shows A (%s)", a);
        s->setParameter(s, P_BD, 1.f);
        display(s, P_SBANK, a);
        CHECK(!strcmp(a, "D") && s->getParameter(s, P_BD) == 1.f && s->getParameter(s, P_BA) == 0.f, "bank D button shows D (%s)", a);
        s->setParameter(s, P_SBANK, 0.f); /* the screen-bank knob moves the buttons too */
        CHECK(s->getParameter(s, P_BA) == 1.f && s->getParameter(s, P_BD) == 0.f, "bank knob lights bank A");
        (void)P_BB;
        (void)P_SCALE;
        /* tap pad 3 on screen: it sounds, then stops by itself */
        s->setParameter(s, P_SBANK, 0.f);
        s->setParameter(s, P_SPAD, 3.f / 16.f);
        double e = block(s, L, R, 256) + block(s, L, R, 256);
        display(s, P_NOW, a);
        CHECK(e > 0.01 && strstr(a, "A03") && strstr(a, "F#m7"), "screen tap auditions pad A03 (%s)", a);
        for (int i = 0; i < 1200; i++) block(s, L, R, 256);
        CHECK(block(s, L, R, 256) == 0.0, "audition stops by itself");
        /* hitting a hardware pad in bank B moves the screen to bank B and lights the pad */
        midi(&eb.ev[0], 0, 0x90, 36 + 16 + 4, 90); send(s, &eb, 1);
        block(s, L, R, 256);
        display(s, P_SBANK, a); display(s, P_SPAD, b2);
        CHECK(!strcmp(a, "B") && !strcmp(b2, "B05"), "screen follows played pad (%s %s)", a, b2);
        midi(&eb.ev[0], 0, 0x80, 36 + 16 + 4, 0); send(s, &eb, 1);
        block(s, L, R, 256);
        /* edit type next switches to Custom and changes the edited pad */
        int P_ET = find_param(s, "Edit type");
        display(s, P_ET, a);
        s->setParameter(s, P_TNEXT, 1.f);
        display(s, P_ET, b2);
        display(s, P_MODE, name);
        CHECK(strcmp(a, b2) && !strcmp(name, "Custom"), "edit type next (%s -> %s, mode %s)", a, b2, name);
        s->setParameter(s, P_EPNEXT, 1.f);
        display(s, find_param(s, "Edit pad"), a);
        CHECK(!strcmp(a, "A02"), "edit pad next (%s)", a);
        CHECK(s->dispatcher(s, effCanBeAutomated, P_SPAD, 0, 0, 0) == 0 && s->dispatcher(s, effCanBeAutomated, P_PN1, 0, 0, 0) == 0, "screen helpers not automatable");
        s->dispatcher(s, effClose, 0, 0, 0, 0);
    }

    /* ChordPrism-style output: pattern + MIDI routing */
    {
        AEffect *s = open_fx();
        int P_OUT = find_param(s, "MIDI out"), P_CH = find_param(s, "MIDI channel"), P_PAT = find_param(s, "Pattern");
        int P_ST1 = find_param(s, "Step 1"), P_PORT = find_param(s, "MIDI port"), P_SND = find_param(s, "Sound");
        int P_PNEXT = find_param(s, "Pattern next");
        CHECK(P_OUT > 0 && P_CH > 0 && P_PAT > 0 && P_ST1 > 0 && P_PORT > 0 && P_PNEXT > 0, "pattern/output params exist");
        char a[64];
        s->setParameter(s, P_PNEXT, 1.f);
        s->setParameter(s, P_PNEXT, 1.f);
        s->setParameter(s, P_PNEXT, 1.f);
        display(s, P_PAT, a);
        CHECK(!strcmp(a, "Quarters"), "pattern next x3 = Quarters (%s)", a);
        display(s, P_ST1, a);
        CHECK(!strcmp(a, "Accent"), "step 1 shows the preset (%s)", a);
        display(s, find_param(s, "Step 2"), a);
        CHECK(!strcmp(a, "Tie"), "step 2 is a tie (%s)", a);
        /* editing a step of a preset makes it Custom, keeping the other steps */
        s->setParameter(s, find_param(s, "Step 2"), 1.f / 6.f); /* Chord */
        display(s, P_PAT, a);
        CHECK(!strcmp(a, "Custom"), "edited preset becomes Custom (%s)", a);
        display(s, P_ST1, a);
        CHECK(!strcmp(a, "Accent"), "other steps kept (%s)", a);
        s->setParameter(s, P_SND, 1.f);         /* Silent: MIDI only, like ChordPrism */
        s->setParameter(s, P_CH, 3.f / 15.f);   /* channel 4 */
        g_midi_out_events = 0;
        s->setParameter(s, P_OUT, 1.f / 3.f);   /* to host */
        midi(&eb.ev[0], 0, 0x90, 36, 100); send(s, &eb, 1);
        double en = 0;
        for (int i = 0; i < 200; i++) en += block(s, L, R, 256);
        CHECK(en == 0.0, "Silent: no audio from the plugin");
        CHECK(g_midi_out_events > 4, "pattern sends MIDI to the host (%d events)", g_midi_out_events);
        midi(&eb.ev[0], 0, 0x80, 36, 0); send(s, &eb, 1);
        block(s, L, R, 256);
        /* virtual port */
        s->setParameter(s, P_OUT, 2.f / 3.f);
        display(s, P_PORT, a);
        const char *fake = getenv("VC_FAKE_ALSA_LOG");
        printf("  MIDI port status: %s\n", a);
        if (fake) CHECK(strstr(a, "Port open") != 0, "virtual port opens (%s)", a);
        else CHECK(strstr(a, "Port open") || strstr(a, "unavailable") || strstr(a, "No ALSA"), "port reports its state (%s)", a);
        midi(&eb.ev[0], 0, 0x90, 36 + 2, 100); send(s, &eb, 1);
        for (int i = 0; i < 100; i++) block(s, L, R, 256);
        midi(&eb.ev[0], 0, 0x80, 36 + 2, 0); send(s, &eb, 1);
        for (int i = 0; i < 10; i++) block(s, L, R, 256);
        /* feedback guard: our own output channel (4) coming back in must not trigger chords */
        s->setParameter(s, P_SND, 0.f);
        midi(&eb.ev[0], 0, 0x93, 40, 100); send(s, &eb, 1);
        double fb = block(s, L, R, 256) + block(s, L, R, 256);
        CHECK(fb == 0.0, "notes on the output channel are ignored while the port is on");
        midi(&eb.ev[0], 0, 0x90, 40, 100); send(s, &eb, 1);
        fb = block(s, L, R, 256) + block(s, L, R, 256);
        CHECK(fb > 0.0, "pad notes on channel 1 still play");
        midi(&eb.ev[0], 0, 0x80, 40, 0); send(s, &eb, 1);
        for (int i = 0; i < 10; i++) block(s, L, R, 256);
        s->setParameter(s, P_OUT, 0.f);
        block(s, L, R, 256);
        s->dispatcher(s, effClose, 0, 0, 0, 0);
        if (fake) {
            FILE *f = fopen(fake, "r");
            char line[256];
            int opened = 0, client = 0, port = 0, on = 0, off = 0, okfmt = 1, closed = 0, ch4 = 1;
            int bal[128] = {0};
            long last_open = 0;
            while (f && fgets(line, sizeof(line), f)) if (!strncmp(line, "open ", 5)) last_open = ftell(f) - (long)strlen(line);
            if (f) fseek(f, last_open, SEEK_SET); /* this instance's port only */
            while (f && fgets(line, sizeof(line), f)) {
                int ty, ch, note, vel, q, dc, dp, sp, sz;
                if (!strncmp(line, "open default 1 1", 16)) opened = 1;
                if (!strncmp(line, "client Velvet Chords", 20)) client = 1;
                if (!strncmp(line, "port Velvet Chords Out 33 ", 26)) port = 1;
                if (!strncmp(line, "close", 5)) closed = 1;
                if (sscanf(line, "ev %d %d %d %d %d %d %d %d %d", &ty, &ch, &note, &vel, &q, &dc, &dp, &sp, &sz) == 9) {
                    if (q != 253 || dc != 254 || dp != 253 || sz != 28) okfmt = 0;
                    if (ch != 3) ch4 = 0;
                    if (ty == 6) { on++; bal[note]++; } else if (ty == 7) { off++; bal[note]--; }
                }
            }
            if (f) fclose(f);
            int balanced = 1;
            for (int k = 0; k < 128; k++) if (bal[k]) balanced = 0;
            CHECK(opened && client && port, "ALSA client 'Velvet Chords' with readable, subscribable port 'Velvet Chords Out'");
            CHECK(on > 4 && balanced, "port got note-ons and matching note-offs (%d on, %d off)", on, off);
            CHECK(okfmt && ch4, "events sent directly to subscribers on channel 4, 28-byte events");
            CHECK(closed, "port closed when MIDI out is switched off");
        }
    }

    /* Smart Scale pads and a second instance following the first */
    {
        AEffect *a = open_fx(), *b = open_fx();
        int SP = find_param(a, "Smart pads"), SN = find_param(a, "Smart notes"), SONG = find_param(a, "Song");
        int PN9 = find_param(a, "Pad 9 chord"), PF9 = find_param(a, "Pad 9 function"), PF11 = find_param(a, "Pad 11 function"), NOW = find_param(a, "Now playing");
        CHECK(SP > 0 && SN > 0, "smart params exist");
        char x[64], y[64];
        a->setParameter(a, find_param(a, "Mode"), 0.f); /* Song mode: the engine still supports it, the screen no longer offers it */
        for (float v = 0.f; v <= 1.f; v += 0.001f) { a->setParameter(a, SONG, v); display(a, SONG, x); if (strstr(x, "Just the Two of Us")) break; }
        a->setParameter(a, SP, 0.5f); /* pads 9-16 */
        block(a, L, R, 256);
        midi(&eb.ev[0], 0, 0x90, 36, 100); send(a, &eb, 1); block(a, L, R, 256); /* pad 1: Dbmaj7 */
        midi(&eb.ev[0], 0, 0x80, 36, 0); send(a, &eb, 1); block(a, L, R, 256);
        display(a, PN9, x); display(a, PF9, y);
        CHECK(!strcmp(x, "Db3") && !strcmp(y, "root"), "pad 9 = root of Dbmaj7 (%s %s)", x, y);
        display(a, PF11, y);
        CHECK(!strcmp(y, "5th"), "pad 11 = 5th (%s)", y);
        midi(&eb.ev[0], 0, 0x90, 36 + 10, 100); send(a, &eb, 1); block(a, L, R, 256); /* pad 11 */
        display(a, NOW, x);
        CHECK(strstr(x, "Ab3: 5th of Dbmaj7") != 0, "smart pad plays one note (%s)", x);
        printf("  %s\n", x);
        midi(&eb.ev[0], 0, 0x80, 36 + 10, 0); send(a, &eb, 1); block(a, L, R, 256);
        midi(&eb.ev[0], 0, 0x90, 36 + 1, 100); send(a, &eb, 1); block(a, L, R, 256); /* pad 2: C7 */
        midi(&eb.ev[0], 0, 0x80, 36 + 1, 0); send(a, &eb, 1); block(a, L, R, 256);
        display(a, PN9, x); display(a, find_param(a, "Pad 12 chord"), y);
        CHECK(!strcmp(x, "C3") && !strcmp(y, "Bb3"), "smart pads follow the new chord C7 (%s %s)", x, y);
        a->setParameter(a, SN, 0.5f); /* scale: C7 tones + F minor scale, upward from C */
        block(a, L, R, 256);
        char row[200] = "";
        for (int k = 9; k <= 16; k++) { char nm[24]; snprintf(nm, 24, "Pad %d chord", k); display(a, find_param(a, nm), x); strcat(row, x); strcat(row, " "); }
        CHECK(!strcmp(row, "C3 Db3 Eb3 E3 F3 G3 Ab3 Bb3 "), "scale mode adds the key's notes (%s)", row);
        /* second instance follows the first */
        b->setParameter(b, find_param(b, "Mode"), 0.f);
        b->setParameter(b, SP, 1.f);       /* follow other */
        b->setParameter(b, SN, 1.f);       /* bass */
        block(b, L, R, 256);
        midi(&eb.ev[0], 0, 0x90, 36 + 2, 100); send(a, &eb, 1); block(a, L, R, 256); /* A plays pad 3: Fm7 */
        block(b, L, R, 256);
        display(b, find_param(b, "Pad 1 chord"), x); display(b, find_param(b, "Pad 2 chord"), y);
        CHECK(!strcmp(x, "F1") && !strcmp(y, "Ab1"), "instance B's pads become an Fm7 bassline (%s %s)", x, y);
        midi(&eb.ev[0], 0, 0x90, 36, 100); send(b, &eb, 1);
        double eb2 = block(b, L, R, 256);
        display(b, NOW, x);
        CHECK(eb2 > 0 && strstr(x, "F1: root of Fm7"), "B plays the bass note (%s)", x);
        printf("  %s\n", x);
        midi(&eb.ev[0], 0, 0x80, 36, 0); send(b, &eb, 1);
        midi(&eb.ev[0], 0, 0x80, 36 + 2, 0); send(a, &eb, 1);
        /* B in follow mode doesn't take over: playing B leaves A's chord as the source */
        for (int i = 0; i < 1500; i++) { block(a, L, R, 256); block(b, L, R, 256); }
        CHECK(block(a, L, R, 256) == 0.0 && block(b, L, R, 256) == 0.0, "both instances silent after release");
        a->dispatcher(a, effClose, 0, 0, 0, 0);
        b->dispatcher(b, effClose, 0, 0, 0, 0);
    }

    /* Direct: one switch silences the built-in sound and sends to the port; switching off puts both back */
    {
        AEffect *d = open_fx();
        int DIR = find_param(d, "Direct"), SND = find_param(d, "Sound"), OUT = find_param(d, "MIDI out");
        char before_snd[64], before_out[64], s1[64], o1[64];
        CHECK(DIR > 0 && SND > 0 && OUT > 0, "Direct param exists");
        d->setParameter(d, SND, 1.f / 3.f);                 /* a named built-in sound */
        d->setParameter(d, OUT, 1.f / 3.f);                 /* MIDI out: To MPC */
        display(d, SND, before_snd); display(d, OUT, before_out);
        d->setParameter(d, DIR, 1.f);
        display(d, SND, s1); display(d, OUT, o1);
        CHECK(!strncmp(s1, "Silent", 6) && !strcmp(o1, "Virtual port"), "Direct on: sound '%s' and MIDI out '%s'", s1, o1);
        display(d, DIR, s1);
        CHECK(!strcmp(s1, "On"), "Direct shows On");
        d->setParameter(d, DIR, 1.f);                        /* a host re-sending the same value changes nothing */
        display(d, SND, s1);
        CHECK(!strncmp(s1, "Silent", 6), "re-sent Direct on keeps the Silent sound");
        d->setParameter(d, DIR, 0.f);
        display(d, SND, s1); display(d, OUT, o1);
        CHECK(!strcmp(s1, before_snd) && !strcmp(o1, before_out), "Direct off restores '%s' and '%s' (got '%s' and '%s')", before_snd, before_out, s1, o1);
        d->setParameter(d, DIR, 1.f);
        d->setParameter(d, SND, 0.f);                        /* a manual sound change takes Direct off */
        display(d, DIR, s1);
        CHECK(d->getParameter(d, DIR) == 0.f && !strcmp(s1, "Off"), "a manual sound change turns Direct off");
        d->dispatcher(d, effClose, 0, 0, 0, 0);
    }

    /* Detect: notes played are read into a scale, Use detected scale sets Key and Scale, Clear empties it */
    {
        AEffect *d = open_fx();
        int DET = find_param(d, "Detected"), CLR = find_param(d, "Clear detection"), APP = find_param(d, "Use detected scale");
        int KEY = find_param(d, "Key"), SCL = find_param(d, "Scale");
        float L[256], R[256];
        char t[160];
        CHECK(DET > 0 && CLR > 0 && APP > 0, "Detect params exist");
        display(d, DET, t);
        CHECK(strstr(t, "Play notes") != NULL, "Detected is empty at start ('%s')", t);
        EvBuf eb;
        midi(&eb.ev[0], 0, 0x90, 60, 100); midi(&eb.ev[1], 0, 0x90, 64, 100); midi(&eb.ev[2], 0, 0x90, 67, 100);
        send(d, &eb, 3);
        block(d, L, R, 256);
        display(d, DET, t);
        CHECK(strstr(t, "C Major") != NULL && strstr(t, "3 of 3") != NULL, "C E G detects as C Major ('%s')", t);
        d->setParameter(d, APP, 1.f);
        char kd[32], sd[64];
        display(d, KEY, kd); display(d, SCL, sd);
        CHECK(!strcmp(kd, "C") && !strcmp(sd, "Major"), "Use detected scale sets Key C and Scale Major (got '%s', '%s')", kd, sd);
        d->setParameter(d, CLR, 1.f);
        display(d, DET, t);
        CHECK(strstr(t, "Play notes") != NULL, "Clear empties the detection ('%s')", t);
        d->dispatcher(d, effClose, 0, 0, 0, 0);
    }

    /* Relative chord on the Edit page */
    {
        AEffect *d = open_fx();
        int REL = find_param(d, "Relative chord"), ER = find_param(d, "Edit root"), ET = find_param(d, "Edit type");
        char t1[64], t2[64];
        CHECK(REL > 0, "the Relative chord param exists");
        d->setParameter(d, ER, 0.f);                                       /* C */
        d->setParameter(d, ET, edit_type_of(""));                          /* Major */
        d->setParameter(d, REL, 1.f);
        display(d, ER, t1); display(d, ET, t2);
        CHECK(!strcmp(t1, "A") && strstr(t2, "Minor") != NULL, "relative of C major is A minor (got %s %s)", t1, t2);
        d->dispatcher(d, effClose, 0, 0, 0, 0);
    }

    /* a preset changes the notes the pads play: its voicing reaches the pads, not only the Voicing setting */
    {
        AEffect *v = open_fx();
        int PRE = find_param(v, "Preset"), NOW = find_param(v, "Now playing");
        float L[256], R[256];
        EvBuf eb;
        char base[160], cur[160];
        midi(&eb.ev[0], 0, 0x90, 36, 100); send(v, &eb, 1);   /* pad 1: its chord notes show on Now playing */
        block(v, L, R, 256);
        display(v, NOW, base);
        int changed = 0;
        for (int k = 1; k <= 900 && !changed; k += 7) {
            v->setParameter(v, PRE, (float)k / 900.f);
            block(v, L, R, 256);
            display(v, NOW, cur);
            if (strcmp(cur, base)) changed = k;
        }
        CHECK(changed > 0, "a preset changes the notes of pad 1 (base '%s', changed at preset %d)", base, changed);
        v->dispatcher(v, effClose, 0, 0, 0, 0);
    }

    /* the pads' notes text follows a preset: the key and voicing it sets change the notes */
    {
        AEffect *n = open_fx();
        int PRE = find_param(n, "Preset"), PN = find_param(n, "Pad 1 notes");
        char base[96], cur[96];
        CHECK(PN > 0, "Pad 1 notes param exists");
        display(n, PN, base);
        int changed = 0;
        for (int k = 2; k <= 900 && !changed; k += 13) {
            n->setParameter(n, PRE, (float)k / 900.f);
            display(n, PN, cur);
            if (strcmp(cur, base)) changed = k;
        }
        CHECK(changed > 0, "a preset changes what Pad 1 notes shows (base '%s', at %d)", base, changed);
        n->dispatcher(n, effClose, 0, 0, 0, 0);
    }

    /* live Pad Perform through the plugin: C major to D minor, from the host, remaps the pads on screen at once */
    {
        AEffect *d = open_fx();
        int PK = find_param(d, "Key"), PSC = find_param(d, "Scale"), PN = find_param(d, "Pad 1 notes"), PM = find_param(d, "Mode");
        float L[256], R[256];
        EvBuf eb;
        char before[96], after[96];
        CHECK(PK >= 0 && PSC >= 0 && PN >= 0 && PM >= 0, "key, scale, mode and Pad 1 notes params exist");
        d->setParameter(d, PK, 0.f); d->setParameter(d, PSC, 0.f);
        display(d, PN, before);
        midi(&eb.ev[0], 0, 0x90, 36, 100); send(d, &eb, 1); /* pad 1 sounds through the change */
        block(d, L, R, 256);
        { struct timespec ts = {0, 150 * 1000000L}; nanosleep(&ts, 0); } /* let the last redraw finish */
        int refresh0 = g_refresh_calls;
        d->setParameter(d, PK, 2.f / 11.f); d->setParameter(d, PSC, 1.f / 16.f); /* D, natural minor */
        { struct timespec ts = {0, 150 * 1000000L}; nanosleep(&ts, 0); } /* the host's redraw comes from the screen thread */
        int refresh1 = g_refresh_calls;
        block(d, L, R, 256);
        display(d, PN, after);
        CHECK(refresh1 > refresh0, "a key change from the host refreshes the pads on screen (%d calls)", refresh1 - refresh0);
        CHECK(strcmp(before, after) != 0 && strstr(after, "D") && strstr(after, "F") && strstr(after, "A"),
              "C major to D minor changes Pad 1 to a D minor chord (%s -> %s)", before, after);
        midi(&eb.ev[0], 0, 0x80, 36, 0); send(d, &eb, 1);
        block(d, L, R, 256);
        d->dispatcher(d, effClose, 0, 0, 0, 0);
    }

    /* key sync, MIDI import through the plugin, imports saved with the project */
    {
        AEffect *a = open_fx(), *b = open_fx();
        int KEY = find_param(a, "Key"), SCALE = find_param(a, "Scale"), SYNC = find_param(b, "Key sync");
        CHECK(SYNC > 0 && find_param(a, "Hold") > 0 && find_param(a, "Repeat") > 0 && find_param(a, "Melody") > 0, "tool params exist");
        b->setParameter(b, SYNC, 1.f);
        block(b, L, R, 256);
        a->setParameter(a, KEY, 5.f / 11.f);       /* F */
        a->setParameter(a, SCALE, value_of(a, SCALE, "Dorian"));
        block(b, L, R, 256);
        char x[96], y[96];
        display(b, KEY, x); display(b, SCALE, y);
        CHECK(!strcmp(x, "F") && !strcmp(y, "Dorian"), "copy B follows A's key and scale (%s %s)", x, y);
        /* import: write a MIDI file into <export dir>/Import, load it, pads show the chords */
        const char *dir = getenv("VC_EXPORT_DIR");
        if (dir) {
            char path[512];
            snprintf(path, sizeof(path), "%s/Import", dir); mkdir(path, 0775);
            snprintf(path, sizeof(path), "%s/Import/groove.mid", dir);
            /* hand-made SMF: Cmaj7 then Am7 then D7 */
            static const unsigned char mid[] = {
                'M','T','h','d',0,0,0,6,0,0,0,1,0x01,0xE0,
                'M','T','r','k',0,0,0,72,
                0,0x90,48,90, 0,0x90,52,90, 0,0x90,55,90, 0,0x90,59,90,
                0x83,0x60,0x80,48,0, 0,0x80,52,0, 0,0x80,55,0, 0,0x80,59,0,
                0,0x90,45,90, 0,0x90,48,90, 0,0x90,52,90, 0,0x90,55,90,
                0x83,0x60,0x80,45,0, 0,0x80,48,0, 0,0x80,52,0, 0,0x80,55,0,
                0,0xFF,0x2F,0 };
            FILE *mf = fopen(path, "wb"); fwrite(mid, 1, sizeof(mid), mf); fclose(mf);
            int LOAD = find_param(a, "Import load"), ST = find_param(a, "Import status"), PADS = find_param(a, "Pads play");
            a->setParameter(a, find_param(a, "Import next"), 1.f);
            a->setParameter(a, LOAD, 1.f);
            display(a, ST, x);
            CHECK(strstr(x, "Loaded 2 chords") != 0, "import loads the file (%s)", x);
            display(a, PADS, y);
            display(a, find_param(a, "Pad 1 chord"), x);
            CHECK(!strcmp(y, "Imported chords") && !strcmp(x, "Cmaj7"), "pads show imported chords (%s / %s)", y, x);
            display(a, find_param(a, "Pad 2 chord"), x);
            CHECK(!strcmp(x, "Am7"), "second imported chord (%s)", x);
            midi(&eb.ev[0], 0, 0x90, 36 + 1, 100); send(a, &eb, 1); block(a, L, R, 256);
            display(a, find_param(a, "Now playing"), x);
            CHECK(strstr(x, "Am7") && strstr(x, "A0 C1 E1 G1") == 0 && strstr(x, "A1"), "pad plays the file's exact notes (%s)", x);
            printf("  %s\n", x);
            midi(&eb.ev[0], 0, 0x80, 36 + 1, 0); send(a, &eb, 1); block(a, L, R, 256);
            /* save/reload */
            void *chunk = 0;
            int size = (int)a->dispatcher(a, effGetChunk, 0, 0, &chunk, 0);
            void *cp = malloc((size_t)size); memcpy(cp, chunk, (size_t)size);
            AEffect *c = open_fx();
            CHECK(c->dispatcher(c, effSetChunk, 0, size, cp, 0) == 1, "chunk with imports loads");
            display(c, find_param(c, "Pad 2 chord"), x);
            CHECK(!strcmp(x, "Am7"), "imported chords survive save/reload (%s)", x);
            free(cp);
            c->dispatcher(c, effClose, 0, 0, 0, 0);
        }
        a->dispatcher(a, effClose, 0, 0, 0, 0);
        b->dispatcher(b, effClose, 0, 0, 0, 0);
    }

    /* screen taps: refresh immediately on the UI thread; one-tap buttons count every tap; imports don't stick */
    {
        AEffect *s = open_fx();
        pthread_t at;
        struct { AEffect *fx; } arg = {s};
        pthread_create(&at, 0, audio_once_fn, &arg); pthread_join(at, 0);   /* plugin now knows its audio thread */
        int SNEXT = find_param(s, "Song next"), SONG = find_param(s, "Song");
        char a[96], b2[96];
        int before = g_refresh_calls;
        display(s, SONG, a);
        s->setParameter(s, SNEXT, 1.f);
        CHECK(g_refresh_calls > before, "a screen tap refreshes immediately (%d)", g_refresh_calls - before);
        display(s, SONG, b2);
        CHECK(strcmp(a, b2) != 0, "tap 1 steps the song");
        /* a host that never re-reads our reset flips its cached 1 back to 0 on the next tap: still a tap */
        struct timespec ts = {0, 300 * 1000000L}; nanosleep(&ts, 0);
        strcpy(a, b2);
        s->setParameter(s, SNEXT, 0.f);
        display(s, SONG, b2);
        CHECK(strcmp(a, b2) != 0, "tap 2 (host sent 0) still steps the song (%s -> %s)", a, b2);
        /* but a 1 followed at once by 0 is one tap (press + release) */
        strcpy(a, b2);
        s->setParameter(s, SNEXT, 1.f);
        s->setParameter(s, SNEXT, 0.f);
        display(s, SONG, b2);
        int steps = 0;
        for (float v = 0; v <= 1.f; v += 0.0001f) { (void)v; break; }
        CHECK(strcmp(a, b2) != 0, "press+release steps once");
        /* imported chords: choosing a mode afterwards shows that mode again */
        s->setParameter(s, find_param(s, "Pads play"), 1.f);
        s->setParameter(s, find_param(s, "Mode"), 1.f / 4.f);
        display(s, find_param(s, "Pads play"), a);
        CHECK(!strcmp(a, "Layout"), "picking a mode leaves imported chords (%s)", a);
        (void)steps;
        s->dispatcher(s, effClose, 0, 0, 0, 0);
    }

    /* learned pad layout through the plugin: survives save/reload; screen taps use it */
    {
        AEffect *s = open_fx();
        static const int SENDS[16] = {48, 49, 50, 51, 44, 45, 46, 47, 40, 41, 42, 43, 36, 37, 38, 39};
        int LA = find_param(s, "Learn all pads"), NOW = find_param(s, "Now playing");
        CHECK(LA > 0, "learn-all param exists");
        s->setParameter(s, LA, 1.f);
        char x[96];
        display(s, NOW, x);
        CHECK(strstr(x, "pad 1 of 16") != 0, "screen prompts for pad 1 (%s)", x);
        for (int k = 0; k < 16; k++) { midi(&eb.ev[0], 0, 0x90, SENDS[k], 100); midi(&eb.ev[1], 5, 0x80, SENDS[k], 0); send(s, &eb, 2); block(s, L, R, 256); }
        display(s, LA, x);
        CHECK(!strcmp(x, "Pads learned"), "all 16 learned (%s)", x);
        void *chunk = 0;
        int size = (int)s->dispatcher(s, effGetChunk, 0, 0, &chunk, 0);
        void *cp = malloc((size_t)size); memcpy(cp, chunk, (size_t)size);
        AEffect *c = open_fx();
        CHECK(c->dispatcher(c, effSetChunk, 0, size, cp, 0) == 1, "chunk with learned pads loads");
        free(cp);
        midi(&eb.ev[0], 0, 0x90, 36, 100); send(c, &eb, 1); block(c, L, R, 256);   /* physical pad 13 */
        display(c, NOW, x);
        CHECK(strstr(x, "A13") != 0, "after reload, MPC pad 13 still plays pad 13 (%s)", x);
        midi(&eb.ev[0], 0, 0x80, 36, 0); send(c, &eb, 1); block(c, L, R, 256);
        c->setParameter(c, find_param(c, "Screen bank"), 0.f);
        c->setParameter(c, find_param(c, "Screen pad"), 2.f / 16.f);   /* tap pad 2 on screen */
        block(c, L, R, 256); block(c, L, R, 256);
        display(c, NOW, x);
        CHECK(strstr(x, "A02") != 0, "screen tap auditions pad 2 through the learned layout (%s)", x);
        s->dispatcher(s, effClose, 0, 0, 0, 0);
        c->dispatcher(c, effClose, 0, 0, 0, 0);
    }

    /* screen refreshes must never happen on the audio thread (they stalled audio on the MPC) */
    {
        AEffect *s = open_fx();
        block(s, L, R, 256);                 /* the main thread is this instance's audio thread from here on */
        g_refresh_on_audio_thread = 0;
        int before = g_refresh_calls;
        for (int r = 0; r < 20; r++) {
            midi(&eb.ev[0], 0, 0x90, 36 + r % 8, 100); send(s, &eb, 1); block(s, L, R, 256);
            midi(&eb.ev[0], 0, 0x80, 36 + r % 8, 0); send(s, &eb, 1); block(s, L, R, 256);
        }
        struct timespec ts = {0, 150 * 1000000L};
        nanosleep(&ts, 0);
        CHECK(g_refresh_calls > before, "screen still refreshes after pad hits (%d)", g_refresh_calls - before);
        CHECK(g_refresh_calls - before <= 8, "refreshes are rate-limited (%d for 20 hits)", g_refresh_calls - before);
        CHECK(g_refresh_on_audio_thread == 0, "no refresh ever called from the audio thread (%d)", g_refresh_on_audio_thread);
        s->dispatcher(s, effClose, 0, 0, 0, 0);
    }

    /* demo render: play a song section pad by pad at 90 bpm (Just the Two of Us is song index 9 by year order) */
    if (demo) {
        AEffect *d = open_fx();
        int songCount = 0;
        for (float v = 0.f; v <= 1.f; v += 0.001f) { d->setParameter(d, P_SONG, v); display(d, P_SONG, buf); if (strstr(buf, "Just the Two of Us")) break; songCount++; }
        d->setParameter(d, P_SOUND, 0.f);
        d->setParameter(d, find_param(d, "Tone"), 0.45f);
        d->setParameter(d, find_param(d, "Release"), 0.35f);
        if (getenv("VC_DEMO_PATTERN")) { /* Neo-soul push, a little swing */
            int pp = find_param(d, "Pattern");
            for (float v = 0.f; v <= 1.f; v += 0.01f) { char nm[64]; d->setParameter(d, pp, v); display(d, pp, nm); if (!strcmp(nm, "Neo-soul push")) break; }
            d->setParameter(d, find_param(d, "Swing"), 0.25f);
            d->setParameter(d, find_param(d, "Gate"), 0.75f);
        }
        /* pads 1-5 = Dbmaj7 C7 Fm7 Ebm7 Ab7; beats 4 4 4 2 2 at 90 bpm, twice */
        int beats[5] = {4, 4, 4, 2, 2};
        int spb = 48000 * 60 / 90;
        int total = 2 * 16 * spb + 48000 * 2;
        short *pcm = (short *)calloc((size_t)total * 2, sizeof(short));
        int pos = 0, ev = 0;
        long sched[32]; int pad[32], on[32]; int ns = 0;
        long t = 0;
        for (int rep = 0; rep < 2; rep++) for (int i = 0; i < 5; i++) {
            sched[ns] = t; pad[ns] = i; on[ns++] = 1;
            t += beats[i] * spb;
            sched[ns] = t - 200; pad[ns] = i; on[ns++] = 0;
        }
        while (pos < total) {
            int m = 0;
            while (ev < ns && sched[ev] < pos + 256) {
                midi(&eb.ev[m++], (int)(sched[ev] - pos), on[ev] ? 0x90 : 0x80, 36 + pad[ev], on[ev] ? 88 : 0);
                ev++;
            }
            if (m) send(d, &eb, m);
            int nfr = total - pos < 256 ? total - pos : 256;
            block(d, L, R, nfr);
            for (int k = 0; k < nfr; k++) {
                pcm[2 * (pos + k)] = (short)(L[k] * 32000.f);
                pcm[2 * (pos + k) + 1] = (short)(R[k] * 32000.f);
            }
            pos += nfr;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s.wav", demo);
        wav_write(path, pcm, total);
        printf("  demo written to %s\n", path);
        free(pcm);
        d->dispatcher(d, effClose, 0, 0, 0, 0);
    }

    /* progressions through the plugin interface: the Progression control fills pads 1-4 */
    {
        int P_PROGC = find_param(fx, "Progression"), P_PAD2 = find_param(fx, "Pad 2 chord");
        CHECK(P_PROGC > 0 && P_PAD2 > 0, "progression and pad 2 parameters exist");
        fx->setParameter(fx, P_PROGC, 1.f / 12.f); /* template 1, I-V-vi-IV: Off, 8 templates, 4 user slots = 13 choices */
        display(fx, P_PROGC, buf);
        CHECK(!strcmp(buf, "I-V-vi-IV"), "progression shows its name (%s)", buf);
        display(fx, P_PAD2, buf);
        CHECK(strstr(buf, "G") != 0, "pad 2 plays the progression's V (%s)", buf);
        fx->setParameter(fx, P_PROGC, 0.f);
        display(fx, P_PROGC, buf);
        CHECK(!strcmp(buf, "Off"), "progression Off (%s)", buf);
        CHECK(find_param(fx, "Progression play") > 0, "progression play parameter exists");
        CHECK(find_param(fx, "Bassline") > 0, "bassline parameter exists");
        CHECK(find_param(fx, "Harmony") > 0, "harmony parameter exists");

        /* save the pads as progression slot 1, then recall it: the same chords come back on pads 1-4 */
        int P_SAVE = find_param(fx, "Save progression"), P_SLOT = find_param(fx, "Progression slot");
        char before[4][64], after[4][64];
        for (int k = 0; k < 4; k++) { char nm[32]; snprintf(nm, sizeof(nm), "Pad %d chord", k + 1); display(fx, find_param(fx, nm), before[k]); }
        CHECK(P_SAVE > 0 && P_SLOT > 0, "save and slot parameters exist");
        fx->setParameter(fx, P_SLOT, 0.f);
        fx->setParameter(fx, P_SAVE, 1.f); /* a press saves bank A into the slot */
        fx->setParameter(fx, P_PROGC, 9.f / 12.f); /* user slot 1 */
        display(fx, P_PROGC, buf);
        CHECK(!strcmp(buf, "User 1 (saved)"), "saved slot shows as saved (%s)", buf);
        for (int k = 0; k < 4; k++) { char nm[32]; snprintf(nm, sizeof(nm), "Pad %d chord", k + 1); display(fx, find_param(fx, nm), after[k]); }
        int same = 1;
        for (int k = 0; k < 4; k++) if (strcmp(before[k], after[k])) same = 0;
        CHECK(same, "recalling the slot restores pads 1-4 (%s %s %s %s)", after[0], after[1], after[2], after[3]);
        fx->setParameter(fx, P_PROGC, 0.f);

        /* the saved slot survives a save and reload */
        void *ck = 0;
        int csize = (int)fx->dispatcher(fx, effGetChunk, 0, 0, &ck, 0);
        void *cc = malloc((size_t)csize); memcpy(cc, ck, (size_t)csize);
        AEffect *fx3 = open_fx();
        CHECK(fx3->dispatcher(fx3, effSetChunk, 0, csize, cc, 0) == 1, "chunk with saved progressions loads");
        fx3->setParameter(fx3, find_param(fx3, "Progression"), 9.f / 12.f);
        int same2 = 1;
        for (int k = 0; k < 4; k++) {
            char nm[32], d[64];
            snprintf(nm, sizeof(nm), "Pad %d chord", k + 1);
            display(fx3, find_param(fx3, nm), d);
            if (strcmp(d, before[k])) same2 = 0;
        }
        CHECK(same2, "a reloaded project recalls the same saved chords");
        free(cc);
    }

    /* user presets: save your settings, change them, load them back; they also survive a save and reload */
    {
        int P_UPS = find_param(fx, "User preset slot"), P_UPSV = find_param(fx, "Save user preset"), P_UPLD = find_param(fx, "Load user preset");
        int P_T = find_param(fx, "Tone"), P_SN = find_param(fx, "Sound"), P_SW = find_param(fx, "Swing");
        CHECK(P_UPS > 0 && P_UPSV > 0 && P_UPLD > 0, "user preset parameters exist");
        fx->setParameter(fx, P_UPS, 1.f / 3.f); /* slot 2 */
        display(fx, P_UPS, buf);
        CHECK(!strcmp(buf, "Slot 2 (empty)"), "an unused slot shows as empty (%s)", buf);
        fx->setParameter(fx, P_T, 0.9f);
        fx->setParameter(fx, P_SN, 1.f); /* Silent: a user preset keeps it, because you chose it */
        fx->setParameter(fx, P_SW, 0.5f);
        char want_tone[64], want_sound[64], want_swing[64];
        display(fx, P_T, want_tone); display(fx, P_SN, want_sound); display(fx, P_SW, want_swing);
        fx->setParameter(fx, P_UPSV, 1.f);
        display(fx, P_UPS, buf);
        CHECK(!strcmp(buf, "Slot 2 (saved)"), "a saved slot shows as saved (%s)", buf);
        fx->setParameter(fx, P_T, 0.1f); fx->setParameter(fx, P_SN, 0.f); fx->setParameter(fx, P_SW, 0.f);
        fx->setParameter(fx, P_UPLD, 1.f);
        char got[64];
        display(fx, P_T, got);   CHECK(!strcmp(got, want_tone), "loading restores Tone (%s vs %s)", got, want_tone);
        display(fx, P_SN, got);  CHECK(!strcmp(got, want_sound), "loading restores the sound, Silent included (%s)", got);
        display(fx, P_SW, got);  CHECK(!strcmp(got, want_swing), "loading restores Swing (%s)", got);
        /* save and reload the project: the slot is still there */
        void *ck = 0;
        int csize = (int)fx->dispatcher(fx, effGetChunk, 0, 0, &ck, 0);
        void *cc = malloc((size_t)csize); memcpy(cc, ck, (size_t)csize);
        AEffect *fx4 = open_fx();
        CHECK(fx4->dispatcher(fx4, effSetChunk, 0, csize, cc, 0) == 1, "chunk with user presets loads");
        int P_UPS4 = find_param(fx4, "User preset slot");
        display(fx4, P_UPS4, got);
        CHECK(!strcmp(got, "Slot 2 (saved)"), "the saved slot survives a reload (%s)", got);
        fx4->setParameter(fx4, find_param(fx4, "Tone"), 0.3f);
        fx4->setParameter(fx4, find_param(fx4, "Load user preset"), 1.f);
        display(fx4, find_param(fx4, "Tone"), got);
        CHECK(!strcmp(got, want_tone), "a reloaded project loads the same settings (%s)", got);
        free(cc);

        /* preset files: export, overwrite the slot, import it back; bad files are refused and change nothing */
        const char *dir = getenv("VC_EXPORT_DIR");
        int P_EXP = find_param(fx, "Export user preset"), P_IMP = find_param(fx, "Import user preset"), P_ST = find_param(fx, "User preset status");
        CHECK(P_EXP > 0 && P_IMP > 0 && P_ST > 0, "export, import and status parameters exist");
        if (dir) {
            char path[600], text[4096];
            snprintf(path, sizeof(path), "%s/user-preset-2.vcpreset", dir);
            fx->setParameter(fx, P_EXP, 1.f);
            display(fx, P_ST, got);
            CHECK(!strcmp(got, "Exported slot 2"), "export reports (%s)", got);
            FILE *pf = fopen(path, "r");
            size_t nread = pf ? fread(text, 1, sizeof(text) - 1, pf) : 0;
            if (pf) fclose(pf);
            text[nread] = 0;
            CHECK(!strncmp(text, "VELVET-PRESET 2\n", 16) && strstr(text, "Tone=0.9") && strstr(text, "Sound=1.0"), "the file has the header and the settings");

            fx->setParameter(fx, P_T, 0.2f);
            fx->setParameter(fx, P_UPSV, 1.f); /* the slot now holds Tone 20% */
            fx->setParameter(fx, P_IMP, 1.f);
            display(fx, P_ST, got);
            CHECK(strstr(got, "Imported") != 0, "import reports (%s)", got);
            fx->setParameter(fx, P_T, 0.5f);
            fx->setParameter(fx, P_UPLD, 1.f);
            display(fx, P_T, got);
            CHECK(!strcmp(got, want_tone), "an imported slot loads the exported settings (%s vs %s)", got, want_tone);

            pf = fopen(path, "w"); fputs("this is not a preset\nTone=0.1\n", pf); fclose(pf);
            fx->setParameter(fx, P_IMP, 1.f);
            display(fx, P_ST, got);
            CHECK(!strcmp(got, "Not a Velvet preset file"), "a file without the header is refused (%s)", got);
            fx->setParameter(fx, P_T, 0.5f);
            fx->setParameter(fx, P_UPLD, 1.f);
            display(fx, P_T, got);
            CHECK(!strcmp(got, want_tone), "a refused file leaves the slot as it was (%s)", got);

            pf = fopen(path, "w"); fputs("VELVET-PRESET 9\nTone=0.1\n", pf); fclose(pf);
            fx->setParameter(fx, P_IMP, 1.f);
            display(fx, P_ST, got);
            CHECK(!strcmp(got, "Made by a newer version"), "a newer file version is refused (%s)", got);

            pf = fopen(path, "w"); fputs("VELVET-PRESET 1\nTone=7\nSwing=0.5\nNoSuchSetting=0.3\nSound=nan\n", pf); fclose(pf);
            fx->setParameter(fx, P_IMP, 1.f);
            display(fx, P_ST, got);
            CHECK(!strcmp(got, "Imported 1 settings, skipped 2"), "out-of-range and non-numeric values are skipped, unknown names ignored (%s)", got);

            /* a version 1 file stores Scale out of 16: 9/16 was Major pentatonic before the scale list grew */
            pf = fopen(path, "w"); fprintf(pf, "VELVET-PRESET 1\nScale=%.6f\n", 9.0 / 16.0); fclose(pf);
            fx->setParameter(fx, P_IMP, 1.f);
            display(fx, P_ST, got);
            fx->setParameter(fx, P_UPLD, 1.f);
            char sc[64];
            display(fx, find_param(fx, "Scale"), sc);
            CHECK(!strcmp(sc, "Major pentatonic"), "a version 1 preset keeps its scale (%s)", sc);
        }
        fx->setParameter(fx, P_UPS, 2.f / 3.f); /* slot 3 was never saved */
        fx->setParameter(fx, P_EXP, 1.f);
        display(fx, P_ST, got);
        CHECK(!strcmp(got, "Slot 3 is empty: Save first"), "exporting an empty slot says so (%s)", got);
        fx->setParameter(fx, P_UPS, 1.f / 3.f);
        fx->setParameter(fx, P_SN, 0.f); fx->setParameter(fx, P_T, 0.45f); fx->setParameter(fx, P_SW, 0.f);
    }

    /* sample analysis: a WAV in the Analyze folder becomes a progression on the pads; a filled slot is never overwritten */
    {
        const char *edir = getenv("VC_EXPORT_DIR");
        int P_AN = find_param(fx, "Analyze sample"), P_ANS = find_param(fx, "Analysis status"), P_UCL = find_param(fx, "Clear progression");
        int P_UPS2 = find_param(fx, "Progression slot"), P_PRG = find_param(fx, "Progression"), P_P1 = find_param(fx, "Pad 1 chord");
        CHECK(P_AN > 0 && P_ANS > 0 && P_UCL > 0, "analysis parameters exist");
        if (edir) {
            char adir[600], apath[700];
            snprintf(adir, sizeof(adir), "%s/Analyze", edir);
            mkdir(adir, 0775);
            snprintf(apath, sizeof(apath), "%s/sample.wav", adir);
            /* a four-chord WAV: Cmaj7, Am7, Fmaj7, G7, each 1.5 s, with the bass an octave down */
            const int rate = 44100, secs = 6;
            static const int ROOT[4] = {0, 9, 5, 7};
            static const int TONES[4][4] = {{0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 4, 7, 10}};
            int nsamp = rate * secs;
            short *pcm = (short *)calloc((size_t)nsamp, sizeof(short));
            for (int i = 0; i < nsamp; i++) {
                double t = (double)i / rate, acc = 0;
                int seg = (int)(t / 1.5); if (seg > 3) seg = 3;
                double f0 = 440.0 * pow(2.0, (36 + ROOT[seg] - 69) / 12.0);
                acc += 0.4 * sin(2 * M_PI * f0 * t);
                for (int k = 0; k < 4; k++) {
                    double f = 440.0 * pow(2.0, (60 + (ROOT[seg] + TONES[seg][k]) % 12 - 69) / 12.0);
                    acc += 0.15 * sin(2 * M_PI * f * t) + 0.05 * sin(4 * M_PI * f * t);
                }
                pcm[i] = (short)(acc * 12000);
            }
            FILE *wf = fopen(apath, "wb");
            if (wf) {
                unsigned dsz = (unsigned)(nsamp * 2), rsz = 36 + dsz, sr = rate, br = rate * 2;
                unsigned char h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,2,0,16,0,'d','a','t','a',0,0,0,0};
                for (int k = 0; k < 4; k++) { h[4 + k] = (rsz >> (8 * k)) & 255; h[40 + k] = (dsz >> (8 * k)) & 255; h[24 + k] = (sr >> (8 * k)) & 255; h[28 + k] = (br >> (8 * k)) & 255; }
                fwrite(h, 1, 44, wf);
                fwrite(pcm, 2, (size_t)nsamp, wf);
                fclose(wf);
            }
            free(pcm);
            char st[160];
            fx->setParameter(fx, P_UPS2, 0.f);          /* slot 1 */
            fx->setParameter(fx, P_UCL, 1.f);           /* start from an empty slot */
            fx->setParameter(fx, P_AN, 1.f);
            display(fx, P_ANS, st);
            CHECK(strstr(st, "chords in progression 1") != 0, "analysis fills progression 1 (%s)", st);
            fx->setParameter(fx, P_PRG, 9.f / 12.f);    /* User progression 1 */
            display(fx, P_P1, st);
            CHECK(strstr(st, "maj7") != 0 || strstr(st, "Cmaj7") != 0, "pad 1 plays the first detected chord (%s)", st);
            fx->setParameter(fx, P_PRG, 0.f);
            fx->setParameter(fx, P_AN, 1.f);            /* a filled slot is not overwritten */
            display(fx, P_ANS, st);
            CHECK(strstr(st, "Clear progression first") != 0, "a filled slot is refused (%s)", st);
            fx->setParameter(fx, P_UCL, 1.f);
            display(fx, P_ANS, st);
            CHECK(strstr(st, "Cleared progression 1") != 0, "clearing empties the slot (%s)", st);
            { /* a renamed export is still found: the first WAV in the folder is used */
                char renamed[700];
                snprintf(renamed, sizeof(renamed), "%s/my song.wav", adir);
                rename(apath, renamed);
            }
            fx->setParameter(fx, P_AN, 1.f);
            display(fx, P_ANS, st);
            CHECK(strstr(st, "chords in progression 1") != 0, "a renamed WAV in the folder is analysed (%s)", st);
            fx->setParameter(fx, P_UCL, 1.f);
        }
    }

    /* starred presets, and clearing a user preset slot */
    {
        int P_PR = find_param(fx, "Preset"), P_FT = find_param(fx, "Star preset"), P_FP = find_param(fx, "Previous starred preset");
        int P_FN = find_param(fx, "Next starred preset"), P_FS = find_param(fx, "Starred preset status");
        CHECK(P_PR > 0 && P_FT > 0 && P_FP > 0 && P_FN > 0 && P_FS > 0, "starred preset parameters exist");
        char n5[64], n20[64], got[96];
        fx->setParameter(fx, P_PR, 5.f / 900.f); display(fx, P_PR, n5);
        fx->setParameter(fx, P_FT, 1.f);
        display(fx, P_FS, got);
        CHECK(!strcmp(got, "Starred. 1 starred"), "starring shows in the status (%s)", got);
        fx->setParameter(fx, P_PR, 20.f / 900.f); display(fx, P_PR, n20);
        fx->setParameter(fx, P_FT, 1.f);
        fx->setParameter(fx, P_PR, 1.f / 900.f);
        fx->setParameter(fx, P_FN, 1.f); display(fx, P_PR, got);
        CHECK(!strcmp(got, n5), "next starred from preset 1 is preset 5 (%s vs %s)", got, n5);
        fx->setParameter(fx, P_FN, 1.f); display(fx, P_PR, got);
        CHECK(!strcmp(got, n20), "next starred goes on to preset 20 (%s)", got);
        fx->setParameter(fx, P_FN, 1.f); display(fx, P_PR, got);
        CHECK(!strcmp(got, n5), "next starred wraps round to preset 5 (%s)", got);
        fx->setParameter(fx, P_FP, 1.f); display(fx, P_PR, got);
        CHECK(!strcmp(got, n20), "previous starred wraps back to preset 20 (%s)", got);

        /* the stars survive a save and reload */
        void *ck = 0;
        int csize = (int)fx->dispatcher(fx, effGetChunk, 0, 0, &ck, 0);
        void *cc = malloc((size_t)csize); memcpy(cc, ck, (size_t)csize);
        AEffect *fx5 = open_fx();
        CHECK(fx5->dispatcher(fx5, effSetChunk, 0, csize, cc, 0) == 1, "chunk with stars loads");
        fx5->setParameter(fx5, find_param(fx5, "Preset"), 5.f / 900.f);
        display(fx5, find_param(fx5, "Starred preset status"), got);
        CHECK(!strcmp(got, "Starred. 2 starred"), "stars survive a reload (%s)", got);
        free(cc);

        fx->setParameter(fx, P_FT, 1.f); /* un-star preset 20 */
        display(fx, P_FS, got);
        CHECK(!strcmp(got, "Not starred. 1 starred"), "toggling again un-stars (%s)", got);
        fx->setParameter(fx, P_PR, 5.f / 900.f); fx->setParameter(fx, P_FT, 1.f); /* un-star preset 5: none left */
        fx->setParameter(fx, P_PR, 1.f / 900.f); fx->setParameter(fx, P_FN, 1.f); display(fx, P_PR, got);
        char n1[64]; display(fx, P_PR, n1);
        CHECK(!strcmp(got, n1) && fx->getParameter(fx, P_PR) > 0.f, "with nothing starred, next does nothing");
        fx->setParameter(fx, P_PR, 0.f);

        /* clearing a user preset slot */
        int P_US = find_param(fx, "User preset slot"), P_CL = find_param(fx, "Clear user preset");
        fx->setParameter(fx, P_US, 1.f / 3.f); /* slot 2 holds a preset from the tests above */
        display(fx, P_US, got);
        CHECK(!strcmp(got, "Slot 2 (saved)"), "slot 2 holds a preset before clearing (%s)", got);
        fx->setParameter(fx, P_CL, 1.f);
        display(fx, P_US, got);
        CHECK(!strcmp(got, "Slot 2 (empty)"), "clearing empties the slot (%s)", got);
        display(fx, find_param(fx, "User preset status"), got);
        CHECK(!strcmp(got, "Cleared slot 2"), "clearing reports (%s)", got);
        fx->setParameter(fx, P_US, 0.f);
    }

    /* Velvet Chords 1.1 features through the plugin interface: presets, riffs, shift keys, note editor, state */
    {
        int P_PRE = find_param(fx, "Preset"), P_PNX = find_param(fx, "Preset next"), P_TN = find_param(fx, "Tone");
        int P_RIFF = find_param(fx, "Riff"), P_RSL = find_param(fx, "Riff slot"), P_RS1 = find_param(fx, "Riff step 1");
        int P_ARPO = find_param(fx, "Arp octaves"), P_A2 = find_param(fx, "Arp 2"), P_SH = find_param(fx, "Shift keys");
        int P_NSEL = find_param(fx, "Edit note"), P_NIV = find_param(fx, "Edit note interval"), P_KEYI = find_param(fx, "Key");
        CHECK(P_PRE > 0 && P_PNX > 0 && P_RIFF > 0 && P_RSL > 0 && P_RS1 > 0 && P_ARPO > 0 && P_A2 > 0 && P_SH > 0 && P_NSEL > 0 && P_NIV > 0 && P_KEYI > 0,
              "1.1 parameters exist");
        fx->setParameter(fx, P_PRE, 1.f / 900.f);
        display(fx, P_PRE, buf);
        CHECK(!strcmp(buf, "Neo-Soul Keys: Late"), "preset 1 is shown by name (%s)", buf);
        { /* a preset never turns the arpeggio on: it only sets one that is already running */
            int P_ARPX = find_param(fx, "Arp"), bad = 0;
            AEffect *q = open_fx();
            for (int k = 0; k < 900; k += 37) {
                q->setParameter(q, find_param(q, "Preset"), (float)k / 900.f);
                char a[64]; display(q, P_ARPX, a);
                if (strcmp(a, "Off")) bad++;
            }
            CHECK(bad == 0, "presets leave a fresh instance's arpeggio Off (%d presets were on)", bad);
            q->dispatcher(q, effClose, 0, 0, 0, 0);
            (void)P_ARPX;
        }
        { /* the preset sets the genre's key and scale (Neo-Soul Keys: F Dorian) and leaves the chords alone */
            int P_KEY_ = find_param(fx, "Key"), P_SCALE_ = find_param(fx, "Scale");
            display(fx, P_KEY_, buf);
            CHECK(!strcmp(buf, "F"), "preset sets the key (%s)", buf);
            display(fx, P_SCALE_, buf);
            CHECK(!strcmp(buf, "Dorian"), "preset sets the scale (%s)", buf);
        }
        display(fx, P_TN, buf);
        CHECK(!strcmp(buf, "45%"), "preset sets Tone (%s)", buf);
        fx->setParameter(fx, P_PNX, 1.f);
        display(fx, P_PRE, buf);
        CHECK(!strcmp(buf, "Neo-Soul Keys: Warm"), "Preset next steps to preset 2 (%s)", buf);
        fx->setParameter(fx, P_PRE, 0.f);
        display(fx, P_PRE, buf);
        CHECK(!strcmp(buf, "Manual"), "Manual is shown (%s)", buf);

        /* the genre and variation knobs reach every preset: genre 3 (of 23) and variation 5 (of 10) is preset 25 */
        int P_GEN = find_param(fx, "Preset genre"), P_VAR = find_param(fx, "Preset variation");
        CHECK(P_GEN > 0 && P_VAR > 0, "genre and variation knobs exist");
        fx->setParameter(fx, P_GEN, 3.f / 23.f);
        fx->setParameter(fx, P_VAR, 4.f / 9.f);
        char gname[160];
        display(fx, P_GEN, gname);
        display(fx, P_PRE, buf);
        CHECK(!strncmp(buf, gname, strlen(gname)) && buf[strlen(gname)] == ':' && !strcmp(buf + strlen(gname) + 2, "Dusty"),
              "genre and variation pick a preset (%s)", buf);
        fx->setParameter(fx, P_PNX, 1.f); /* the stepper moves on, and the knobs follow it */
        display(fx, P_VAR, buf);
        CHECK(strstr(buf, "Sparkle") != NULL, "variation knob follows the stepper (%s)", buf);
        fx->setParameter(fx, P_GEN, 0.f);
        display(fx, P_PRE, buf);
        CHECK(!strcmp(buf, "Manual"), "genre Manual turns the presets off (%s)", buf);
        fx->setParameter(fx, P_PRE, 0.f);

        /* a host that re-sends the stored preset must not undo a sound chosen after it */
        int P_SND2 = find_param(fx, "Sound");
        fx->setParameter(fx, P_PRE, 1.f / 900.f);
        fx->setParameter(fx, P_SND2, 1.f);
        fx->setParameter(fx, P_PRE, 1.f / 900.f);
        display(fx, P_SND2, buf);
        CHECK(!strcmp(buf, "Silent (MIDI only)"), "re-sent preset keeps the chosen sound (%s)", buf);

        /* the reverb: at 0 it adds nothing, and a note's tail dies away */
        {
            static float RL[256], RR[256];
            int P_RV = find_param(fx, "Reverb");
            fx->setParameter(fx, P_RV, 0.f);
            midi(&eb.ev[0], 0, 0x90, 60, 100); send(fx, &eb, 1); block(fx, RL, RR, 256);
            midi(&eb.ev[0], 0, 0x80, 60, 0); send(fx, &eb, 1);
            fx->setParameter(fx, P_RV, 0.6f);
            double tail0 = 0, tail1 = 0;
            for (int b = 0; b < 200; b++) { block(fx, RL, RR, 256); }
            for (int b = 0; b < 40; b++) { block(fx, RL, RR, 256); }
            for (int b = 0; b < 40; b++) { double e = block(fx, RL, RR, 256); tail0 += e; }
            for (int b = 0; b < 40; b++) { double e = block(fx, RL, RR, 256); tail1 += e; }
            CHECK(tail0 > 0.0 && tail1 < tail0, "reverb tail after a note falls away (%g then %g)", tail0, tail1);
            fx->setParameter(fx, P_RV, 0.f);
            for (int b = 0; b < 1500; b++) block(fx, RL, RR, 256);   /* let the note's own release finish */
            double after = block(fx, RL, RR, 256);
            CHECK(after == 0.0, "reverb at 0 leaves silence silent");
        }

        /* a preset leaves the instrument alone: the Sound you chose stays after loading any preset */
        {
            char buf2[256];
            int P_SN = find_param(fx, "Sound"), P_SXN = find_param(fx, "Sound extra");
            fx->setParameter(fx, P_SXN, 0.f);
            fx->setParameter(fx, P_SN, 17.f / 19.f); display(fx, P_SN, buf);      /* Pluck */
            fx->setParameter(fx, P_PRE, 150.f / 900.f);                          /* a preset from another genre */
            display(fx, P_SN, buf2);
            CHECK(!strcmp(buf2, "Pluck"), "a preset keeps the Sound you chose (%s)", buf2);
            fx->setParameter(fx, P_SXN, 2.f / 4.f); fx->setParameter(fx, P_PRE, 151.f / 900.f);
            CHECK(fx->getParameter(fx, P_SXN) == 2.f / 4.f, "a preset keeps Sound extra too");
            fx->setParameter(fx, P_SXN, 0.f);
            fx->setParameter(fx, P_SN, 1.f);   /* back to Silent, as the note tests below expect */
        }
        /* a preset leaves the pattern you chose alone */
        {
            int P_PATT = find_param(fx, "Pattern"), P_PN = find_param(fx, "Pattern next");
            fx->setParameter(fx, P_PATT, 0.f);
            for (int k = 0; k < 3; k++) fx->setParameter(fx, P_PN, 1.f);
            fx->setParameter(fx, P_PRE, 40.f / 900.f);
            display(fx, P_PATT, buf);
            CHECK(!strcmp(buf, "Quarters"), "a preset keeps the pattern you chose (%s)", buf);
        }
        /* the host's preset box: Manual plus all 900 presets, past the first hundred, and picking one sets the Preset knob */
        CHECK(fx->numPrograms == 901, "host preset box lists 901 programs (%d)", fx->numPrograms);
        char pn[64], pk[64];
        CHECK(fx->dispatcher(fx, effGetProgramNameIndexed, 101, 0, pn, 0) == 1, "program 101 has a name");
        fx->setParameter(fx, P_PRE, 101.f / 900.f); display(fx, P_PRE, pk);
        CHECK(!strcmp(pn, pk), "program 101 is the 101st preset (%s / %s)", pn, pk);
        CHECK(fx->dispatcher(fx, effGetProgramNameIndexed, 901, 0, pn, 0) == 0, "no program past the last preset");
        fx->dispatcher(fx, effSetProgram, 0, 600, 0, 0); display(fx, P_PRE, pk);
        CHECK(!strcmp(pk, "S. Bounce: Wide") && fx->dispatcher(fx, effGetProgram, 0, 0, 0, 0) == 600, "host picks program 600 (%s)", pk);
        fx->dispatcher(fx, effSetProgram, 0, 0, 0, 0); display(fx, P_PRE, pk);
        CHECK(!strcmp(pk, "Manual"), "program 0 is Manual (%s)", pk);
        fx->dispatcher(fx, effSetProgram, 0, 600, 0, 0);

        /* no preset makes sound or arpeggio you didn't choose: Silent stays, and Arp stays off unless you turned it on */
        int P_ARP_ = find_param(fx, "Arp"), bad_sound = 0, bad_arp = 0;
        fx->setParameter(fx, P_ARP_, 0.f);
        for (int k = 1; k <= 900; k++) {
            fx->setParameter(fx, P_PRE, k / 900.f);
            display(fx, P_SND2, buf);
            bad_sound += strcmp(buf, "Silent (MIDI only)") != 0;
            display(fx, P_ARP_, buf);
            bad_arp += strcmp(buf, "Off") != 0;
        }
        CHECK(bad_sound == 0, "no preset replaces Silent (%d presets did)", bad_sound);
        CHECK(bad_arp == 0, "no preset turns the arpeggio on (%d presets did)", bad_arp);
        fx->setParameter(fx, P_SND2, 0.f);
        fx->setParameter(fx, P_PRE, 0.f);

        fx->setParameter(fx, P_ARPO, 1.f / 3.f);
        display(fx, P_ARPO, buf);
        CHECK(!strcmp(buf, "2 oct"), "arp octaves (%s)", buf);
        fx->setParameter(fx, P_A2, 2.f / 6.f);
        display(fx, P_A2, buf);
        CHECK(!strcmp(buf, "Down"), "arp 2 order (%s)", buf);
        display(fx, P_SH, buf);
        CHECK(!strcmp(buf, "On"), "shift keys are on by default (%s)", buf);

        fx->setParameter(fx, P_RSL, 1.f / 5.f); /* riff slot 2: Octave bounce */
        display(fx, P_RS1, buf);
        CHECK(!strcmp(buf, "+0 st"), "riff step 1 shows its offset (%s)", buf);
        fx->setParameter(fx, P_RS1 + 1, 0.f); /* step 2: a rest */
        display(fx, P_RS1 + 1, buf);
        CHECK(!strcmp(buf, "Rest"), "riff step 2 is a rest (%s)", buf);
        fx->setParameter(fx, P_RS1 + 2, (5.f + 13.f) / 37.f);
        display(fx, P_RS1 + 2, buf);
        CHECK(!strcmp(buf, "+5 st"), "riff step 3 is +5 (%s)", buf);
        fx->setParameter(fx, P_RIFF, 2.f / 6.f);
        display(fx, P_RIFF, buf);
        CHECK(!strcmp(buf, "Octave bounce"), "riff plays slot 2 (%s)", buf);

        fx->setParameter(fx, find_param(fx, "Edit pad"), 5.f / 63.f);
        fx->setParameter(fx, find_param(fx, "Edit root"), 2.f / 11.f);
        fx->setParameter(fx, find_param(fx, "Edit type"), edit_type_of("m9"));
        fx->setParameter(fx, P_NSEL, 1.f / 7.f);
        fx->setParameter(fx, P_NIV, (7.f + 13.f) / 37.f);
        display(fx, P_NIV, buf);
        CHECK(!strcmp(buf, "+7 st"), "note 2 is set to +7 (%s)", buf);
        fx->setParameter(fx, P_NIV, 0.f);
        display(fx, P_NIV, buf);
        CHECK(!strcmp(buf, "+7 st"), "note 2 removed, the note above moves down (%s)", buf);

        float k0 = fx->getParameter(fx, P_KEYI);
        int i0 = (int)(k0 * 11.f + 0.5f);
        midi(&eb.ev[0], 0, 0x90, 25, 100); send(fx, &eb, 1);
        block(fx, L, R, 256);
        float k1 = fx->getParameter(fx, P_KEYI);
        CHECK((int)(k1 * 11.f + 0.5f) == (i0 + 11) % 12, "C# moves the key down (%d -> %d)", i0, (int)(k1 * 11.f + 0.5f));

        void *c2 = 0;
        int size2 = (int)fx->dispatcher(fx, effGetChunk, 0, 0, &c2, 0);
        void *copy2 = malloc((size_t)size2);
        memcpy(copy2, c2, (size_t)size2);
        AEffect *fx3 = open_fx();
        CHECK(fx3->dispatcher(fx3, effSetChunk, 0, size2, copy2, 0) == 1, "1.1 chunk loads");
        display(fx3, P_RS1 + 2, buf);
        CHECK(!strcmp(buf, "+5 st"), "riff steps restored from state (%s)", buf);
        fx3->setParameter(fx3, P_RSL, 0.f);
        display(fx3, P_RS1, buf);
        CHECK(!strcmp(buf, "+0 st"), "riff slot 1 keeps its own steps (%s)", buf);
        free(copy2);
        fx3->dispatcher(fx3, effClose, 0, 0, 0, 0);
    }

    fx->dispatcher(fx, effClose, 0, 0, 0, 0);
    fx2->dispatcher(fx2, effClose, 0, 0, 0, 0);
    const char *fake = getenv("VC_FAKE_ALSA_LOG");
    if (fake) {
        FILE *f = fopen(fake, "r");
        char line[256];
            int all[16][128];
            memset(all, 0, sizeof(all));
            while (f && fgets(line, sizeof(line), f)) {
                int ty, ch, note, vel, q, dc, dp, sp, sz;
                if (sscanf(line, "ev %d %d %d %d %d %d %d %d %d", &ty, &ch, &note, &vel, &q, &dc, &dp, &sp, &sz) == 9 && ch < 16 && note < 128)
                    all[ch][note] += ty == 6 ? 1 : ty == 7 ? -1 : 0;
            }
            if (f) fclose(f);
            int hanging = 0;
            for (int c2 = 0; c2 < 16; c2++) for (int k = 0; k < 128; k++) if (all[c2][k] > 0) hanging++;
            CHECK(hanging == 0, "no instance leaves notes hanging on the port, even when closed mid-chord (%d)", hanging);
    }
    if (g_probe_count) CHECK(*g_probe_count == 0, "no heap allocation or free inside processReplacing or effProcessEvents (%ld calls in %ld blocks)", *g_probe_count, g_probe_blocks);
    else printf("  (allocation probe not loaded: run through make test to check heap use on the audio path)\n");
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
