#include "synth.h"
#include "vcmath.h"
#include <string.h>

const char *SY_SOUND_NAMES[SY_SOUND_COUNT] = {"Electric piano", "Rhodes", "Wurlitzer", "Bright Rhodes", "Clavinet", "Piano",
    "Soft pad", "Poly synth", "Strings", "Supersaw", "Choir", "Brass", "Harp", "Organ", "Marimba", "Vibraphone",
    "Nylon guitar", "Pluck", "Music box", "Silent (MIDI only)", "FM bell", "Synth bass", "Glass pad", "Saw lead"};
const char *SY_SOUND_SHORT[SY_SOUND_COUNT] = {"E. piano", "Rhodes", "Wurli", "Bright Rh", "Clav", "Piano", "Pad", "Poly",
    "Strings", "Supersaw", "Choir", "Brass", "Harp", "Organ", "Marimba", "Vibes", "Nylon gtr", "Pluck", "Music box", "Silent",
    "FM bell", "Synth bass", "Glass", "Saw lead"};

int sy_sound_decays(int s) {
    return s == SY_EPIANO || s == SY_RHODES || s == SY_WURLI || s == SY_DYNO || s == SY_CLAV || s == SY_PIANO || s == SY_HARP ||
           s == SY_MARIMBA || s == SY_VIBES || s == SY_NYLON || s == SY_PLUCK || s == SY_MUSICBOX || s == SY_FMBELL;
}

/* plucked string (Karplus-Strong): noise burst through a damped delay line */
static void ks_start(Synth *s, SVoice *v, float hz, float bright, float damp) {
    int len = (int)(s->sr / hz); /* the averaging filter adds half a sample of delay */
    if (len < 4) len = 4;
    if (len > SY_KS_LEN) len = SY_KS_LEN;
    v->kslen = len; v->kspos = 0;
    float lp = 0;
    for (int i = 0; i < len; i++) {
        s->rng = s->rng * 1664525u + 1013904223u;
        float r = (float)(s->rng >> 8) / 8388608.f - 1.f;
        lp += (r - lp) * bright; v->ks[i] = lp;
    }
    v->ksdamp = damp;
}

void sy_init(Synth *s, float sr) {
    vc_math_init();
    memset(s, 0, sizeof(*s));
    s->sr = sr > 1000.f ? sr : 44100.f;
    s->tone = 0.5f;
    s->release = 0.1f;
    s->volume = 0.7f;
    s->rng = 0x2545F491u;
    for (int i = 0; i < SY_VOICES; i++) s->v[i].ks = s->ks[i];
}

/* A free voice, or else a released one (the oldest). When every voice is still held, there is no voice: the new note is
 * dropped, rather than cutting a note that is sounding (a drum hit must not cut the melody). NULL means none. */
static SVoice *alloc_voice(Synth *s, int group) {
    if (group == SY_GROUP_DRUM) {   /* the drums have their share: a drum note beyond it is dropped */
        int held = 0;
        for (int i = 0; i < SY_VOICES; i++) if (s->v[i].active && !s->v[i].released && s->v[i].group == SY_GROUP_DRUM) held++;
        if (held >= SY_DRUM_MAX) return NULL;
    }
    SVoice *best = 0;
    for (int i = 0; i < SY_VOICES; i++) if (!s->v[i].active) return &s->v[i];
    for (int i = 0; i < SY_VOICES; i++) {
        SVoice *v = &s->v[i];
        if (v->released && (!best || v->age < best->age)) best = v;
    }
    return best;
}

static float rnd(Synth *s) { s->rng = s->rng * 1664525u + 1013904223u; return (float)(s->rng >> 8) / 8388608.f - 1.f; }

