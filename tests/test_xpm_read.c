/* Keygroup reader tests on a program written from the field notes in docs/XPM_FORMAT.md. These show the reader
 * follows the notes; they do not show that the notes match an MPC-made file. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "xpm_read.h"

static int failures = 0;
static void check(int ok, const char *what) { printf("  %s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static int near(double a, double b, double tol) { return fabs(a - b) < tol; }

static const char *prog =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<MPCVObject>\n"
" <Program type=\"Keygroup\">\n"
"  <ProgramName>Test Keys</ProgramName>\n"
"  <KeygroupNumKeygroups>24</KeygroupNumKeygroups>\n"
"  <KeygroupPitchBendRange>0.5</KeygroupPitchBendRange>\n"
"  <Instruments>\n"
"   <Instrument number=\"1\">\n"
"    <LowNote>36</LowNote><HighNote>47</HighNote>\n"
"    <ZonePlay>1</ZonePlay><TriggerMode>2</TriggerMode>\n"
"    <Layers>\n"
"     <Layer>\n"
"      <VelStart>0</VelStart><VelEnd>63</VelEnd>\n"
"      <SampleName>Piano soft C2</SampleName>\n"
"      <Volume>0.353</Volume><Pan>0.5</Pan>\n"
"      <TuneCoarse>-2</TuneCoarse><TuneFine>0.25</TuneFine>\n"
"      <RootNote>49</RootNote><KeyTrack>True</KeyTrack>\n"
"      <SliceStart>100</SliceStart><SliceEnd>5000</SliceEnd>\n"
"     </Layer>\n"
"     <Layer>\n"
"      <VelStart>64</VelStart><VelEnd>127</VelEnd>\n"
"      <SampleName>Piano hard C2</SampleName>\n"
"      <Volume>1.0</Volume><Pan>1.0</Pan>\n"
"      <Active>False</Active>\n"
"     </Layer>\n"
"    </Layers>\n"
"   </Instrument>\n"
"   <Instrument number=\"2\">\n"
"    <LowNote>48</LowNote><HighNote>59</HighNote>\n"
"    <OneShot>True</OneShot>\n"
"    <Layers><Layer><VelStart>0</VelStart><VelEnd>127</VelEnd><SampleName>Bell</SampleName></Layer></Layers>\n"
"   </Instrument>\n"
"  </Instruments>\n"
" </Program>\n"
"</MPCVObject>\n";

static XpmProgram p;

int main(void) {
    printf("keygroup reader\n");
    int rc = xpm_read(prog, strlen(prog), &p);
    check(rc == 0, "a keygroup program reads");
    check(!p.is_drum && strcmp(p.name, "Test Keys") == 0, "type Keygroup and the program name");
    check(p.num_keygroups == 24 && near(p.pitchbend_octaves, 0.5, 1e-9), "keygroup count and pitch bend range");
    check(p.ninstruments == 2, "two instruments");

    const XpmInstrument *a = &p.instruments[0];
    check(a->number == 1 && a->low_note == 36 && a->high_note == 47, "instrument 1 covers notes 36..47");
    check(a->zone_play == 1 && a->trigger == 2 && !a->one_shot, "velocity zone play, attack trigger");
    check(a->nlayers == 2, "instrument 1 has two velocity layers");
    const XpmLayer *l0 = &a->layers[0];
    check(l0->vel_start == 0 && l0->vel_end == 63, "layer 1 velocity 0..63");
    check(strcmp(l0->sample, "Piano soft C2") == 0, "the sample name keeps its spaces");
    check(near(l0->gain_db, -12.0, 1e-6), "Volume 0.353 reads as -12 dB");
    check(near(l0->pan, 0.0, 1e-9), "Pan 0.5 reads as centre");
    check(near(l0->tune_cents, -1.75, 1e-9), "tune is coarse + fine (-2 + 0.25)");
    check(l0->root_note == 48, "RootNote 49 in the file is root note 48 (one lower)");
    check(l0->key_track == 1 && l0->slice_start == 100 && l0->slice_end == 5000, "key track and slice start and end");
    const XpmLayer *l1 = &a->layers[1];
    check(near(l1->gain_db, 6.0, 1e-6), "Volume 1.0 reads as +6 dB");
    check(near(l1->pan, 1.0, 1e-9), "Pan 1.0 reads as hard right");
    check(l1->active == 0, "Active False switches the layer off");
    check(l1->root_note == -1 && l1->slice_start == -1, "absent RootNote and slice points are marked as absent");

    const XpmInstrument *b = &p.instruments[1];
    check(b->number == 2 && b->one_shot, "OneShot True when no TriggerMode is given");
    check(b->nlayers == 1 && strcmp(b->layers[0].sample, "Bell") == 0, "a single layer with the sample name");
    check(near(b->layers[0].gain_db, 0.0, 0.001), "a missing Volume reads as 0 dB");

    printf("sample paths\n");
    char path[512];
    check(xpm_sample_path("/media/az01-internal/Programs", "Piano soft C2", path, sizeof path) == 0 &&
          strcmp(path, "/media/az01-internal/Programs/Piano soft C2.WAV") == 0, "folder, name and .WAV");
    check(xpm_sample_path("/x", "y", path, 3) != 0, "a path that doesn't fit is refused");

    printf("bad input\n");
    XpmProgram q;
    check(xpm_read("<Other/>", strlen("<Other/>"), &q) != 0, "a file whose root isn't MPCVObject is refused");
    check(xpm_read("<MPCVObject><Program type=\"Synth\"/></MPCVObject>", strlen("<MPCVObject><Program type=\"Synth\"/></MPCVObject>"), &q) != 0, "an unknown program type is refused");
    check(xpm_read("<MPCVObject><Program type=\"Drum\"/></MPCVObject>", strlen("<MPCVObject><Program type=\"Drum\"/></MPCVObject>"), &q) == 0 && q.is_drum, "a drum program reads");
    check(xpm_read("not xml", strlen("not xml"), &q) != 0, "non-XML text is refused");

    printf("\n%s\n", failures ? "KEYGROUP READER TESTS FAILED" : "keygroup reader tests passed");
    return failures ? 1 : 0;
}
