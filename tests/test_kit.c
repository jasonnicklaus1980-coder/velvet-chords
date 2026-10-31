/* Tests the drum kit: WAV decoding, the kit loader and the sample player. Usage: test_kit <scratch folder> */
#include "kit.h"
#include "vcmath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int checks, failures;

#define CHECK(cond, what) do { checks++; if (!(cond)) { failures++; printf("  FAIL: %s\n", what); } } while (0)

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, unsigned long v) { put16(f, v & 0xFFFF); put16(f, (v >> 16) & 0xFFFF); }

/* writes a WAV whose data is given as raw bytes */
static int write_wav(const char *path, unsigned fmt_tag, unsigned ch, unsigned rate, unsigned bits, const unsigned char *data, size_t nbytes) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite("RIFF", 1, 4, f); put32(f, 4 + 24 + 8 + nbytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16);
    put16(f, fmt_tag); put16(f, ch); put32(f, rate); put32(f, rate * ch * bits / 8);
    put16(f, ch * bits / 8); put16(f, bits);
    fwrite("data", 1, 4, f); put32(f, nbytes);
    fwrite(data, 1, nbytes, f);
    fclose(f);
    return 0;
}

static const char *XPM =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<MPCVObject>\n"
    "  <Version><File_Version>1.0</File_Version></Version>\n"
    "  <Program type=\"Drum\">\n"
    "    <Name>Test Kit</Name>\n"
    "    <Instruments>\n"
    "      <Instrument number=\"1\">\n"
    "        <OneShot>True</OneShot>\n"
    "        <Layers>\n"
    "          <Layer number=\"1\"><VelStart>0</VelStart><VelEnd>63</VelEnd><SampleName>KICK</SampleName><Volume>0.784333</Volume><Pan>0.5</Pan></Layer>\n"
    "          <Layer number=\"2\"><VelStart>64</VelStart><VelEnd>127</VelEnd><SampleName>SNARE</SampleName><Volume>0.784333</Volume><Pan>0.5</Pan></Layer>\n"
    "        </Layers>\n"
    "      </Instrument>\n"
    "      <Instrument number=\"2\">\n"
    "        <OneShot>True</OneShot>\n"
    "        <Layers><Layer number=\"1\"><VelStart>0</VelStart><VelEnd>127</VelEnd><SampleName>MISSING</SampleName></Layer></Layers>\n"
    "      </Instrument>\n"
    "      <Instrument number=\"3\">\n"
    "        <OneShot>False</OneShot>\n"
    "        <Layers><Layer number=\"1\"><VelStart>0</VelStart><VelEnd>127</VelEnd><SampleName>hat</SampleName><Volume>0.784333</Volume><Pan>0.5</Pan></Layer></Layers>\n"
    "      </Instrument>\n"
    "    </Instruments>\n"
    "  </Program>\n"
    "</MPCVObject>\n";

static int active_on(const KitPlayer *kp, int inst) {
    int n = 0;
    for (int i = 0; i < KIT_VOICES; i++) if (kp->v[i].active && kp->v[i].inst == inst) n++;
    return n;
}

