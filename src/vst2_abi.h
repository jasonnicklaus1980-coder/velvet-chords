/* Minimal, independently written declarations of the VST 2.x binary interface
 * (the same ABI that MPC OS's JUCE-based plugin host loads). No Steinberg code. */
#ifndef VC_VST2_ABI_H
#define VC_VST2_ABI_H
#include <stdint.h>

#define VC_FOURCC(a, b, c, d) \
    ((int32_t)(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d)))
#define kEffectMagic VC_FOURCC('V', 's', 't', 'P')

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt);
typedef intptr_t (*AEffectDispatcherProc)(AEffect *, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt);
typedef void (*AEffectProcessProc)(AEffect *, float **inputs, float **outputs, int32_t frames);
typedef void (*AEffectProcessDoubleProc)(AEffect *, double **inputs, double **outputs, int32_t frames);
typedef void (*AEffectSetParameterProc)(AEffect *, int32_t index, float value);
typedef float (*AEffectGetParameterProc)(AEffect *, int32_t index);

struct AEffect {
    int32_t magic;
    AEffectDispatcherProc dispatcher;
    AEffectProcessProc process; /* deprecated accumulating process */
    AEffectSetParameterProc setParameter;
    AEffectGetParameterProc getParameter;
    int32_t numPrograms;
    int32_t numParams;
    int32_t numInputs;
    int32_t numOutputs;
    int32_t flags;
    intptr_t resvd1;
    intptr_t resvd2;
    int32_t initialDelay;
    int32_t realQualities;
    int32_t offQualities;
    float ioRatio;
    void *object;
    void *user;
    int32_t uniqueID;
    int32_t version;
    AEffectProcessProc processReplacing;
    AEffectProcessDoubleProc processDoubleReplacing;
    char future[56];
};

enum {
    effFlagsHasEditor = 1 << 0,
    effFlagsCanReplacing = 1 << 4,
    effFlagsProgramChunks = 1 << 5,
    effFlagsIsSynth = 1 << 8,
    effFlagsNoSoundInStop = 1 << 9
};

enum {
    effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effSetProgramName = 4,
    effGetProgramName = 5, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12,
    effEditGetRect = 13, effEditOpen = 14, effEditClose = 15, effEditIdle = 19,
    effGetChunk = 23, effSetChunk = 24, effProcessEvents = 25, effCanBeAutomated = 26,
    effString2Parameter = 27, effGetProgramNameIndexed = 29,
    effGetInputProperties = 33, effGetOutputProperties = 34, effGetPlugCategory = 35,
    effSetBypass = 44, effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
    effGetVendorVersion = 49, effVendorSpecific = 50, effCanDo = 51, effGetTailSize = 52,
    effGetParameterProperties = 56, effGetVstVersion = 58,
    effStartProcess = 71, effStopProcess = 72
};

enum {
    audioMasterAutomate = 0, audioMasterVersion = 1, audioMasterCurrentId = 2, audioMasterIdle = 3,
    audioMasterGetTime = 7, audioMasterProcessEvents = 8, audioMasterUpdateDisplay = 42
};

enum { kPlugCategSynth = 2 };
enum { kVstMidiType = 1 };
enum { kVstTransportPlaying = 1 << 1, kVstTempoValid = 1 << 10 };

typedef struct VstEvent {
    int32_t type, byteSize, deltaFrames, flags;
    char data[16];
} VstEvent;

typedef struct VstMidiEvent {
    int32_t type;      /* kVstMidiType */
    int32_t byteSize;  /* sizeof(VstMidiEvent) */
    int32_t deltaFrames;
    int32_t flags;
    int32_t noteLength;
    int32_t noteOffset;
    char midiData[4];
    char detune;
    char noteOffVelocity;
    char reserved1;
    char reserved2;
} VstMidiEvent;

typedef struct VstEvents {
    int32_t numEvents;
    intptr_t reserved;
    VstEvent *events[2]; /* variable length */
} VstEvents;

typedef struct VstTimeInfo {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;

#endif
