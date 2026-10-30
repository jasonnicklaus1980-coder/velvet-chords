/* Tests the reader for the MPC's plugin list. Usage: test_mpc_list <scratch file> */
#include "mpc_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

#define CHECK(cond, what) do { checks++; if (!(cond)) { failures++; printf("  FAIL: %s\n", what); } } while (0)

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(text, f);
    fclose(f);
    return 0;
}

static const char *SETTINGS =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<PROPERTIES>\n"
    "  <VALUE name=\"Other\"><KNOWNPLUGINS><PLUGIN name=\"Not me\" file=\"/sdcard/vst/notme.so\" uid=\"11111111\" isInstrument=\"1\"/></KNOWNPLUGINS></VALUE>\n"
    "  <VALUE name=\"pluginList-arm\">\n"
    "    <KNOWNPLUGINS>\n"
    "      <PLUGIN name=\"Fake Sine\" descriptiveName=\"Fake Sine\" format=\"VST\" category=\"Synth\" manufacturer=\"Test\" version=\"1.0\"\n"
    "              file=\"/sdcard/vst/fake_sine.so\" uid=\"46616b65\" isInstrument=\"1\" numInputs=\"0\" numOutputs=\"2\"/>\n"
    "      <PLUGIN name=\"Delay\" format=\"VST\" category=\"Effect\" file=\"/sdcard/vst/delay.so\" uid=\"44656c79\" isInstrument=\"0\"/>\n"
    "      <PLUGIN name=\"Velvet Chords\" format=\"VST\" file=\"/sdcard/vst/velvetchords.so\" uid=\"566c7643\" isInstrument=\"1\"/>\n"
    "      <PLUGIN name=\"Warm Pad\" format=\"VST\" file=\"/sdcard/vst/pad/warm.so\" uid=\"57617270\" isInstrument=\"1\"/>\n"
    "    </KNOWNPLUGINS>\n"
    "  </VALUE>\n"
    "</PROPERTIES>\n";

static const char *NO_LIST =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<PROPERTIES>\n"
    "  <VALUE name=\"Other\"><KNOWNPLUGINS/></VALUE>\n"
    "</PROPERTIES>\n";

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <scratch file>\n", argv[0]); return 2; }
    const char *path = argv[1];

    printf("MPC plugin list\n");

    CHECK(write_text(path, SETTINGS) == 0, "scratch file written");
    MpcPlugin list[8];
    int n = mpc_plugin_list(path, list, 8);
    CHECK(n == 2, "two instruments listed (effect and Velvet Chords itself left out)");
    if (n >= 1) {
        CHECK(!strcmp(list[0].name, "Fake Sine"), "first is Fake Sine, in list order");
        CHECK(!strcmp(list[0].path, "/sdcard/vst/fake_sine.so"), "first path is its file=");
    }
    if (n >= 2) {
        CHECK(!strcmp(list[1].name, "Warm Pad"), "second is Warm Pad, from a subfolder");
        CHECK(!strcmp(list[1].path, "/sdcard/vst/pad/warm.so"), "second path is its file=");
    }

    MpcPlugin few[1];
    CHECK(mpc_plugin_list(path, few, 1) == 1, "max limits the count");

    CHECK(write_text(path, NO_LIST) == 0, "no-list file written");
    CHECK(mpc_plugin_list(path, list, 8) == 0, "a settings file with no plugin list gives 0");

    CHECK(mpc_plugin_list("/nonexistent/MPC.settings", list, 8) == -1, "a missing file gives -1");

    CHECK(write_text(path, "<PROPERTIES><VALUE name=\"pluginList-arm\"><KNOWNPLUGINS>") == 0, "broken file written");
    CHECK(mpc_plugin_list(path, list, 8) == -1, "a file that isn't well-formed gives -1");

    setenv("VC_SETTINGS_FILE", path, 1);
    char found[300];
    CHECK(mpc_settings_find(found, sizeof found) == 0 && !strcmp(found, path), "VC_SETTINGS_FILE overrides the search");

    remove(path);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
