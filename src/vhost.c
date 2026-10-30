#include "vhost.h"
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Velvet Chords' own unique ID. Hosting it inside itself would recurse, so it is refused. */
#define VH_OWN_UID VC_FOURCC('V', 'l', 'v', 'C')
#define VH_MAX_BLOCK 2048

/* Host callback opcodes that the VST 2.4 spec defines and vst2_abi.h doesn't list. */
enum {
    vhAudioMasterCurrentProcessLevel = 23, vhAudioMasterGetBlockSize = 15, vhAudioMasterGetSampleRate = 16,
    vhAudioMasterGetVendorString = 32, vhAudioMasterGetProductString = 33, vhAudioMasterGetVendorVersion = 34,
    vhAudioMasterGetLanguage = 38
};

typedef AEffect *(*EntryFn)(audioMasterCallback);

/* MIDI events in the layout a plugin reads: VstEvents with room for VH_MAX_NOTES entries. */
typedef struct {
    int32_t numEvents;
    intptr_t reserved;
    VstEvent *events[VH_MAX_NOTES];
} VhEventList;

struct VHost {
    void *lib;
    AEffect *fx;
    VhInfo info;
    double sr;
    int block;
    VstMidiEvent midi[VH_MAX_NOTES];
    VhEventList list;
    float zeros[VH_MAX_BLOCK];
    float bufL[VH_MAX_BLOCK], bufR[VH_MAX_BLOCK];
};

static intptr_t host_callback(AEffect *e, int32_t op, int32_t idx, intptr_t val, void *ptr, float opt) {
    (void)idx; (void)val; (void)opt;
    VHost *h = e ? (VHost *)e->user : NULL;
    switch (op) {
        case audioMasterVersion:                return 2400;
        case audioMasterCurrentId:              return 0;
        case audioMasterIdle:                   return 0;
        case audioMasterGetTime:                return 0;      /* no transport yet: plugins see a stopped host */
        case audioMasterProcessEvents:          return 0;      /* MIDI out from a hosted plugin is not routed yet */
        case audioMasterUpdateDisplay:          return 0;
        case audioMasterAutomate:               return 0;
        case vhAudioMasterGetSampleRate:        return h ? (intptr_t)h->sr : 0;
        case vhAudioMasterGetBlockSize:         return h ? h->block : 0;
        case vhAudioMasterCurrentProcessLevel:  return 0;
        case vhAudioMasterGetLanguage:          return 1;      /* English */
        case vhAudioMasterGetVendorVersion:     return 1;
        case vhAudioMasterGetVendorString:      if (ptr) strcpy((char *)ptr, "Velvet Chords"); return 1;
        case vhAudioMasterGetProductString:     if (ptr) strcpy((char *)ptr, "Velvet Chords host"); return 1;
        default:                                return 0;
    }
}

static void copy_text(char *dst, size_t len, const char *src) {
    if (!dst || len == 0) return;
    snprintf(dst, len, "%s", src ? src : "");
}

/* Loads the library and runs its entry point. Returns NULL (and cleans up) if it isn't a usable VST2 plugin. */
static AEffect *load_entry(const char *path, void **lib_out) {
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) return NULL;
    EntryFn entry = (EntryFn)dlsym(lib, "VSTPluginMain");
    if (!entry) entry = (EntryFn)dlsym(lib, "main");     /* some older plugins only export main */
    AEffect *fx = entry ? entry(host_callback) : NULL;
    if (!fx || fx->magic != kEffectMagic || fx->uniqueID == VH_OWN_UID) {
        dlclose(lib);
        return NULL;
    }
    *lib_out = lib;
    return fx;
}

static void read_info(AEffect *fx, VhInfo *info) {
    char buf[VH_NAME_LEN] = "";
    fx->dispatcher(fx, effGetEffectName, 0, 0, buf, 0.f);
    if (!buf[0]) fx->dispatcher(fx, effGetProductString, 0, 0, buf, 0.f);
    copy_text(info->name, sizeof info->name, buf);
    info->uid = fx->uniqueID;
    info->is_instrument = (fx->flags & effFlagsIsSynth) != 0 ||
                          fx->dispatcher(fx, effGetPlugCategory, 0, 0, NULL, 0.f) == kPlugCategSynth;
    info->inputs = fx->numInputs;
    info->outputs = fx->numOutputs;
    info->nparams = fx->numParams;
}

