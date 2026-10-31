/* Tests the reader for the MPC's plugin list. Usage: test_mpc_list <scratch file> */
#include "mpc_list.h"
#include <sys/stat.h>
#include <unistd.h>
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

    /* the expansions folder: .so files at the top and in a subfolder, a non-plugin file, and a folder too deep to search */
    const char *root = "build/native/mpc_exp_test";
    char deep[300];
    mkdir(root, 0775);
    mkdir("build/native/mpc_exp_test/pads", 0775);
    mkdir("build/native/mpc_exp_test/a", 0775);
    mkdir("build/native/mpc_exp_test/a/b", 0775);
    mkdir("build/native/mpc_exp_test/a/b/c", 0775);
    mkdir("build/native/mpc_exp_test/a/b/c/d", 0775);
    mkdir("build/native/mpc_exp_test/a/b/c/d/e", 0775);   /* one level below the search: its plugin must be left out */
    write_text("build/native/mpc_exp_test/top.so", "");
    write_text("build/native/mpc_exp_test/readme.txt", "");
    write_text("build/native/mpc_exp_test/pads/warm.so", "");
    snprintf(deep, sizeof deep, "%s/a/b/c/d/e/deep.so", root);
    write_text(deep, "");
    char walked[8][VH_PATH_LEN];
    int w = mpc_walk_plugins(root, walked, 8, MPC_WALK_DEPTH);
    if (w != 2) printf("  (the walk returned %d)\n", w);
    CHECK(w == 2, "the walk finds the .so files at the top and in a subfolder, not the text file or the too-deep one");
    /* readdir order differs between systems, so look for each file rather than assuming a position */
    int top = 0, sub = 0;
    for (int i = 0; i < w; i++) {
        if (strstr(walked[i], "/top.so") != NULL) top = 1;
        if (strstr(walked[i], "pads/warm.so") != NULL) sub = 1;
    }
    CHECK(top, "the top-level plugin is found");
    CHECK(sub, "the subfolder plugin is found");
    CHECK(mpc_walk_plugins("/nonexistent/expansions", walked, 8, MPC_WALK_DEPTH) == -1, "a missing expansions folder gives -1");
    remove("build/native/mpc_exp_test/top.so");
    remove("build/native/mpc_exp_test/readme.txt");
    remove("build/native/mpc_exp_test/pads/warm.so");
    remove(deep);
    rmdir("build/native/mpc_exp_test/a/b/c/d/e");
    rmdir("build/native/mpc_exp_test/a/b/c/d");
    rmdir("build/native/mpc_exp_test/a/b/c");
    rmdir("build/native/mpc_exp_test/a/b");
    rmdir("build/native/mpc_exp_test/a");
    rmdir("build/native/mpc_exp_test/pads");
    rmdir(root);

    /* an expansion: its title comes from expansion.xml in its top folder, found from a kit deep inside it */
    const char *troot = "build/native/mpc_title_test";
    mkdir(troot, 0775);
    mkdir("build/native/mpc_title_test/Expansion A", 0775);
    mkdir("build/native/mpc_title_test/Expansion A/Programs", 0775);
    mkdir("build/native/mpc_title_test/Loose", 0775);
    write_text("build/native/mpc_title_test/Expansion A/expansion.xml",
               "<?xml version=\"1.0\"?><expansion><title>Dirty Drummer Demo</title><type>drum</type></expansion>");
    write_text("build/native/mpc_title_test/Expansion A/Programs/kit.xpm", "");
    write_text("build/native/mpc_title_test/Loose/kit.xpm", "");
    char title[96];
    CHECK(mpc_expansion_title("build/native/mpc_title_test/Expansion A/Programs/kit.xpm", troot, title, sizeof title) == 0
          && !strcmp(title, "Dirty Drummer Demo"), "a kit inside an expansion takes the expansion's title");
    CHECK(mpc_expansion_title("build/native/mpc_title_test/Loose/kit.xpm", troot, title, sizeof title) == -1,
          "a kit outside any expansion has no title");
    remove("build/native/mpc_title_test/Expansion A/Programs/kit.xpm");
    remove("build/native/mpc_title_test/Expansion A/expansion.xml");
    remove("build/native/mpc_title_test/Loose/kit.xpm");
    rmdir("build/native/mpc_title_test/Expansion A/Programs");
    rmdir("build/native/mpc_title_test/Expansion A");
    rmdir("build/native/mpc_title_test/Loose");
    rmdir(troot);

    setenv("VC_SETTINGS_FILE", path, 1);
    char found[300];
    CHECK(mpc_settings_find(found, sizeof found) == 0 && !strcmp(found, path), "VC_SETTINGS_FILE overrides the search");

    remove(path);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