void sy_note_on(Synth *s, int note, float vel, int owner, int group) {
    if (s->sound == SY_SILENT || note < 0 || note > 127) return;
    float sr = s->sr;
    /* retrigger choke: an older, already-releasing copy of this note fades in ~6 ms, so fast repeats don't pile up */
    float choke = vc_decay_mul(0.006, sr);
    for (int i = 0; i < SY_VOICES; i++) {
        SVoice *o = &s->v[i];
        if (o->active && o->note == note && o->released && o->relMul > choke) o->relMul = choke;
    }
    SVoice *v = alloc_voice(s, group);
    if (!v) return;
    float *ks = v->ks;
    memset(v, 0, sizeof(*v));
    v->ks = ks;
    v->active = 1; v->note = note; v->owner = owner; v->group = group; v->sound = s->sound; v->vel = vel; v->age = ++s->age;
    float hz = vc_midi_hz(note);
    v->inc = hz / sr;
    float pan = (note - 60) / 60.f;
    v->pan = pan > 0.35f ? 0.35f : pan < -0.35f ? -0.35f : pan;
    double rel = 0.05 + s->release * 2.5;
    double low = vc_exp2(-(note - 48) / 24.0); /* lower notes ring longer */
    switch (v->sound) {
    case SY_EPIANO: {
        v->attK = 1.f - vc_decay_mul(0.0015, sr);
        double tau = 1.0 + 3.5 * low; if (tau > 6.0) tau = 6.0;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(rel * 0.4, sr);
        v->modEnv = 1.f; v->modMul = vc_decay_mul(0.25 + 0.3 * (1.f - s->tone), sr);
        v->modDepth = (0.5f + 2.2f * s->tone) * (0.35f + 0.65f * vel);
        v->tineEnv = 1.f; v->tineMul = vc_decay_mul(0.025, sr);
        break;
    }
    case SY_WURLI: {
        v->attK = 1.f - vc_decay_mul(0.001, sr);
        double tau = 0.8 + 1.8 * low; if (tau > 4.0) tau = 4.0;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(0.05 + rel * 0.2, sr);
        v->modEnv = 1.f; v->modMul = vc_decay_mul(0.12 + 0.2 * (1.f - s->tone), sr);
        v->modDepth = (0.8f + 1.8f * s->tone) * (0.3f + 0.7f * vel);
        v->lfoInc = 5.6f / sr;
        break;
    }
    case SY_CLAV: {
        v->attK = 1.f - vc_decay_mul(0.0005, sr);
        double tau = 0.35 + 0.4 * low; if (tau > 1.2) tau = 1.2;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(0.03 + rel * 0.05, sr);
        double fc = 1200.0 + (2500.0 + 6000.0 * s->tone) * vel; if (fc > sr * 0.45) fc = sr * 0.45;
        v->lpA = 1.f - (float)vc_exp(-6.283185307 * fc / sr);
        break;
    }
    case SY_PAD: {
        v->attK = 1.f - vc_decay_mul(0.18, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.08 + rel * 0.5, sr);
        double fc = 250.0 + s->tone * 4500.0 + vel * 1200.0; if (fc > sr * 0.45) fc = sr * 0.45;
        v->lpA = 1.f - (float)vc_exp(-6.283185307 * fc / sr);
        v->ph[1] = 0.37f; v->ph[2] = 0.71f;
        break;
    }
    case SY_STRINGS: {
        v->attK = 1.f - vc_decay_mul(0.22, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.25 + rel * 0.5, sr);
        double fc = 700.0 + s->tone * 3500.0 + vel * 800.0; if (fc > sr * 0.45) fc = sr * 0.45;
        v->lpA = 1.f - (float)vc_exp(-6.283185307 * fc / sr);
        v->ph[1] = 0.29f; v->ph[2] = 0.61f;
        v->lfoInc = (4.8f + 0.4f * rnd(s)) / sr;
        v->lfo = 0.5f + 0.5f * rnd(s);
        break;
    }
    case SY_BRASS: {
        v->attK = 1.f - vc_decay_mul(0.03, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.08 + rel * 0.25, sr);
        v->fenv = 1.f; v->fenvMul = vc_decay_mul(0.25, sr);     /* filter opens, then settles */
        v->ph[1] = 0.43f;
        break;
    }
    case SY_VIBES: {
        v->attK = 1.f - vc_decay_mul(0.001, sr);
        double tau = 1.6 + 2.0 * low; if (tau > 5.0) tau = 5.0;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(0.1 + rel * 0.6, sr);
        v->modEnv = 1.f; v->modMul = vc_decay_mul(0.08, sr);
        v->modDepth = (0.4f + 1.2f * s->tone) * vel;
        v->lfoInc = 6.0f / sr;                                     /* motor tremolo */
        break;
    }
    case SY_PLUCK: case SY_HARP: case SY_NYLON: {
        v->attK = 1.f;
        v->amp = vel; v->stage = 1;
        double tau = v->sound == SY_HARP ? 2.0 + 3.0 * low : v->sound == SY_NYLON ? 1.0 + 1.6 * low : 1.2 + 2.0 * low;
        if (tau > 5.0) tau = 5.0;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(v->sound == SY_HARP ? 0.3 + rel * 0.4 : 0.04 + rel * 0.15, sr);
        float bright = v->sound == SY_HARP ? 0.45f + 0.5f * s->tone * vel : v->sound == SY_NYLON ? 0.12f + 0.3f * s->tone * vel
                                           : 0.25f + 0.7f * s->tone * (0.4f + 0.6f * vel);
        float damp = v->sound == SY_HARP ? 0.4996f : v->sound == SY_NYLON ? 0.4978f
                                         : 0.4985f + 0.0013f * (float)(1.0 - low * 0.3 > 0 ? 1.0 - low * 0.3 : 0);
        ks_start(s, v, hz, bright, damp);
        break;
    }
    case SY_RHODES: case SY_DYNO: {
        int dyno = v->sound == SY_DYNO;
        v->attK = 1.f - vc_decay_mul(dyno ? 0.0008 : 0.0015, sr);
        double tau = (dyno ? 0.9 : 1.2) + (dyno ? 2.5 : 3.5) * low; if (tau > 6.0) tau = 6.0;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(0.05 + rel * 0.35, sr);
        v->modEnv = 1.f; v->modMul = vc_decay_mul(dyno ? 0.15 + 0.15 * (1.f - s->tone) : 0.35 + 0.3 * (1.f - s->tone), sr);
        v->modDepth = (dyno ? 1.3f + 2.6f * s->tone : 0.35f + 1.3f * s->tone) * (0.3f + 0.7f * vel);
        v->tineEnv = 1.f; v->tineMul = vc_decay_mul(dyno ? 0.03 : 0.07, sr);
        v->lfoInc = dyno ? 0.f : 4.5f / sr;                       /* Rhodes: stereo tremolo */
        break;
    }
    case SY_PIANO: {
        v->attK = 1.f - vc_decay_mul(0.0008, sr);
        double tau = 2.0 + 4.0 * low; if (tau > 8.0) tau = 8.0;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(0.08 + rel * 0.25, sr);
        v->modEnv = 1.f; v->modMul = vc_decay_mul(0.35, sr);    /* bright attack fading to the body */
        v->tineEnv = 1.f; v->tineMul = vc_decay_mul(0.008, sr); /* hammer */
        float B = 0.00025f, bright = (0.35f + 0.65f * s->tone) * (0.4f + 0.6f * vel);
        for (int k = 0; k < 8; k++) {
            float kk = (float)(k + 1);
            float stretch = 1.f + B * kk * kk;                      /* piano strings are slightly inharmonic */
            v->pinc[k] = v->inc * kk * stretch;
            float a = 1.f / (kk * (0.6f + 0.4f * kk));
            if (k > 0) a *= bright;
            v->pamp[k] = v->pinc[k] < 0.45f ? a : 0.f;
            v->pmul[k] = vc_decay_mul(tau / (1.0 + 0.45 * k), sr);
            v->php[k] = 0.f;
        }
        break;
    }
    case SY_POLY: {
        v->attK = 1.f - vc_decay_mul(0.006, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.1 + rel * 0.4, sr);
        v->fenv = 1.f; v->fenvMul = vc_decay_mul(0.3, sr);
        v->ph[1] = 0.5f; v->ph[2] = 0.21f;
        v->lfoInc = 0.6f / sr;                                     /* slow chorus drift */
        break;
    }
    case SY_SUPERSAW: {
        v->attK = 1.f - vc_decay_mul(0.01, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.12 + rel * 0.5, sr);
        static const float DET[5] = {-1.f, -0.5f, 0.f, 0.5f, 1.f};
        float spread = 0.006f + 0.008f * s->tone;
        for (int k = 0; k < 5; k++) { v->pinc[k] = v->inc * (1.f + DET[k] * spread); v->php[k] = 0.137f * (float)k; }
        double fc = 2500.0 + s->tone * 7000.0; if (fc > sr * 0.45) fc = sr * 0.45;
        v->lpA = 1.f - (float)vc_exp(-6.283185307 * fc / sr);
        break;
    }
    case SY_CHOIR: {
        v->attK = 1.f - vc_decay_mul(0.15, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.2 + rel * 0.5, sr);
        float f1 = 730.f, f2 = 1090.f + 300.f * s->tone;           /* "ah" formants */
        v->sf1 = 2.f * vc_sinc(f1 / sr * 0.5f); v->sf2 = 2.f * vc_sinc(f2 / sr * 0.5f);
        v->sq = 0.22f;
        v->lfoInc = (5.0f + 0.5f * rnd(s)) / sr;
        break;
    }
    case SY_MARIMBA: {
        v->attK = 1.f - vc_decay_mul(0.0008, sr);
        double tau = 0.35 + 0.6 * low; if (tau > 1.5) tau = 1.5;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(0.05 + rel * 0.2, sr);
        v->modEnv = 1.f; v->modMul = vc_decay_mul(0.04, sr);      /* bright overtone dies fast */
        v->modDepth = 0.35f + 0.4f * s->tone;
        break;
    }
    case SY_MUSICBOX: {
        v->inc *= 2.f;                                            /* plays an octave up, like the real thing */
        v->attK = 1.f - vc_decay_mul(0.0005, sr);
        double tau = 1.2 + 1.2 * low; if (tau > 3.0) tau = 3.0;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(0.2 + rel * 0.4, sr);
        v->modEnv = 1.f; v->modMul = vc_decay_mul(0.25, sr);
        v->modDepth = (0.5f + 1.0f * s->tone) * vel;
        break;
    }
    case SY_FMBELL: { /* two-operator FM with an inharmonic ratio: a bell that starts bright and settles to a pure tone */
        v->attK = 1.f - vc_decay_mul(0.0008, sr);
        double tau = 1.2 + 2.8 * low; if (tau > 5.0) tau = 5.0;
        v->decMul = vc_decay_mul(tau, sr);
        v->relMul = vc_decay_mul(0.05 + rel * 0.3, sr);
        v->modEnv = 1.f; v->modMul = vc_decay_mul(0.4 + 0.8 * low, sr);
        v->modDepth = (1.5f + 3.0f * s->tone) * (0.4f + 0.6f * vel);
        break;
    }
    case SY_SYNBASS: { /* two saws and a sub, through a low-pass that opens on each note */
        v->attK = 1.f - vc_decay_mul(0.004, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.03 + rel * 0.15, sr);
        v->fenv = 1.f; v->fenvMul = vc_decay_mul(0.18 + 0.2 * (1.f - s->tone), sr);
        v->ph[1] = 0.5f;
        break;
    }
    case SY_GLASS: { /* slow attack, a detuned sine pair and a slow FM shimmer */
        v->attK = 1.f - vc_decay_mul(0.45, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.3 + rel * 0.4, sr);
        v->ph[1] = 0.31f; v->ph[2] = 0.67f;
        v->modEnv = 1.f; v->modMul = vc_decay_mul(1.5, sr);
        v->modDepth = 0.6f + 1.4f * s->tone;
        v->lfoInc = (4.2f + 0.8f * rnd(s)) / sr;
        v->lfo = 0.5f + 0.5f * rnd(s);
        break;
    }
    case SY_SAWLEAD: { /* three detuned saws with vibrato; the filter opens on each note */
        v->attK = 1.f - vc_decay_mul(0.012, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.1 + rel * 0.35, sr);
        v->fenv = 1.f; v->fenvMul = vc_decay_mul(0.35, sr);
        v->ph[1] = 0.33f; v->ph[2] = 0.66f;
        v->lfoInc = 5.5f / sr;
        break;
    }
    default: { /* organ */
        v->attK = 1.f - vc_decay_mul(0.003, sr);
        v->decMul = 1.f;
        v->relMul = vc_decay_mul(0.02 + rel * 0.2, sr);
        break;
    }
    }
}

