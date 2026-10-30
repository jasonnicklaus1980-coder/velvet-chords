/* A tiny VST2 instrument for testing the host: a sine that follows MIDI notes, with one "Gain" parameter.
 * Its phase runs continuously across blocks, so the test can check that blocks join without a click. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "vst2_abi.h"

typedef struct { int32_t delta; int on; float freq; } Pending;

typedef struct {
    double sr, phase;
    int note_on;
    float freq, gain, param;
    Pending pend[32];
    int npend;
} Fake;

static intptr_t dispatch(AEffect *e, int32_t op, int32_t idx, intptr_t val, void *ptr, float opt) {
    Fake *f = (Fake *)e->object;
    (void)idx; (void)val; (void)opt;
    switch (op) {
        case effOpen: return 0;
        case effClose: free(f); free(e); return 0;
        case effSetSampleRate: f->sr = opt; return 0;
        case effGetEffectName: strcpy((char *)ptr, "Fake Sine"); return 1;
        case effGetProductString: strcpy((char *)ptr, "Fake Sine"); return 1;
        case effGetVendorString: strcpy((char *)ptr, "Velvet Chords tests"); return 1;
        case effGetPlugCategory: return kPlugCategSynth;
        case effGetParamName: if (idx == 0) strcpy((char *)ptr, "Gain"); return 0;
        case effProcessEvents: {
            /* events apply at their deltaFrames inside the block, as a real plugin must */
            VstEvents *list = (VstEvents *)ptr;
            f->npend = 0;
            for (int32_t i = 0; i < list->numEvents && f->npend < 32; ++i) {
                VstMidiEvent *m = (VstMidiEvent *)list->events[i];
                if (m->type != kVstMidiType) continue;
                unsigned status = (unsigned char)m->midiData[0] & 0xF0u;
                int vel = (unsigned char)m->midiData[2];
                Pending *p = &f->pend[f->npend];
                p->delta = m->deltaFrames;
                if (status == 0x90 && vel > 0) {
                    p->on = 1;
                    p->freq = (float)(440.0 * pow(2.0, ((unsigned char)m->midiData[1] - 69) / 12.0));
                    ++f->npend;
                } else if (status == 0x80 || (status == 0x90 && vel == 0)) {
                    p->on = 0;
                    ++f->npend;
                }
            }
            return 1;
        }
        case effSetProgram: case effSetBlockSize: case effMainsChanged: case effStartProcess: case effStopProcess:
            return 0;
        default: return 0;
    }
}

static void process_replacing(AEffect *e, float **in, float **out, int32_t n) {
    Fake *f = (Fake *)e->object;
    (void)in;
    int k = 0;
    for (int32_t i = 0; i < n; ++i) {
        while (k < f->npend && f->pend[k].delta == i) {
            if (f->pend[k].on) { f->freq = f->pend[k].freq; f->note_on = 1; }
            else f->note_on = 0;
            ++k;
        }
        float s = 0.f;
        if (f->note_on) {
            s = (float)(0.2 * f->gain * sin(f->phase));
            f->phase += 2.0 * M_PI * f->freq / f->sr;
            if (f->phase > 2.0 * M_PI) f->phase -= 2.0 * M_PI;
        }
        out[0][i] = s;
        out[1][i] = s;
    }
}

static float get_param(AEffect *e, int32_t idx) { Fake *f = (Fake *)e->object; return idx == 0 ? f->param : 0.f; }

static void set_param(AEffect *e, int32_t idx, float v) {
    Fake *f = (Fake *)e->object;
    if (idx != 0) return;
    f->param = v;
    f->gain = v;
}

AEffect *VSTPluginMain(audioMasterCallback master) {
    (void)master;
    AEffect *e = calloc(1, sizeof *e);
    Fake *f = calloc(1, sizeof *f);
    if (!e || !f) { free(e); free(f); return NULL; }
    f->gain = 1.f;
    f->param = 1.f;
    e->object = f;
    e->magic = kEffectMagic;
    e->dispatcher = dispatch;
    e->setParameter = set_param;
    e->getParameter = get_param;
    e->processReplacing = process_replacing;
    e->numParams = 1;
    e->numInputs = 0;
    e->numOutputs = 2;
    e->flags = effFlagsCanReplacing | effFlagsIsSynth;
    e->uniqueID = VC_FOURCC('F', 'k', 'S', 'n');
    e->version = 1;
    return e;
}