VHost *vh_open(const char *path, double sample_rate, int block_size) {
    if (!path || sample_rate <= 0 || block_size <= 0) return NULL;
    if (block_size > VH_MAX_BLOCK) block_size = VH_MAX_BLOCK;

    VHost *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->sr = sample_rate;
    h->block = block_size;
    h->fx = load_entry(path, &h->lib);
    if (!h->fx) { free(h); return NULL; }

    AEffect *fx = h->fx;
    fx->user = h;
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0.f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, (float)sample_rate);
    fx->dispatcher(fx, effSetBlockSize, 0, block_size, NULL, 0.f);
    fx->dispatcher(fx, effMainsChanged, 0, 1, NULL, 0.f);
    fx->dispatcher(fx, effStartProcess, 0, 0, NULL, 0.f);
    read_info(fx, &h->info);
    return h;
}

void vh_close(VHost *h) {
    if (!h) return;
    AEffect *fx = h->fx;
    if (fx) {
        fx->dispatcher(fx, effStopProcess, 0, 0, NULL, 0.f);
        fx->dispatcher(fx, effMainsChanged, 0, 0, NULL, 0.f);
        fx->dispatcher(fx, effClose, 0, 0, NULL, 0.f);     /* the plugin frees its own memory here */
    }
    if (h->lib) dlclose(h->lib);
    free(h);
}

int vh_probe(const char *path, VhInfo *info) {
    if (!path || !info) return -1;
    VHost *h = vh_open(path, 44100.0, 512);
    if (!h) return -1;
    *info = h->info;
    vh_close(h);
    return 0;
}

const VhInfo *vh_info(const VHost *h) { return h ? &h->info : NULL; }

static int collect_midi(VHost *h, const VhMidi *notes, int nnotes, int from, int to) {
    int count = 0;
    for (int i = 0; i < nnotes && count < VH_MAX_NOTES; ++i) {
        if (notes[i].frame < from || notes[i].frame >= to) continue;
        VstMidiEvent *m = &h->midi[count];
        memset(m, 0, sizeof *m);
        m->type = kVstMidiType;
        m->byteSize = sizeof *m;
        m->deltaFrames = notes[i].frame - from;
        m->midiData[0] = (char)notes[i].status;
        m->midiData[1] = (char)notes[i].data1;
        m->midiData[2] = (char)notes[i].data2;
        h->list.events[count] = (VstEvent *)m;
        ++count;
    }
    h->list.numEvents = count;
    return count;
}

int vh_process(VHost *h, const VhMidi *notes, int nnotes, float *outL, float *outR, int n) {
    if (!h) return -1;
    AEffect *fx = h->fx;
    for (int start = 0; start < n; start += h->block) {
        int len = n - start < h->block ? n - start : h->block;
        int count = notes ? collect_midi(h, notes, nnotes, start, start + len) : 0;
        if (count > 0) fx->dispatcher(fx, effProcessEvents, 0, 0, &h->list, 0.f);

        float *ins[2] = { h->zeros, h->zeros };
        float *outs[2] = { h->bufL, h->bufR };
        memset(h->bufL, 0, sizeof(float) * (size_t)len);
        memset(h->bufR, 0, sizeof(float) * (size_t)len);
        if (fx->numOutputs > 0) {
            if (fx->processReplacing) fx->processReplacing(fx, ins, outs, len);
            else if (fx->process) fx->process(fx, ins, outs, len);
        }
        for (int i = 0; outL && outR && i < len; ++i) {
            outL[start + i] = h->bufL[i];
            outR[start + i] = h->bufR[i];
        }
    }
    return 0;
}

int vh_param_count(const VHost *h) { return h && h->fx ? h->fx->numParams : 0; }

void vh_param_name(const VHost *h, int index, char *buf, size_t len) {
    if (!buf || len == 0) return;
    buf[0] = 0;
    if (!h || index < 0 || index >= h->info.nparams) return;
    char name[VH_NAME_LEN] = "";
    h->fx->dispatcher(h->fx, effGetParamName, index, 0, name, 0.f);
    copy_text(buf, len, name);
}

float vh_get_param(const VHost *h, int index) {
    if (!h || index < 0 || index >= h->info.nparams) return 0.f;
    return h->fx->getParameter(h->fx, index);
}

void vh_set_param(VHost *h, int index, float value) {
    if (!h || index < 0 || index >= h->info.nparams) return;
    h->fx->setParameter(h->fx, index, value);
}

int vh_list(const char *dir, char paths[][VH_PATH_LEN], int max) {
    if (!dir || !paths || max <= 0) return -1;
    DIR *d = opendir(dir);
    if (!d) return -1;
    int count = 0;
    struct dirent *de;
    while (count < max && (de = readdir(d)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len < 4 || strcmp(de->d_name + len - 3, ".so") != 0) continue;
        snprintf(paths[count], VH_PATH_LEN, "%s/%s", dir, de->d_name);
        ++count;
    }
    closedir(d);
    return count;
}
