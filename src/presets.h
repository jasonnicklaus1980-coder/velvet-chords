#ifndef VC_PRESETS_H
#define VC_PRESETS_H
/* Factory presets: performance settings only (sound, voicing, strum, arpeggio, pattern, smart pads, melody).
 * The chords, key, scale, song and custom pads are not part of a preset. Generated from data/presets.json.
 * Units: octave and smart_oct are offsets (-2..+2); arp_oct is 1..4 notes-per-arpeggio octaves; the rest are the
 * choice indexes of the matching parameter (see plugin.c). */

typedef struct {
    const char *name, *genre;
    int key, scale;                 /* the key (0 = C) and the scale (index into VC_SCALES) */
    int sound, voicing, inversion, octave, voice_lead, strum_dir, arp, arp_rate, arp_oct, vel;
    int pattern, pat_rate, smart, smart_mode, smart_oct, melody;
    float tone, release, volume, strum_ms, swing, gate;
} VcPreset;

extern const VcPreset VC_PRESETS[];
extern const int VC_NUM_PRESETS;
/* presets per genre: the genre and variation knobs split the list this way (preset = genre * 10 + variation) */
#define VC_PRESET_VARS 10

#endif