void sy_release(Synth *s, int note, int owner) {
    for (int i = 0; i < SY_VOICES; i++) {
        SVoice *v = &s->v[i];
        if (v->active && !v->released && v->owner == owner && (note < 0 || v->note == note)) v->released = 1;
    }
}

void sy_kill_all(Synth *s) { for (int i = 0; i < SY_VOICES; i++) s->v[i].active = 0; }

int sy_active(const Synth *s) { int n = 0; for (int i = 0; i < SY_VOICES; i++) n += s->v[i].active; return n; }

static inline float blep_saw(float ph, float inc) {
    float y = 2.f * ph - 1.f;
    if (ph < inc) { float t = ph / inc; y -= t + t - t * t - 1.f; }
    else if (ph > 1.f - inc) { float t = (ph - 1.f) / inc; y -= t * t + t + t + 1.f; }
    return y;
}

void sy_render(Synth *s, float *L, float *R, int n) {
    static const float H[6] = {1.f, 2.f, 3.f, 4.f, 6.f, 8.f};
    float drawbar[6] = {1.f, 0.8f, 0.55f * s->tone + 0.15f, 0.35f * s->tone, 0.25f * s->tone, 0.18f * s->tone};
    float g = s->volume * s->volume * 0.22f;
    float sr = s->sr;
    for (int i = 0; i < SY_VOICES; i++) {
        SVoice *v = &s->v[i];
        if (!v->active) continue;
        float gl = g * (1.f - v->pan), gr = g * (1.f + v->pan);
        for (int k = 0; k < n; k++) {
            if (v->released) v->amp *= v->relMul;
            else if (v->stage == 0) { v->amp += (v->vel - v->amp) * v->attK; if (v->amp >= v->vel * 0.995f) v->stage = 1; }
            else v->amp *= v->decMul;
            float y;
            switch (v->sound) {
            case SY_EPIANO: {
                float m = vc_sinc(v->ph[1]) * v->modDepth * (0.2f + 0.8f * v->modEnv);
                y = vc_sinc(v->ph[0] + m * 0.159155f);
                if (v->inc * 14.f < 0.45f) y += vc_sinc(v->ph[2]) * v->tineEnv * 0.12f;
                v->modEnv *= v->modMul; v->tineEnv *= v->tineMul;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc); v->ph[1] = vc_wrap1(v->ph[1] + v->inc); v->ph[2] = vc_wrap1(v->ph[2] + v->inc * 14.f);
                break;
            }
            case SY_WURLI: {
                float m = vc_sinc(v->ph[1]) * v->modDepth * (0.25f + 0.75f * v->modEnv);
                float c = vc_sinc(v->ph[0] + m * 0.159155f);
                y = c + 0.22f * c * c - 0.11f;                          /* reedy, slightly asymmetric */
                y *= 1.f - 0.16f * (0.5f + 0.5f * vc_sinc(v->lfo));     /* tremolo */
                v->modEnv *= v->modMul;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc); v->ph[1] = vc_wrap1(v->ph[1] + v->inc); v->lfo = vc_wrap1(v->lfo + v->lfoInc);
                break;
            }
            case SY_CLAV: {
                float p2 = v->ph[0] + 0.28f; if (p2 >= 1.f) p2 -= 1.f;
                float x = blep_saw(v->ph[0], v->inc) - blep_saw(p2, v->inc);   /* narrow pulse */
                v->lp1 += (x - v->lp1) * v->lpA;
                y = (x - 0.6f * v->lp1) * 0.55f + v->lp1 * 0.35f;              /* bright, a little hollow */
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc);
                break;
            }
            case SY_PAD: {
                float a = blep_saw(v->ph[0], v->inc * 1.004f), b = blep_saw(v->ph[1], v->inc * 0.996f);
                float x = 0.5f * (a + b) + 0.35f * vc_sinc(v->ph[2] * 0.5f);
                v->lp1 += (x - v->lp1) * v->lpA; v->lp2 += (v->lp1 - v->lp2) * v->lpA;
                y = v->lp2 * 0.8f;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc * 1.004f); v->ph[1] = vc_wrap1(v->ph[1] + v->inc * 0.996f);
                v->ph[2] += v->inc; if (v->ph[2] >= 2.f) v->ph[2] -= 2.f;
                break;
            }
            case SY_STRINGS: {
                float vib = 1.f + 0.0035f * vc_sinc(v->lfo) * (v->amp > 0.5f * v->vel ? 1.f : 0.3f);
                float i0 = v->inc * vib;
                float x = blep_saw(v->ph[0], i0 * 1.0045f) + blep_saw(v->ph[1], i0 * 0.9955f) + blep_saw(v->ph[2], i0);
                v->lp1 += (x - v->lp1) * v->lpA; v->lp2 += (v->lp1 - v->lp2) * v->lpA;
                y = v->lp2 * 0.33f;
                v->ph[0] = vc_wrap1(v->ph[0] + i0 * 1.0045f); v->ph[1] = vc_wrap1(v->ph[1] + i0 * 0.9955f); v->ph[2] = vc_wrap1(v->ph[2] + i0);
                v->lfo = vc_wrap1(v->lfo + v->lfoInc);
                break;
            }
            case SY_BRASS: {
                float x = blep_saw(v->ph[0], v->inc * 1.003f) + blep_saw(v->ph[1], v->inc * 0.997f);
                float fc = (400.f + 2600.f * s->tone * v->vel) * (0.45f + 0.55f * (1.f - v->fenv)) + 300.f * v->vel;
                if (v->fenv > 0.f && v->stage == 1) v->fenv *= v->fenvMul; /* swell then settle */
                float a = 6.2831853f * fc / sr; if (a > 0.9f) a = 0.9f;
                v->lp1 += (x - v->lp1) * a; v->lp2 += (v->lp1 - v->lp2) * a;
                y = v->lp2 * 0.45f;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc * 1.003f); v->ph[1] = vc_wrap1(v->ph[1] + v->inc * 0.997f);
                break;
            }
            case SY_VIBES: {
                float m = vc_sinc(v->ph[1]) * v->modDepth * v->modEnv;
                y = vc_sinc(v->ph[0] + m * 0.159155f);
                y *= 1.f - 0.3f * (0.5f + 0.5f * vc_sinc(v->lfo));
                v->modEnv *= v->modMul;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc); v->ph[1] = vc_wrap1(v->ph[1] + v->inc * 4.f); v->lfo = vc_wrap1(v->lfo + v->lfoInc);
                break;
            }
            case SY_PLUCK: {
                int p = v->kspos, q = p + 1 == v->kslen ? 0 : p + 1;
                float o = v->ks[p];
                v->ks[p] = (o + v->ks[q]) * v->ksdamp;
                v->kspos = q;
                y = o * 1.6f;
                break;
            }
            case SY_HARP: case SY_NYLON: {
                int p = v->kspos, q = p + 1 == v->kslen ? 0 : p + 1;
                float o = v->ks[p];
                v->ks[p] = (o + v->ks[q]) * v->ksdamp;
                v->kspos = q;
                y = o * (v->sound == SY_HARP ? 1.3f : 1.9f);
                break;
            }
            case SY_RHODES: case SY_DYNO: {
                float m = vc_sinc(v->ph[1]) * v->modDepth * (0.2f + 0.8f * v->modEnv);
                y = vc_sinc(v->ph[0] + m * 0.159155f);
                if (v->inc * 14.f < 0.45f) y += vc_sinc(v->ph[2]) * v->tineEnv * (v->sound == SY_RHODES ? 0.22f : 0.16f);
                v->modEnv *= v->modMul; v->tineEnv *= v->tineMul;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc); v->ph[1] = vc_wrap1(v->ph[1] + v->inc); v->ph[2] = vc_wrap1(v->ph[2] + v->inc * 14.f);
                if (v->lfoInc > 0.f) v->lfo = vc_wrap1(v->lfo + v->lfoInc);
                break;
            }
            case SY_PIANO: {
                float x = 0.f;
                for (int p = 0; p < 8; p++) {
                    if (v->pamp[p] <= 0.f) continue;
                    x += vc_sinc(v->php[p]) * v->pamp[p];
                    v->php[p] = vc_wrap1(v->php[p] + v->pinc[p]);
                    v->pamp[p] *= v->pmul[p];
                }
                s->rng = s->rng * 1664525u + 1013904223u;
                float nz = (float)(s->rng >> 8) / 8388608.f - 1.f;
                y = x * (0.75f + 0.25f * v->modEnv) + nz * v->tineEnv * 0.15f * v->vel;
                v->modEnv *= v->modMul; v->tineEnv *= v->tineMul;
                break;
            }
            case SY_POLY: {
                float drift = 1.f + 0.0025f * vc_sinc(v->lfo);
                float a = blep_saw(v->ph[0], v->inc), b = blep_saw(v->ph[1], v->inc * 0.997f * drift);
                float p2 = v->ph[2] + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
                float sq = 0.5f * (blep_saw(v->ph[2], v->inc * 0.5f) - blep_saw(p2, v->inc * 0.5f)); /* sub octave */
                float x = 0.45f * (a + b) + 0.3f * sq;
                float fc = (500.f + 3500.f * s->tone + 1200.f * v->vel) * (0.55f + 0.7f * v->fenv);
                if (v->fenv > 0.f) v->fenv *= v->fenvMul;
                float k2 = 6.2831853f * fc / sr; if (k2 > 0.9f) k2 = 0.9f;
                v->lp1 += (x - v->lp1) * k2; v->lp2 += (v->lp1 - v->lp2) * k2;
                y = v->lp2 * 0.5f;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc); v->ph[1] = vc_wrap1(v->ph[1] + v->inc * 0.997f * drift);
                v->ph[2] = vc_wrap1(v->ph[2] + v->inc * 0.5f); v->lfo = vc_wrap1(v->lfo + v->lfoInc);
                break;
            }
            case SY_SUPERSAW: {
                float x = 0.f;
                for (int p = 0; p < 5; p++) { x += blep_saw(v->php[p], v->pinc[p]); v->php[p] = vc_wrap1(v->php[p] + v->pinc[p]); }
                v->lp1 += (x - v->lp1) * v->lpA;
                y = v->lp1 * 0.2f;
                break;
            }
            case SY_CHOIR: {
                float vib = 1.f + 0.004f * vc_sinc(v->lfo);
                float x = blep_saw(v->ph[0], v->inc * vib);
                /* two state-variable band-passes = the vowel's formants */
                v->s1l += v->sf1 * v->s1b; float h1 = x - v->s1l - v->sq * v->s1b; v->s1b += v->sf1 * h1;
                v->s2l += v->sf2 * v->s2b; float h2 = x - v->s2l - v->sq * v->s2b; v->s2b += v->sf2 * h2;
                v->lp1 += (x - v->lp1) * 0.05f;
                y = 0.32f * v->s1b + 0.2f * v->s2b + 0.35f * v->lp1;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc * vib); v->lfo = vc_wrap1(v->lfo + v->lfoInc);
                break;
            }
            case SY_MARIMBA: {
                y = vc_sinc(v->ph[0]) + vc_sinc(v->ph[1]) * v->modDepth * v->modEnv;
                v->modEnv *= v->modMul;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc);
                if (v->inc * 3.93f < 0.45f) v->ph[1] = vc_wrap1(v->ph[1] + v->inc * 3.93f);
                break;
            }
            case SY_MUSICBOX: {
                float m = vc_sinc(v->ph[1]) * v->modDepth * v->modEnv;
                y = vc_sinc(v->ph[0] + m * 0.159155f);
                v->modEnv *= v->modMul;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc);
                if (v->inc * 3.5f < 0.45f) v->ph[1] = vc_wrap1(v->ph[1] + v->inc * 3.5f);
                break;
            }
            case SY_FMBELL: {
                float m = vc_sinc(v->ph[1]) * v->modDepth * v->modEnv;
                y = vc_sinc(v->ph[0] + m * 0.159155f);
                v->modEnv *= v->modMul;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc); v->ph[1] = vc_wrap1(v->ph[1] + v->inc * 3.5f);
                break;
            }
            case SY_SYNBASS: {
                float x = 0.5f * (blep_saw(v->ph[0], v->inc) + blep_saw(v->ph[1], v->inc * 1.006f));
                float sub = vc_sinc(v->ph[2]);
                float fc = (180.f + 2200.f * s->tone * v->vel) * (0.25f + 0.75f * v->fenv) + 120.f;
                float a = 6.2831853f * fc / sr; if (a > 0.9f) a = 0.9f;
                v->lp1 += (x - v->lp1) * a; v->lp2 += (v->lp1 - v->lp2) * a;
                y = (v->lp2 * 0.8f + 0.35f * sub) * 0.8f;
                if (v->fenv > 0.f) v->fenv *= v->fenvMul;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc); v->ph[1] = vc_wrap1(v->ph[1] + v->inc * 1.006f); v->ph[2] = vc_wrap1(v->ph[2] + v->inc * 0.5f);
                break;
            }
            case SY_GLASS: {
                float vib = 1.f + 0.002f * vc_sinc(v->lfo);
                float shimmer = v->inc * 3.011f < 0.45f ? vc_sinc(v->ph[2]) * v->modDepth * v->modEnv : 0.f;
                float x = vc_sinc(v->ph[0] + shimmer * 0.159155f);
                if (v->inc * 2.003f < 0.45f) x += 0.45f * vc_sinc(v->ph[1]);
                y = x * 0.6f;
                v->modEnv *= v->modMul;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc * vib); v->ph[1] = vc_wrap1(v->ph[1] + v->inc * 2.003f);
                v->ph[2] = vc_wrap1(v->ph[2] + v->inc * 3.011f); v->lfo = vc_wrap1(v->lfo + v->lfoInc);
                break;
            }
            case SY_SAWLEAD: {
                float vib = 1.f + 0.003f * vc_sinc(v->lfo) * (v->stage == 1 ? 1.f : 0.4f);
                float i0 = v->inc * vib;
                float x = blep_saw(v->ph[0], i0 * 1.005f) + blep_saw(v->ph[1], i0 * 0.995f) + blep_saw(v->ph[2], i0);
                float fc = (300.f + 3800.f * s->tone * v->vel) * (0.2f + 0.8f * v->fenv) + 250.f;
                float a = 6.2831853f * fc / sr; if (a > 0.9f) a = 0.9f;
                v->lp1 += (x - v->lp1) * a; v->lp2 += (v->lp1 - v->lp2) * a;
                y = v->lp2 * 0.3f;
                if (v->fenv > 0.f) v->fenv *= v->fenvMul;
                v->ph[0] = vc_wrap1(v->ph[0] + i0 * 1.005f); v->ph[1] = vc_wrap1(v->ph[1] + i0 * 0.995f); v->ph[2] = vc_wrap1(v->ph[2] + i0);
                v->lfo = vc_wrap1(v->lfo + v->lfoInc);
                break;
            }
            default: {
                y = 0.f;
                for (int h = 0; h < 6; h++) if (v->inc * H[h] < 0.45f) y += vc_sinc(v->ph[0] * H[h]) * drawbar[h];
                y *= 0.32f;
                v->ph[0] = vc_wrap1(v->ph[0] + v->inc);
                break;
            }
            }
            float a = v->amp;
            if (v->sound == SY_RHODES) {
                float pl = v->pan + 0.4f * vc_sinc(v->lfo);
                L[k] += y * a * g * (1.f - pl);
                R[k] += y * a * g * (1.f + pl);
            } else {
                L[k] += y * a * gl;
                R[k] += y * a * gr;
            }
        }
        if ((v->released || (sy_sound_decays(v->sound) && v->stage == 1)) && v->amp < 0.0002f) v->active = 0;
    }
    for (int k = 0; k < n; k++) { L[k] = vc_softclip(L[k]); R[k] = vc_softclip(R[k]); }
}