static float peak(const float *L, const float *R, int n) {
    float m = 0;
    for (int i = 0; i < n; i++) {
        if (L[i] > m) m = L[i];
        if (-L[i] > m) m = -L[i];
        if (R[i] > m) m = R[i];
        if (-R[i] > m) m = -R[i];
    }
    return m;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <scratch folder>\n", argv[0]); return 2; }
    const char *dir = argv[1];
    mkdir(dir, 0775);
    vc_math_init();
    printf("drum kit\n");

    /* the WAV files: 16-bit mono, 24-bit stereo and 32-bit float mono */
    char path[512];
    unsigned char kick[4410 * 2];
    for (int i = 0; i < 4410; i++) { kick[2 * i] = 16000 & 255; kick[2 * i + 1] = 16000 >> 8; }
    snprintf(path, sizeof path, "%s/KICK.WAV", dir);
    CHECK(write_wav(path, 1, 1, 44100, 16, kick, sizeof kick) == 0, "kick WAV written");

    unsigned char snare[100 * 6];
    for (int i = 0; i < 100; i++) {
        unsigned char *s = snare + 6 * i;   /* left 0x123456, right 0x654321, 24-bit little-endian */
        s[0] = 0x56; s[1] = 0x34; s[2] = 0x12; s[3] = 0x21; s[4] = 0x43; s[5] = 0x65;
    }
    snprintf(path, sizeof path, "%s/snare.wav", dir);
    CHECK(write_wav(path, 1, 2, 44100, 24, snare, sizeof snare) == 0, "stereo 24-bit WAV written");

    unsigned char hat[100 * 4];
    for (int i = 0; i < 100; i++) { float h = 0.5f; memcpy(hat + 4 * i, &h, 4); }
    snprintf(path, sizeof path, "%s/hat.wav", dir);
    CHECK(write_wav(path, 3, 1, 44100, 32, hat, sizeof hat) == 0, "float WAV written");

    snprintf(path, sizeof path, "%s/bad.wav", dir);
    FILE *bad = fopen(path, "wb");
    if (bad) { fputs("not a wave file at all", bad); fclose(bad); }

    KitSample s;
    snprintf(path, sizeof path, "%s/KICK.WAV", dir);
    CHECK(kit_wav_load(path, &s) == 0, "the 16-bit WAV loads");
    CHECK(s.frames == 4410 && s.channels == 1 && s.rate == 44100, "kick: 4410 mono frames at 44.1 kHz");
    CHECK(s.data && s.data[0] == 16000, "kick: sample value 16000");
    kit_sample_free(&s);

    snprintf(path, sizeof path, "%s/snare.wav", dir);
    CHECK(kit_wav_load(path, &s) == 0, "the 24-bit stereo WAV loads");
    CHECK(s.channels == 2 && s.frames == 100, "snare: 100 stereo frames");
    CHECK(s.data && s.data[0] == 0x1234 && s.data[1] == 0x6543, "snare: 24-bit top 16 bits kept on both channels");
    kit_sample_free(&s);

    snprintf(path, sizeof path, "%s/hat.wav", dir);
    CHECK(kit_wav_load(path, &s) == 0, "the float WAV loads");
    CHECK(s.data && s.data[0] == 16384, "hat: 0.5 float becomes 16384 (rounded)");
    kit_sample_free(&s);

    snprintf(path, sizeof path, "%s/bad.wav", dir);
    CHECK(kit_wav_load(path, &s) == -1, "a file that isn't a WAV is refused");
    CHECK(kit_wav_load("/nonexistent/x.wav", &s) == -1, "a missing WAV is refused");

    /* the program */
    snprintf(path, sizeof path, "%s/kit.xpm", dir);
    FILE *xf = fopen(path, "wb");
    if (xf) { fputs(XPM, xf); fclose(xf); }
    Kit *k = kit_new(path);
    CHECK(k != NULL, "the program reads");
    CHECK(k && strcmp(k->name, "Test Kit") == 0, "the program name is read from Name");
    CHECK(k && k->prog->ninstruments == 3, "three instruments");
    CHECK(kit_new("/nonexistent/kit.xpm") == NULL, "a missing program gives NULL");

    KitPlayer kp;
    memset(&kp, 0, sizeof kp);
    kp.gain = 1.f;
    float L[512], R[512];
    memset(L, 0, sizeof L); memset(R, 0, sizeof R);
    kit_note_on(&kp, k, 0, 100, 44100.0);
    kit_render(&kp, k, L, R, 64);
    CHECK(peak(L, R, 64) == 0.f, "before an instrument is loaded, a note makes no sound");

    CHECK(k && kit_load_instrument(k, 0) == 0 && kit_load_instrument(k, 1) == 0 && kit_load_instrument(k, 2) == 0,
          "all three instruments load");
    CHECK(k && k->inst[0].ready && k->inst[0].nlayers == 2, "instrument 1 is ready with two layers");
    CHECK(k && k->inst[1].ready && k->inst[1].nlayers == 0, "instrument 2 is ready with no layers (its WAV is missing)");
    CHECK(k && k->inst[2].ready && k->inst[2].nlayers == 1, "instrument 3 is ready with one layer");

    kit_player_reset(&kp);
    memset(L, 0, sizeof L); memset(R, 0, sizeof R);
    kit_note_on(&kp, k, 0, 30, 44100.0);
    CHECK(k && kp.v[0].layer == &k->inst[0].layers[0], "velocity 30 picks the kick layer (0..63)");
    kit_render(&kp, k, L, R, 64);
    CHECK(peak(L, R, 64) > 0.01f, "the kick sounds");
    kit_note_on(&kp, k, 0, 100, 44100.0);
    CHECK(k && kp.v[1].layer == &k->inst[0].layers[1], "velocity 100 picks the snare layer (64..127)");

    kit_note_on(&kp, k, 1, 100, 44100.0);
    CHECK(active_on(&kp, 1) == 0, "a layerless instrument doesn't start a voice");

    kit_note_on(&kp, k, 2, 100, 44100.0);
    CHECK(active_on(&kp, 2) == 1, "the sustained hat starts");
    kit_note_off(&kp, 2);
    CHECK(active_on(&kp, 2) == 0, "note off ends the sustained hat");
    kit_note_on(&kp, k, 0, 100, 44100.0);
    kit_note_off(&kp, 0);
    CHECK(active_on(&kp, 0) > 0, "note off doesn't cut a one-shot kit piece");

    kit_player_reset(&kp);
    for (int i = 0; i < 40; i++) kit_note_on(&kp, k, 0, 10, 44100.0);
    int busy = 0;
    for (int i = 0; i < KIT_VOICES; i++) if (kp.v[i].active) busy++;
    CHECK(busy == KIT_VOICES, "with all voices busy, a new note takes the oldest, and nothing overflows");

    kit_render(&kp, NULL, L, R, 64);
    busy = 0;
    for (int i = 0; i < KIT_VOICES; i++) if (kp.v[i].active) busy++;
    CHECK(busy == 0, "switching to no kit silences every voice");

    /* a new kit must not reach voices that still point into the old one */
    kit_player_reset(&kp);
    kit_note_on(&kp, k, 0, 100, 44100.0);
    kit_render(&kp, k, L, R, 8);
    Kit *k2 = kit_new(path);
    kit_render(&kp, k2, L, R, 8);
    busy = 0;
    for (int i = 0; i < KIT_VOICES; i++) if (kp.v[i].active) busy++;
    CHECK(busy == 0, "a different kit silences the voices from the old one");
    kit_free(k2);

    kit_free(k);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
