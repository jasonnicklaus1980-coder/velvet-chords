#include "xpm_read.h"
#include "xml_min.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double num(const XNode *n, const char *name, double def) {
    const char *t = xml_child_text(n, name);
    return t && *t ? strtod(t, NULL) : def;
}

static int has_true(const XNode *n, const char *name, int def) {
    const char *t = xml_child_text(n, name);
    if (!t) return def;
    return strcmp(t, "True") == 0;
}

double xpm_gain_db(double volume) {
    const double minus12 = 0.353, plus6 = 1.0;
    return (volume - minus12) * 18.0 / (plus6 - minus12) - 12.0;
}

double xpm_pan(double pan) {
    double p = pan * 2.0 - 1.0;
    return p < -1.0 ? -1.0 : (p > 1.0 ? 1.0 : p);
}

int xpm_sample_path(const char *folder, const char *sample, char *out, size_t len) {
    int n = snprintf(out, len, "%s/%s.WAV", folder, sample);
    return n > 0 && (size_t)n < len ? 0 : -1;
}

static void read_layer(const XNode *ln, XpmLayer *l) {
    memset(l, 0, sizeof *l);
    const char *s = xml_child_text(ln, "SampleName");
    if (s) snprintf(l->sample, sizeof l->sample, "%s", s);
    l->vel_start = (int)num(ln, "VelStart", 0);
    l->vel_end = (int)num(ln, "VelEnd", 0);
    l->active = has_true(ln, "Active", 1);
    l->gain_db = xpm_gain_db(num(ln, "Volume", 0.784333));       /* a missing Volume reads as 0 dB */
    l->pan = xpm_pan(num(ln, "Pan", 0.5));
    if (xml_child_text(ln, "Pitch") && *xml_child_text(ln, "Pitch"))
        l->tune_cents = num(ln, "Pitch", 0);
    else
        l->tune_cents = num(ln, "TuneCoarse", 0) + num(ln, "TuneFine", 0);
    l->root_note = xml_child_text(ln, "RootNote") ? (int)num(ln, "RootNote", 0) - 1 : -1;   /* the file's root is one higher */
    l->key_track = has_true(ln, "KeyTrack", 0);
    l->slice_start = xml_child_text(ln, "SliceStart") ? (long)num(ln, "SliceStart", 0) : -1;
    l->slice_end = xml_child_text(ln, "SliceEnd") ? (long)num(ln, "SliceEnd", 0) : -1;
}

static void read_instrument(const XNode *in, XpmInstrument *ins) {
    memset(ins, 0, sizeof *ins);
    const char *nr = xml_attr(in, "number");
    ins->number = nr ? atoi(nr) : 0;
    ins->low_note = (int)num(in, "LowNote", 0);
    ins->high_note = (int)num(in, "HighNote", 0);
    ins->zone_play = (int)num(in, "ZonePlay", 0);
    ins->trigger = xml_child_text(in, "TriggerMode") ? (int)num(in, "TriggerMode", 2) : 2;
    if (!xml_child_text(in, "TriggerMode")) ins->one_shot = has_true(in, "OneShot", 1);
    else ins->one_shot = ins->trigger == 0;
    const XNode *layers = xml_child(in, "Layers");
    for (const XNode *ln = layers ? layers->child : NULL; ln && ins->nlayers < XPM_MAX_LAYERS; ln = ln->next) {
        if (strcmp(ln->name, "Layer") != 0) continue;
        read_layer(ln, &ins->layers[ins->nlayers++]);
    }
}

int xpm_read(const char *xml, size_t len, XpmProgram *out) {
    if (!xml || !out) return -1;
    memset(out, 0, sizeof *out);
    XNode *root = xml_parse(xml, len);
    if (!root || strcmp(root->name, "MPCVObject") != 0) { xml_free(root); return -1; }
    const XNode *prog = xml_child(root, "Program");
    if (!prog) { xml_free(root); return -1; }
    const char *type = xml_attr(prog, "type");
    out->is_drum = type && strcmp(type, "Drum") == 0;
    if (type && strcmp(type, "Drum") != 0 && strcmp(type, "Keygroup") != 0) { xml_free(root); return -1; }
    const char *name = xml_child_text(prog, "ProgramName");
    if (name) snprintf(out->name, sizeof out->name, "%s", name);
    out->num_keygroups = (int)num(prog, "KeygroupNumKeygroups", 128);
    out->pitchbend_octaves = num(prog, "KeygroupPitchBendRange", 0);
    const XNode *insts = xml_child(prog, "Instruments");
    for (const XNode *in = insts ? insts->child : NULL; in && out->ninstruments < XPM_MAX_INSTRUMENTS; in = in->next) {
        if (strcmp(in->name, "Instrument") != 0) continue;
        read_instrument(in, &out->instruments[out->ninstruments++]);
    }
    xml_free(root);
    return 0;
}
