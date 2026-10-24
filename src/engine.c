#include "engine.h"
#include "progression.h"
#include <string.h>

/* Original rhythm presets in the spirit of common R&B, soul and Caribbean comping figures (16 steps). */
const PatternPreset EN_PATTERNS[] = {
    {"Sustain", "X---------------"},
    {"Half notes", "X-------x-------"},
    {"Quarters", "X---x---x---x---"},
    {"Eighths", "X-x-x-x-X-x-x-x-"},
    {"Ballad pulse", "X-----x-------x-"},
    {"Neo-soul push", "X-----x---x---x-"},
    {"Slow jam", "X--x----x--x--x-"},
    {"Gospel drive", "X.xX.xx.X.xX.xx."},
    {"Funk 16ths", "X.xx.xX.x.xx.xX."},
    {"Bass and stabs", "b...U.u.b...U.u."},
    {"Reggae skank", "b.U.b.U.b.U.b.U."},
    {"Caribbean 3-3-2", "X..x..x.X..x..x."},
    {"Dancehall", "b..U..U.b..U..u."},
    {"Afrobeats", "X..x..x...x.x..."},
    {"Half-time", "X-------.....x--"},
};
const int EN_NUM_PATTERNS = (int)(sizeof(EN_PATTERNS) / sizeof(EN_PATTERNS[0]));
const char EN_STEP_CHARS[] = ".xXbuU-";

/* Original melodic riffs, as semitone offsets from the chord root (see engine.h) */
#define MR RIFF_REST
const char *EN_RIFF_NAMES[VC_RIFFS] = {"Root and fifth", "Octave bounce", "Thirds run", "Pentatonic lift",
                                             "Syncopated fifths", "Tonic pedal"};
const signed char EN_RIFF_DEFAULTS[VC_RIFFS][EN_STEPS] = {
    {0, MR, MR, MR, 7, MR, MR, MR, 0, MR, MR, MR, 7, MR, MR, MR},
    {0, MR, 12, MR, 0, MR, 12, MR, 0, MR, 12, MR, 0, MR, 12, MR},
    {4, MR, 7, MR, 9, MR, 11, MR, 12, MR, 11, MR, 9, MR, 7, MR},
    {0, MR, 3, MR, 5, MR, 7, MR, 10, MR, 7, MR, 5, MR, 3, MR},
    {MR, 7, MR, MR, 7, MR, 12, MR, MR, 0, MR, MR, 7, MR, MR, MR},
    {0, MR, MR, 0, MR, MR, 0, MR, 0, MR, MR, 0, MR, MR, 0, MR},
};
#undef MR

int en_shift_action(int note) {
    if (note < 24 || note > 35) return -1; /* the octave below the pads (pads start at 36) */
    switch (note % 12) {
    case 1: return SHIFT_KEY_DOWN;      /* C# */
    case 3: return SHIFT_KEY_UP;        /* D# */
    case 6: return SHIFT_SCALE_PREV;    /* F# */
    case 8: return SHIFT_SCALE_NEXT;    /* G# */
    case 10: return SHIFT_INVERSION;    /* A# */
    default: return -1;
    }
}

SharedChord g_vc_shared;
SharedKey g_vc_key;
static int g_next_instance = 1;

/* the scale note `steps` scale degrees from `note` in the key's scale (a note off the scale counts as the degree below it) */
static int scale_step_note(const Engine *e, int note, int steps) {
    const Scale *sc = &VC_SCALES[(e->lp.scale >= 0 && e->lp.scale < VC_NUM_SCALES) ? e->lp.scale : 0];
    int key = ((e->lp.key % 12) + 12) % 12;
    int rel = (((note - key) % 12) + 12) % 12;
    int base = note - rel;
    int i = 0;
    for (int k = 0; k < sc->n; k++) if (sc->iv[k] <= rel) i = k;
    int j = i + steps;
    int q = j >= 0 ? j / sc->n : -((-j + sc->n - 1) / sc->n); /* floor division */
    int idx = j - q * sc->n;
    int out = base + q * 12 + sc->iv[idx];
    return out < 0 ? out + 12 : out > 127 ? out - 12 : out;
}

/* the second voice for a melody note, or -1 */
static int harmony_note(const Engine *e, int note) {
    switch (e->harmony) {
    case HARM_THIRD_UP: return scale_step_note(e, note, 2);
    case HARM_SIXTH_UP: return scale_step_note(e, note, 5);
    case HARM_THIRD_DOWN: return scale_step_note(e, note, -2);
    case HARM_OCTAVE: return note + 12 <= 127 ? note + 12 : -1;
    default: return -1;
    }
}

int en_smart_index(const Engine *e, int pad) {
    int k = pad % 16;
    if (e->smart == SMART_FOLLOW) return k;
    if (e->smart == SMART_SPLIT && k >= 8) return k - 8;
    return -1;
}

static int engine_flats(const Engine *e) {
    if (e->lp.mode == VC_MODE_SONG && e->lp.song >= 0 && e->lp.song < VC_NUM_SONGS) return vc_song_flats(&VC_SONGS[e->lp.song], e->lp.transpose);
    return vc_prefer_flats(e->lp.key, vc_scale_is_minor(e->lp.scale));
}

static void chord_from_pad(SmartChord *sc, const PadChord *pc) {
    memset(sc, 0, sizeof(*sc));
    if (!pc->valid) return;
    int mask = vc_chord_pcmask(&pc->spec);
    if (pc->spec.bass >= 0) { /* slash bass isn't a chord tone unless it already was */
        ChordSpec t = pc->spec; t.bass = -1;
        mask = vc_chord_pcmask(&t);
    }
    sc->valid = 1;
    sc->root = pc->spec.root;
    sc->bass = pc->spec.bass >= 0 ? pc->spec.bass : pc->spec.root;
    for (int i = 0; i < 12; i++) if (mask & (1 << ((sc->root + i) % 12))) sc->pcs[sc->npcs++] = (sc->root + i) % 12;
    int k = 0;
    while (k < (int)sizeof(sc->name) - 1 && pc->name[k]) { sc->name[k] = pc->name[k]; k++; }
    sc->name[k] = 0;
}

/* rebuild the 16 smart notes from the followed chord */
void en_smart_update(Engine *e) {
    if (e->smart == SMART_FOLLOW) {
        if (g_vc_shared.chord.valid && g_vc_shared.source != e->instance_id) e->smart_src = g_vc_shared.chord;
        e->shared_seen = g_vc_shared.version;
    } else {
        e->smart_src = e->own_chord;
    }
    if (!e->smart_src.valid) { /* nothing played yet: follow pad 1 of bank A */
        SmartChord sc;
        chord_from_pad(&sc, &e->pads[0]);
        sc.flats = engine_flats(e);
        if (!sc.valid) { sc.valid = 1; sc.npcs = 1; sc.pcs[0] = e->lp.key % 12; sc.root = sc.bass = sc.pcs[0]; strcpy(sc.name, "-"); }
        e->smart_src = sc;
    }
    if (e->smart_map != SMAP_FOLLOW) { /* the fixed maps ignore the chord: 16 pads walk the notes up from the root */
        const Scale *sc = &VC_SCALES[e->lp.scale];
        int tones[12], m = 0, root = e->smart_map == SMAP_ABSOLUTE ? 0 : e->lp.key % 12;
        if (e->smart_map == SMAP_CHROMATIC) for (int i = 0; i < 12; i++) tones[m++] = i;
        else for (int i = 0; i < sc->n; i++) tones[m++] = sc->iv[i];
        int base = 60 + root + 12 * e->smart_oct;
        e->smart_changed = 1;
        for (int i = 0; i < 16; i++) {
            int n = base + tones[i % m] + 12 * (i / m);
            while (n > 127) n -= 12;
            e->smart_notes[i] = (unsigned char)n;
        }
        return;
    }
    const SmartChord *s = &e->smart_src;
    int tones[12], m = 0;
    if (e->smart_mode == SMODE_SCALE) { /* chord tones plus the key's scale, upward from the chord root */
        int mask = e->scale_pcs_mask;
        for (int i = 0; i < s->npcs; i++) mask |= 1 << s->pcs[i];
        for (int i = 0; i < 12; i++) if (mask & (1 << ((s->root + i) % 12))) tones[m++] = i;
    } else {
        for (int i = 0; i < s->npcs; i++) tones[m++] = (s->pcs[i] - s->root + 12) % 12;
    }
    if (m == 0) { tones[0] = 0; m = 1; }
    int base = (e->smart_mode == SMODE_BASS ? 36 : 60) + s->root + 12 * e->smart_oct;
    if (e->smart_mode == SMODE_BASS && s->bass != s->root) { /* bassline starts on the slash bass */
        base += (s->bass - s->root + 12) % 12 - 12;
        if (base < 24) base += 12;
    }
    e->smart_changed = 1;
    for (int i = 0; i < 16; i++) {
        int n = base + tones[i % m] + 12 * (i / m);
        if (e->smart_mode == SMODE_BASS && s->bass != s->root && i == 0) n = base;
        while (n > 127) n -= 12;
        while (n < 0) n += 12;
        e->smart_notes[i] = (unsigned char)n;
    }
}

int en_step_index(char c) {
    for (int i = 0; EN_STEP_CHARS[i]; i++) if (EN_STEP_CHARS[i] == c) return i;
    return 0;
}

void en_set_sr(Engine *e, float sr) {
    if (sr < 1000.f) sr = 44100.f;
    e->sr = sr;
    e->synth.sr = sr;
}

void en_init(Engine *e, float sr) {
    memset(e, 0, sizeof(*e));
    e->prog_bar = -1;
    e->bass_beat = -1;
    sy_init(&e->synth, sr);
    en_set_sr(e, sr);
    e->lp.mode = VC_MODE_KEY;
    e->lp.scale = 0;
    e->lp.voicing = VC_VOICE_BASS_UPPER;
    e->lp.voice_lead = 1;
    e->pad_base = 36; /* MPC "C1"; use Learn if your pads send something else */
    e->tempo = 90.0;
    e->last_pad = -1;
    e->last_note_in = -1;
    e->edit_pad_request = -1;
    e->edit_follow = 1;
    e->rng = 0x12345u;
    e->hum_rng = 0x2F6B3u;
    e->gate = 0.9f;
    for (int k = 0; k < EN_ARP_LANES; k++) {
        e->lanes[k].order = ARP_OFF;
        e->lanes[k].rate = RATE_16;
        e->lanes[k].oct = 1;
        e->lanes[k].cnt = -1;
        e->lanes[k].note = -1;
    }
    memcpy(e->rif, EN_RIFF_DEFAULTS, sizeof(e->rif));
    e->shift_keys = 1;
    e->pat_sync = 1;
    e->pat_rate = PRATE_16;
    memcpy(e->pat, EN_PATTERNS[0].steps, EN_STEPS);
    e->pat_len = EN_STEPS;
    e->pat_chance = 100;
    e->pat_seed = 1;
    e->instance_id = g_next_instance++;
    e->scale_pcs_mask = 0xAB5; /* C major */
    e->latched_note = -1;
    memset(e->pad_map, -1, sizeof(e->pad_map));
    e->mel_rng = 0x9E3779B9u ^ (unsigned)e->instance_id;
    e->mel_prev = 79;
    /* seed the custom banks with the Scale layout in C so Custom mode is playable out of the box */
    LayoutParams kit = e->lp;
    kit.mode = VC_MODE_KEY;
    PadChord tmp[VC_PADS];
    vc_build_pads(&kit, e->custom, tmp);
    for (int i = 0; i < 32; i++) {
        CustomPad *c = &e->custom[i];
        c->valid = (unsigned char)tmp[i].valid;
        c->root = (unsigned char)tmp[i].spec.root;
        c->quality = (unsigned char)(tmp[i].spec.quality < 0 ? 0 : tmp[i].spec.quality);
        c->bass = tmp[i].spec.bass < 0 ? 255 : (unsigned char)tmp[i].spec.bass;
        c->inv = 0;
        c->oct = (signed char)tmp[i].oct;
    }
    e->dirty = 1;
    en_rebuild(e);
}

void en_rebuild(Engine *e) {
    e->dirty = 0;
    vc_build_pads(&e->lp, e->custom, e->pads);
    vc_voice_pads(&e->lp, e->pads, e->voicings);
    /* scale used by Smart Scale "Scale" notes: the song's key in Song mode, else Key + Scale */
    int key = e->lp.key, sc = e->lp.scale, mask = 0;
    if (e->lp.mode == VC_MODE_SONG && e->lp.song >= 0 && e->lp.song < VC_NUM_SONGS) {
        key = VC_SONGS[e->lp.song].key_root + e->lp.transpose;
        sc = VC_SONGS[e->lp.song].minor ? 1 : 0;
    }
    if (sc < 0 || sc >= VC_NUM_SCALES) sc = 0;
    for (int i = 0; i < VC_SCALES[sc].n; i++) mask |= 1 << (((key + VC_SCALES[sc].iv[i]) % 12 + 12) % 12);
    e->scale_pcs_mask = mask;
    en_smart_update(e);
}

float en_vel_curve(int curve, int vel) {
    float x = vel / 127.f;
    switch (curve) {
    case VEL_SOFT: x = 1.f - (1.f - x) * (1.f - x); break;
    case VEL_HARD: x = x * x; break;
    case VEL_FIXED: x = 0.8f; break;
    default: break;
    }
    return x < 0.03f ? 0.03f : x > 1.f ? 1.f : x;
}

static void emit(Engine *e, int frame, unsigned char s, unsigned char d1, unsigned char d2) {
    if (e->nout >= EN_MIDI_OUT) return;
    MidiMsg *m = &e->out[e->nout++];
    m->frame = frame;
    m->d[0] = s; m->d[1] = d1; m->d[2] = d2;
}

static OwnerRec *find_owner(Engine *e, int owner) {
    if (!owner) return 0;
    for (int i = 0; i < EN_OWNERS; i++) if (e->owners[i].owner == owner) return &e->owners[i];
    return 0;
}

static void note_on_slot(Engine *e, OwnerRec *r, int slot, int frame) {
    if (slot < 0 || slot >= r->n || r->sounding[slot]) return;
    int note = r->notes[slot];
    sy_note_on(&e->synth, note, r->vel, r->owner);
    r->sounding[slot] = 1;
    if (e->out_ref[note]++ == 0 && e->midi_out) emit(e, frame, (unsigned char)(0x90 | (e->out_channel & 15)), (unsigned char)note, (unsigned char)(r->vel * 127.f + 0.5f));
}

static void note_off_slot(Engine *e, OwnerRec *r, int slot, int frame) {
    if (slot < 0 || slot >= r->n || !r->sounding[slot]) return;
    int note = r->notes[slot];
    sy_release(&e->synth, note, r->owner);
    r->sounding[slot] = 0;
    if (e->out_ref[note] > 0 && --e->out_ref[note] == 0 && e->midi_out) emit(e, frame, (unsigned char)(0x80 | (e->out_channel & 15)), (unsigned char)note, 0);
}

static void note_on_raw(Engine *e, OwnerRec *r, int note, int frame) {
    sy_note_on(&e->synth, note, r->vel, r->owner);
    if (e->out_ref[note]++ == 0 && e->midi_out) emit(e, frame, (unsigned char)(0x90 | (e->out_channel & 15)), (unsigned char)note, (unsigned char)(r->vel * 127.f + 0.5f));
}

static void note_off_raw(Engine *e, OwnerRec *r, int note, int frame) {
    sy_release(&e->synth, note, r->owner);
    if (e->out_ref[note] > 0 && --e->out_ref[note] == 0 && e->midi_out) emit(e, frame, (unsigned char)(0x80 | (e->out_channel & 15)), (unsigned char)note, 0);
}

/* a lane's current note goes silent */
static void arp_silence(Engine *e, ArpLane *l, OwnerRec *r, int frame) {
    if (l->note < 0) return;
    if (r) note_off_raw(e, r, l->note, frame);
    l->note = -1;
}

int en_arp_on(const Engine *e) {
    for (int k = 0; k < EN_ARP_LANES; k++) if (e->lanes[k].order != ARP_OFF) return 1;
    return 0;
}

static void pat_resume(Engine *e, OwnerRec *gone, int frame);

static void release_owner(Engine *e, OwnerRec *r, int frame) {
    if (!r || !r->owner) return;
    int k = 0;
    for (int i = 0; i < e->nqueue; i++) if (e->queue[i].owner != r->owner) e->queue[k++] = e->queue[i];
    e->nqueue = k;
    for (int k = 0; k < EN_ARP_LANES; k++)
        if (e->lanes[k].owner == r->owner) { arp_silence(e, &e->lanes[k], r, frame); e->lanes[k].owner = 0; }
    if (e->pat_owner == r->owner) { e->pat_owner = 0; e->pat_off_at = 0; pat_resume(e, r, frame); }
    if (e->mel_parent == r->owner) { int keep = r->owner; e->mel_parent = 0; OwnerRec *m = find_owner(e, e->mel_owner); e->mel_owner = 0; e->mel_off_at = 0; if (m && m->owner != keep) release_owner(e, m, frame); }
    if (e->latched_note >= 0 && e->pad_owner[e->latched_note] == r->owner) e->latched_note = -1;
    for (int s = 0; s < r->n; s++) note_off_slot(e, r, s, frame);
    sy_release(&e->synth, -1, r->owner);
    for (int n = 0; n < 128; n++) if (e->pad_owner[n] == r->owner) e->pad_owner[n] = 0;
    memset(r, 0, sizeof(*r));
}

static OwnerRec *alloc_owner(Engine *e, int frame) {
    OwnerRec *oldest = 0;
    for (int i = 0; i < EN_OWNERS; i++) {
        if (!e->owners[i].owner) return &e->owners[i];
        if (!oldest || e->owners[i].owner < oldest->owner) oldest = &e->owners[i];
    }
    release_owner(e, oldest, frame);
    return oldest;
}

void en_reset(Engine *e) {
    for (int i = 0; i < EN_OWNERS; i++) if (e->owners[i].owner) release_owner(e, &e->owners[i], 0);
    e->prog_owner = 0;
    e->prog_bar = -1;
    e->bass_owner = 0;
    e->bass_beat = -1;
    e->nqueue = 0;
    for (int k = 0; k < EN_ARP_LANES; k++) { e->lanes[k].owner = 0; e->lanes[k].note = -1; }
    e->pat_owner = 0;
    e->pat_off_at = 0;
    e->sustain = 0;
    memset(e->pad_owner, 0, sizeof(e->pad_owner));
    for (int n = 0; n < 128; n++) {
        if (e->out_ref[n] > 0 && e->midi_out) emit(e, 0, (unsigned char)(0x80 | (e->out_channel & 15)), (unsigned char)n, 0);
        e->out_ref[n] = 0;
    }
    sy_kill_all(&e->synth);
}

/* ---------- pattern generator ---------- */

static double pat_beats(int rate) {
    switch (rate) {
    case PRATE_8: return 0.5;
    case PRATE_8T: return 1.0 / 3.0;
    case PRATE_16T: return 1.0 / 6.0;
    case PRATE_32: return 0.125;
    default: return 0.25;
    }
}

static double rep_beats(int r) {
    switch (r) { case REP_4: return 1.0; case REP_8: return 0.5; case REP_8T: return 1.0 / 3.0;
                 case REP_16: return 0.25; case REP_16T: return 1.0 / 6.0; default: return 0.125; }
}

static double cur_step_beats(const Engine *e) { return e->pat_is_repeat ? rep_beats(e->repeat) : pat_beats(e->pat_rate); }

static double pat_step_samples(const Engine *e) {
    double tempo = e->tempo > 20.0 && e->tempo < 400.0 ? e->tempo : 90.0;
    return e->sr * 60.0 / tempo * cur_step_beats(e);
}

static double pat_swing(const Engine *e, int64_t k) { /* odd steps land late */
    return (k & 1) ? e->swing * 0.5 * pat_step_samples(e) : 0.0;
}

static void pat_release_notes(Engine *e, OwnerRec *r, int frame) {
    for (int s = 0; s < r->n; s++) note_off_slot(e, r, s, frame);
}

static void pat_off_after(Engine *e, int ties, int frame) {
    double len = ties * pat_step_samples(e) * (e->gate < 0.1f ? 0.1f : e->gate);
    if (len < e->sr * 0.005) len = e->sr * 0.005;
    e->pat_off_at = e->now + frame + (int64_t)len;
}

static void chord_restore(OwnerRec *r) { /* back to the chord as played, after a melodic step */
    if (!r->base_n || r->n == r->base_n) return;
    r->n = r->base_n;
    memcpy(r->notes, r->base_notes, (size_t)r->base_n);
}

/* melodic step: one note from the slot, transposed to the held chord */
static void riff_do(Engine *e, OwnerRec *r, int64_t k, int frame) {
    int off = e->rif[e->riff - 1][(int)(k % EN_STEPS)];
    pat_release_notes(e, r, frame);
    e->pat_off_at = 0;
    if (off == RIFF_REST) return;
    int lo = r->base_n ? r->base_notes[0] : 48;
    int note = (lo / 12 + 1) * 12 + r->root + off; /* the chord root, one octave above the lowest note */
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    r->n = 1;
    r->notes[0] = (unsigned char)note;
    memset(r->sounding, 0, sizeof(r->sounding));
    r->vel = r->vel_base;
    note_on_slot(e, r, 0, frame);
    pat_off_after(e, 1, frame);
}

/* deterministic chance: the same seed and step always give the same answer, so a pattern can be reproduced */
static int pat_step_plays(const Engine *e, int64_t k) {
    if (e->pat_chance >= 100) return 1;
    uint32_t h = (uint32_t)k * 2654435761u ^ (uint32_t)e->pat_seed * 40503u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (int)(h % 100) < e->pat_chance;
}

static void pat_do(Engine *e, int64_t k, int frame) {
    OwnerRec *r = find_owner(e, e->pat_owner);
    if (!r) { e->pat_owner = 0; return; }
    e->pat_last_k = k;
    if (e->riff) { riff_do(e, r, k, frame); return; }
    int steps = e->pat_len > 0 ? e->pat_len : EN_STEPS;
    char c = e->pat_is_repeat ? 'X' : e->pat[(int)(k % steps)]; /* Repeat = every step, full velocity */
    if (!e->pat_is_repeat && c != '.' && c != '-' && !pat_step_plays(e, k)) c = '.'; /* the chance skips this hit */
    if (c == '-') return; /* tie */
    pat_release_notes(e, r, frame);
    e->pat_off_at = 0;
    chord_restore(r);
    if (c == '.') return;
    int from = 0, to = r->n;
    if (c == 'b') to = r->n > 0 ? 1 : 0;
    if ((c == 'u' || c == 'U') && r->n > 1) from = 1;
    float accent = (c == 'X' || c == 'U') ? 1.0f : 0.78f;
    r->vel = r->vel_base * accent;
    if (r->vel < 0.03f) r->vel = 0.03f;
    for (int s = from; s < to; s++) note_on_slot(e, r, s, frame);
    r->vel = r->vel_base;
    int ties = 1;
    while (!e->pat_is_repeat && ties < steps && e->pat[(int)((k + ties) % steps)] == '-') ties++;
    double len = ties * pat_step_samples(e) * (e->gate < 0.1f ? 0.1f : e->gate);
    if (len < e->sr * 0.005) len = e->sr * 0.005;
    e->pat_off_at = e->now + frame + (int64_t)len;
}

/* place the step grid: from the host song position when synced and playing, else from this moment */
static void pat_align(Engine *e, int frame, int pressed) {
    double stepS = pat_step_samples(e);
    if (e->pat_sync && e->playing) {
        double tempo = e->tempo > 20.0 && e->tempo < 400.0 ? e->tempo : 90.0;
        double pos = (e->ppq + frame / e->sr * tempo / 60.0) / cur_step_beats(e); /* in steps */
        if (pos < 0) pos = 0;
        int64_t cur = (int64_t)pos;
        double frac = pos - (double)cur;
        /* a step that hasn't sounded yet still plays (also covers swung odd steps);
         * on a fresh press, only if the press is within the first quarter of the step */
        if (cur > e->pat_last_k && (!pressed || frac < 0.25)) {
            e->pat_k = cur;
            e->pat_grid = (double)(e->now + frame) - frac * stepS;
        } else {
            if (pressed) e->pat_last_k = cur; /* pressed late: wait for the next step */
            e->pat_k = cur + 1;
            e->pat_grid = (double)(e->now + frame) + (1.0 - frac) * stepS;
        }
    } else if (pressed) {
        e->pat_k = 0;
        e->pat_grid = (double)(e->now + frame);
    }
}

static void pat_start(Engine *e, int frame) {
    e->pat_last_k = -1;
    pat_align(e, frame, 1);
}

/* the pattern's pad was let go while another pad is still held (a tap on a second pad took the pattern):
 * the most recent held pad that is still down plays the pattern again */
static void pat_resume(Engine *e, OwnerRec *gone, int frame) {
    if (!e->pattern_on && e->repeat == REP_OFF) return;
    OwnerRec *best = 0;
    for (int i = 0; i < EN_OWNERS; i++) {
        OwnerRec *o = &e->owners[i];
        if (!o->owner || o == gone || !o->held) continue;
        if (!best || o->owner > best->owner) best = o;
    }
    if (!best) return;
    e->pat_owner = best->owner;
    e->pat_off_at = 0;
    pat_start(e, frame);
}

/* ---------- melody generator ---------- */

static double mel_beats(const Engine *e) { return e->mel_rate == MRATE_8 ? 0.5 : e->mel_rate == MRATE_8T ? 1.0 / 3.0 : 0.25; }
static double mel_step_samples(const Engine *e) {
    double tempo = e->tempo > 20.0 && e->tempo < 400.0 ? e->tempo : 90.0;
    return e->sr * 60.0 / tempo * mel_beats(e);
}
static unsigned mel_rand(Engine *e) { e->mel_rng = e->mel_rng * 1664525u + 1013904223u; return e->mel_rng >> 8; }

static void mel_stop(Engine *e, int frame) {
    OwnerRec *m = find_owner(e, e->mel_owner);
    e->mel_parent = 0;
    e->mel_owner = 0;
    e->mel_off_at = 0;
    if (m) release_owner(e, m, frame);
}

static void mel_align(Engine *e, int frame, int pressed) {
    double stepS = mel_step_samples(e);
    if (e->pat_sync && e->playing) {
        double tempo = e->tempo > 20.0 && e->tempo < 400.0 ? e->tempo : 90.0;
        double pos = (e->ppq + frame / e->sr * tempo / 60.0) / mel_beats(e);
        if (pos < 0) pos = 0;
        int64_t cur = (int64_t)pos;
        double frac = pos - (double)cur;
        if (cur > e->mel_last_k && (!pressed || frac < 0.25)) { e->mel_k = cur; e->mel_grid = (double)(e->now + frame) - frac * stepS; }
        else { if (pressed) e->mel_last_k = cur; e->mel_k = cur + 1; e->mel_grid = (double)(e->now + frame) + (1.0 - frac) * stepS; }
    } else if (pressed) { e->mel_k = 0; e->mel_grid = (double)(e->now + frame); }
}

static void mel_start(Engine *e, OwnerRec *parent, int frame) {
    if (e->mel_owner) mel_stop(e, frame);
    OwnerRec *m = alloc_owner(e, frame);
    if (++e->next_owner <= 0) e->next_owner = 1;
    memset(m, 0, sizeof(*m));
    m->owner = e->next_owner;
    m->held = 1;
    m->n = 1;
    m->vel = m->vel_base = parent->vel_base * 0.85f;
    e->mel_owner = m->owner;
    e->mel_parent = parent->owner;
    e->mel_off_at = 0;
    e->mel_last_k = -1;
    mel_align(e, frame, 1);
}

/* one melody step: chord tones on strong steps, chord+scale tones elsewhere, mostly stepwise, some rests */
static void mel_step(Engine *e, int64_t k, int frame) {
    OwnerRec *m = find_owner(e, e->mel_owner);
    if (!m || !find_owner(e, e->mel_parent)) { mel_stop(e, frame); return; }
    e->mel_last_k = k;
    int per_beat = e->mel_rate == MRATE_8 ? 2 : e->mel_rate == MRATE_8T ? 3 : 4;
    int strong = (k % per_beat) == 0;
    unsigned dens = e->mel_mode == MEL_BUSY ? 85 : 55;
    if (!strong && (mel_rand(e) % 100) >= dens) return;          /* rest: let the previous note ring */
    int cmask = 0;
    for (int i = 0; i < e->own_chord.npcs; i++) cmask |= 1 << e->own_chord.pcs[i];
    if (!cmask) cmask = e->scale_pcs_mask;
    int pool = strong ? cmask : (cmask | e->scale_pcs_mask);
    int lo = 67, hi = 91, prev = e->mel_prev, cand[32], nc = 0;
    for (int n = lo; n <= hi && nc < 32; n++) {
        if (!(pool & (1 << (n % 12)))) continue;
        int d = n - prev; if (d < 0) d = -d;
        if (d <= (strong ? 5 : 3)) cand[nc++] = n;                  /* small steps, a bit wider on strong beats */
    }
    int note;
    if (nc) {
        note = cand[mel_rand(e) % (unsigned)nc];
        if (note == prev && nc > 1 && (mel_rand(e) % 100) < 70) note = cand[(mel_rand(e)) % (unsigned)nc];
    } else { /* nothing near: nearest pool note */
        note = prev; int bd = 99;
        for (int n = lo; n <= hi; n++) if (pool & (1 << (n % 12))) { int d = n - prev; if (d < 0) d = -d; if (d < bd) { bd = d; note = n; } }
    }
    if (note > 86 && (mel_rand(e) % 100) < 40) note -= 12 * (note - 12 >= lo);  /* drift back down from the top */
    note_off_slot(e, m, 0, frame);
    m->notes[0] = (unsigned char)note;
    m->vel = m->vel_base * (strong ? 1.f : 0.8f);
    note_on_slot(e, m, 0, frame);
    e->mel_prev = note;
    e->mel_off_at = e->now + frame + (int64_t)(mel_step_samples(e) * 0.85);
}

static void pad_off(Engine *e, int note, int frame) {
    int owner = e->pad_owner[note];
    if (!owner) return;
    if (e->hold && note == e->latched_note) return; /* Hold: keeps sounding until another pad */
    e->pad_owner[note] = 0;
    OwnerRec *r = find_owner(e, owner);
    if (!r) return;
    r->held = 0;
    if (e->sustain) r->sustained = 1;
    else release_owner(e, r, frame);
}

int en_pad_index(const Engine *e, int note) {
    if (note < 0 || note > 127) return -1;
    if (e->map_set) {
        /* learned bank A; banks B-D are the same layout 16, 32 and 48 notes higher */
        for (int b = 0; b < 4; b++) {
            int n = note - 16 * b;
            if (n >= 0 && e->pad_map[n] >= 0) return 16 * b + e->pad_map[n];
        }
        return -1;
    }
    int idx = note - e->pad_base;
    return (idx >= 0 && idx < VC_PADS) ? idx : -1;
}

/* Humanize: each hit's velocity wanders by up to 20% at full setting, from a seeded generator so a take can be repeated */
static float humanize_vel(Engine *e, float v) {
    if (e->humanize <= 0.f) return v;
    e->hum_rng = e->hum_rng * 1664525u + 1013904223u;
    float r = (float)(e->hum_rng >> 8) / 8388608.f - 1.f;   /* -1..1 */
    v *= 1.f + e->humanize * 0.2f * r;
    return v < 0.05f ? 0.05f : v > 1.f ? 1.f : v;
}

static void pad_on(Engine *e, int note, int vel, int frame) {
    e->last_note_in = note;
    e->events_changed = 1;
    if (e->learn) {               /* Learn pad 1: a simple offset layout */
        e->pad_base = note > 112 ? 112 : note;
        e->learn = 0;
        e->map_set = 0;
        memset(e->pad_map, -1, sizeof(e->pad_map));
    }
    if (e->learn_all > 0) {       /* Learn all 16 pads: whatever note each physical pad sends */
        if (e->pad_map[note] >= 0) return; /* a pad already learned in this round: the next slot needs a different pad */
        int k = e->learn_all - 1;
        for (int n = 0; n < 128; n++) if (e->pad_map[n] == k) e->pad_map[n] = -1;
        e->pad_map[note] = (signed char)k;
        if (++e->learn_all > 16) { e->learn_all = 0; e->map_set = 1; }
        e->last_pad = k;
        return;                   /* learning taps don't play */
    }
    if (e->shift_keys) { /* black keys in the octave below the pads: queued for the host thread */
        int a = en_shift_action(note);
        if (a >= 0) { e->shift_pending[a]++; return; }
    }
    int idx = en_pad_index(e, note);
    if (idx < 0) return;
    if (e->hold && note == e->latched_note && en_smart_index(e, idx) < 0) { /* Hold: same pad again = off */
        OwnerRec *lr = find_owner(e, e->pad_owner[note]);
        e->latched_note = -1;
        if (lr) release_owner(e, lr, frame);
        e->pad_owner[note] = 0;
        e->last_pad = idx;
        return;
    }
    if (e->pad_owner[note]) release_owner(e, find_owner(e, e->pad_owner[note]), frame);
    e->last_pad = idx;
    if (e->edit_follow && e->lp.mode == VC_MODE_CUSTOM) e->edit_pad_request = idx;
    int si = en_smart_index(e, idx);
    if (si >= 0) { /* Smart Scale pad: one note that fits the current chord */
        OwnerRec *r = alloc_owner(e, frame);
        if (++e->next_owner <= 0) e->next_owner = 1;
        r->owner = e->next_owner;
        r->held = 1;
        r->n = 1;
        r->notes[0] = e->smart_notes[si];
        int hn = harmony_note(e, r->notes[0]);
        if (hn >= 0 && hn != r->notes[0]) { r->notes[1] = (unsigned char)hn; r->n = 2; } /* the harmony voice shares the owner, so it is released with it */
        memset(r->sounding, 0, sizeof(r->sounding));
        r->vel = r->vel_base = humanize_vel(e, en_vel_curve(e->vel_curve, vel));
        e->pad_owner[note] = r->owner;
        for (int s = 0; s < r->n; s++) note_on_slot(e, r, s, frame);
        return;
    }
    const Voicing *v = &e->voicings[idx];
    if (!e->pads[idx].valid || v->n == 0) return;
    if (e->hold) { /* latch: another pad replaces the held chord; the same pad again turns it off */
        if (e->latched_note >= 0) {
            int ln = e->latched_note;
            e->latched_note = -1;
            OwnerRec *lr = find_owner(e, e->pad_owner[ln]);
            if (lr) release_owner(e, lr, frame);
            e->pad_owner[ln] = 0;
            if (ln == note) return;
        }
    }
    if (e->smart != SMART_FOLLOW) { /* this chord now drives the smart pads, here and in following instances */
        chord_from_pad(&e->own_chord, &e->pads[idx]);
        e->own_chord.flats = engine_flats(e); /* followers spell notes the way this chord is written */
        g_vc_shared.chord = e->own_chord;
        g_vc_shared.source = e->instance_id;
        g_vc_shared.version++;
        en_smart_update(e);
    }

    OwnerRec *r = alloc_owner(e, frame);
    if (++e->next_owner <= 0) e->next_owner = 1;
    r->owner = e->next_owner;
    r->held = 1;
    r->sustained = 0;
    r->n = v->n;
    memcpy(r->notes, v->notes, (size_t)v->n);
    memset(r->sounding, 0, sizeof(r->sounding));
    r->vel = humanize_vel(e, en_vel_curve(e->vel_curve, vel));
    e->pad_owner[note] = r->owner;

    r->vel_base = r->vel;
    r->root = e->pads[idx].spec.root % 12;
    r->base_n = v->n;
    memcpy(r->base_notes, v->notes, (size_t)v->n);
    if (e->hold) e->latched_note = note;
    if (e->mel_mode != MEL_OFF) mel_start(e, r, frame);
    if (e->pattern_on || e->repeat != REP_OFF) {
        if (e->pat_owner) { OwnerRec *old = find_owner(e, e->pat_owner); if (old) pat_release_notes(e, old, frame); }
        e->pat_is_repeat = !e->pattern_on;   /* a pattern wins over Repeat */
        e->pat_owner = r->owner;
        e->pat_off_at = 0;
        pat_start(e, frame);
        return;
    }
    if (en_arp_on(e)) { /* every lane that is on starts over with this chord */
        for (int k = 0; k < EN_ARP_LANES; k++) {
            ArpLane *l = &e->lanes[k];
            if (l->order == ARP_OFF) continue;
            OwnerRec *old = find_owner(e, l->owner);
            if (old) arp_silence(e, l, old, frame);
            l->owner = r->owner;
            l->cnt = -1;
            l->dirn = 1;
            l->next = e->now + frame;
        }
        return;
    }
    int dir = e->strum_dir;
    if (dir == STRUM_ALT) { dir = e->strum_flip ? STRUM_DOWN : STRUM_UP; e->strum_flip ^= 1; }
    double step = e->strum_ms * 0.001 * e->sr;
    for (int i = 0; i < r->n; i++) {
        int slot = dir == STRUM_DOWN ? r->n - 1 - i : i;
        int64_t delay = (int64_t)(step * i + 0.5);
        if (delay <= 0) note_on_slot(e, r, slot, frame);
        else if (e->nqueue < EN_QUEUE) {
            QEvent *q = &e->queue[e->nqueue++];
            q->when = e->now + frame + delay;
            q->owner = r->owner;
            q->slot = (unsigned char)slot;
            q->vel = 0;
        }
    }
}

static void handle_midi(Engine *e, const MidiMsg *m, int frame) {
    int st = m->d[0] & 0xF0;
    if (st == 0x90 && m->d[2] > 0) { e->det_pcs |= 1u << ((m->d[1] & 127) % 12); pad_on(e, m->d[1] & 127, m->d[2], frame); }
    else if (st == 0x80 || st == 0x90) pad_off(e, m->d[1] & 127, frame);
    else if (st == 0xB0) {
        int cc = m->d[1], val = m->d[2];
        if (cc == 64) {
            int on = val >= 64;
            if (e->sustain && !on) {
                e->sustain = 0;
                for (int i = 0; i < EN_OWNERS; i++)
                    if (e->owners[i].owner && e->owners[i].sustained && !e->owners[i].held) release_owner(e, &e->owners[i], frame);
            }
            e->sustain = on;
        } else if (cc == 120 || cc == 123) {
            /* All sound off / all notes off: the MPC sends these when a recording starts, and acting on them cut the
             * chord that was ringing. Each pad still releases its own chord on note-off, and a stop or reload resets all. */
        } else if (cc == 121) {
            if (e->sustain) {
                e->sustain = 0;
                for (int i = 0; i < EN_OWNERS; i++)
                    if (e->owners[i].owner && e->owners[i].sustained && !e->owners[i].held) release_owner(e, &e->owners[i], frame);
            }
        }
    }
}

void en_push_midi(Engine *e, int frame, unsigned char s, unsigned char d1, unsigned char d2) {
    if (e->nin >= EN_MIDI_IN) return;
    MidiMsg *m = &e->in[e->nin++];
    m->frame = frame < 0 ? 0 : frame;
    m->d[0] = s; m->d[1] = d1; m->d[2] = d2;
}

static double arp_beats(int rate) {
    switch (rate) {
    case RATE_4: return 1.0;
    case RATE_8: return 0.5;
    case RATE_8T: return 1.0 / 3.0;
    case RATE_16: return 0.25;
    case RATE_16T: return 1.0 / 6.0;
    default: return 0.125;
    }
}

/* position of the k-th note when converging on the outside notes: 0, len-1, 1, len-2, ... */
static int arp_converge(int k, int len) {
    return (k & 1) ? len - 1 - k / 2 : k / 2;
}

/* one step of one lane. The sequence covers every held note, repeated once per octave of the lane */
static void arp_step(Engine *e, int k, int frame) {
    ArpLane *l = &e->lanes[k];
    OwnerRec *r = find_owner(e, l->owner);
    if (!r || r->n == 0) { l->owner = 0; l->note = -1; return; }
    arp_silence(e, l, r, frame);
    int n = r->n, len = n * l->oct, j;
    switch (l->order) {
    case ARP_DOWN:
        l->cnt = (l->cnt + 1) % len;
        j = len - 1 - l->cnt;
        break;
    case ARP_UPDOWN: { /* up, then down without repeating the top and bottom notes */
        int period = len > 1 ? 2 * len - 2 : 1;
        l->cnt = (l->cnt + 1) % period;
        j = l->cnt < len ? l->cnt : period - l->cnt;
        break;
    }
    case ARP_CONVERGE:
        l->cnt = (l->cnt + 1) % len;
        j = arp_converge(l->cnt, len);
        break;
    case ARP_DIVERGE:
        l->cnt = (l->cnt + 1) % len;
        j = len - 1 - arp_converge(l->cnt, len);
        break;
    case ARP_PLAYED: /* random */
        e->rng = e->rng * 1664525u + 1013904223u;
        j = (int)((e->rng >> 16) % (unsigned)len);
        break;
    default:
        l->cnt = (l->cnt + 1) % len;
        j = l->cnt;
        break;
    }
    int note = r->notes[j % n] + 12 * (j / n) + l->transpose;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    note_on_raw(e, r, note, frame);
    l->note = note;
    double tempo = e->tempo > 20.0 && e->tempo < 400.0 ? e->tempo : 90.0;
    int64_t step = (int64_t)(e->sr * 60.0 / tempo * arp_beats(l->rate) + 0.5);
    if (step < 16) step = 16;
    l->next += step;
    if (l->next <= e->now + frame) l->next = e->now + frame + step;
}

static void sort_in(Engine *e) {
    for (int i = 1; i < e->nin; i++) {
        MidiMsg v = e->in[i];
        int j = i - 1;
        while (j >= 0 && e->in[j].frame > v.frame) { e->in[j + 1] = e->in[j]; j--; }
        e->in[j + 1] = v;
    }
}

/* ---------- progression play ---------- */
/* floor and ceil without libm: the plugin links no maths library (scripts/check_so.sh refuses one) */
static double vc_floor(double x) { int64_t i = (int64_t)x; if ((double)i > x) i--; return (double)i; }
static double vc_ceil(double x) { int64_t i = (int64_t)x; if ((double)i < x) i++; return (double)i; }

#define PROG_BEATS_PER_BAR 4

static double prog_tempo(const Engine *e) { return e->tempo > 20.0 && e->tempo < 400.0 ? e->tempo : 90.0; }

/* host beats at a frame of the current block */
static double prog_beats(const Engine *e, int frame) { return e->ppq + frame / e->sr * prog_tempo(e) / 60.0; }

static void prog_stop(Engine *e, int frame) {
    OwnerRec *r = e->prog_owner ? find_owner(e, e->prog_owner) : 0;
    if (r) release_owner(e, r, frame);
    e->prog_owner = 0;
    e->prog_bar = -1;
}

/* the chord for this bar: the progression's step (bar modulo its length), voiced like the pads */
static void prog_start(Engine *e, int bar, int frame) {
    prog_stop(e, frame);
    e->prog_bar = bar;
    int n = vc_prog_length(&e->lp);
    if (n < 1) return;
    ChordSpec c;
    if (!vc_prog_step(&e->lp, bar % n, &c)) return;
    Voicing v;
    vc_voice(&c, e->lp.voicing, e->lp.inversion, e->lp.octave, 0, &v);
    if (v.n == 0) return;
    OwnerRec *r = alloc_owner(e, frame);
    if (++e->next_owner <= 0) e->next_owner = 1;
    r->owner = e->next_owner;
    r->held = 1;      /* sounds until the next bar or the host stops, not until a pad lets go */
    r->sustained = 0;
    r->n = v.n;
    memcpy(r->notes, v.notes, (size_t)v.n);
    memset(r->sounding, 0, sizeof(r->sounding));
    r->vel = r->vel_base = en_vel_curve(e->vel_curve, 100);
    r->root = c.root;
    r->base_n = v.n;
    memcpy(r->base_notes, v.notes, (size_t)v.n);
    for (int s = 0; s < r->n; s++) note_on_slot(e, r, s, frame);
    e->prog_owner = r->owner;
}

/* ---------- bassline ---------- */
static const char *BASS_PATTERN[BASS_COUNT] = {"----", "R---", "R-F-", "RORO", "RTFA"};

/* Phrase library: one bar of eighth notes (8 steps). Tokens: R root, F fifth, O octave, T third, A approach note
 * (a semitone below the next chord's root), - no new note (the last one rings on). */
const EnPhrase EN_PHRASES[EN_NUM_PHRASES] = {
    {"Root pulse", "RRRRRRRR", "Root on every eighth"},
    {"Root and fifth", "R-F-R-F-", "Root and fifth, steady"},
    {"Octave pump", "RORORORO", "Octaves, driving"},
    {"Syncopated", "R--R-R--", "Syncopated root, room to breathe"},
    {"Walking", "RTFTRTFA", "Walking line into the next chord"},
    {"Lift", "R-T-F-O-", "Chord tones rising"},
    {"Trap sub", "R-----R-", "Sub bass: long notes, sparse"},
    {"Hold", "R-------", "One long note on the root"},
    {"Soul", "R-TF-O-A", "Soul walk with an approach note"},
    {"Staccato", "R-R---R-", "Short stabs on the root"},
    {"Approach", "--R-A-R-", "Approach notes into the root"},
    {"Pedal", "R-R-O-R-", "Root pedal with octave colour"},
};

/* the bassline is on when a style or a phrase is chosen */
static int bass_on(const Engine *e) { return e->phrase > 0 || (e->bass_style > 0 && e->bass_style < BASS_COUNT); }
/* steps per beat: a phrase has eighth notes, a style has one note per beat */
static int bass_subdiv(const Engine *e) { return e->phrase > 0 ? 2 : 1; }

static void bass_release(Engine *e, int frame) {
    OwnerRec *r = e->bass_owner ? find_owner(e, e->bass_owner) : 0;
    if (r) release_owner(e, r, frame);
    e->bass_owner = 0;
}

static void bass_stop(Engine *e, int frame) {
    bass_release(e, frame);
    e->bass_beat = -1;
}

/* a new bass note for this beat (or hold the last one on a '-'); the chord is the progression step for the bar */
static void bass_beat_start(Engine *e, int beat, int frame) {
    e->bass_beat = beat;
    if (!bass_on(e)) return;
    int n = vc_prog_length(&e->lp);
    if (n < 1) return;
    int per_bar = 4 * bass_subdiv(e);
    int bar = beat / per_bar, step = beat % per_bar;
    char tok = e->phrase > 0 ? EN_PHRASES[e->phrase - 1].steps[step] : BASS_PATTERN[e->bass_style][step];
    if (tok == '-') return;
    ChordSpec c, next;
    if (!vc_prog_step(&e->lp, bar % n, &c)) return;
    if (!vc_prog_step(&e->lp, (bar + 1) % n, &next)) next = c;
    int iv[VC_MAX_IV], m = vc_chord_intervals(&c, iv);
    int root = 36 + c.root, note = root;
    if (tok == 'F') note = root + 7;
    else if (tok == 'O') note = root + 12;
    else if (tok == 'T') note = root + (m > 1 ? iv[1] : 4);
    else if (tok == 'A') note = 36 + (next.root + 11) % 12; /* a semitone below the next root */
    if (note > 127) note -= 12;
    bass_release(e, frame);
    OwnerRec *r = alloc_owner(e, frame);
    if (++e->next_owner <= 0) e->next_owner = 1;
    r->owner = e->next_owner;
    r->held = 1;
    r->sustained = 0;
    r->n = 1;
    r->notes[0] = (unsigned char)note;
    memset(r->sounding, 0, sizeof(r->sounding));
    r->vel = r->vel_base = en_vel_curve(e->vel_curve, 100);
    r->root = c.root;
    r->base_n = 1;
    r->base_notes[0] = (unsigned char)note;
    note_on_slot(e, r, 0, frame);
    e->bass_owner = r->owner;
}

/* frames from this point in the block to the next bass step */
static int bass_frames_to_beat(const Engine *e, int frame) {
    double b = prog_beats(e, frame);
    double sub = bass_subdiv(e);
    double next_beat = (vc_floor(b * sub + 1e-9) + 1) / sub;
    return (int)vc_ceil((next_beat - b) * 60.0 / prog_tempo(e) * e->sr - 1e-6);
}

/* frames from this point in the block to the next bar line */
static int prog_frames_to_bar(const Engine *e, int frame) {
    double b = prog_beats(e, frame);
    double next_bar = (vc_floor(b / PROG_BEATS_PER_BAR + 1e-9) + 1) * PROG_BEATS_PER_BAR;
    return (int)vc_ceil((next_bar - b) * 60.0 / prog_tempo(e) * e->sr - 1e-6);
}

void en_process(Engine *e, float *L, float *R, int n) {
    if (e->dirty) en_rebuild(e);
    memset(L, 0, sizeof(float) * (size_t)n);
    memset(R, 0, sizeof(float) * (size_t)n);
    if (e->midi_out_flush) { /* MIDI out switched off: silence anything we left hanging */
        for (int k = 0; k < 128; k++) if (e->out_ref[k] > 0) emit(e, 0, (unsigned char)(0x80 | (e->out_channel & 15)), (unsigned char)k, 0);
        e->midi_out_flush = 0;
    }
    if (!e->hold && e->latched_note >= 0) { /* Hold switched off: let go of the latched chord */
        int ln = e->latched_note; e->latched_note = -1;
        OwnerRec *lr = find_owner(e, e->pad_owner[ln]);
        if (lr) release_owner(e, lr, 0);
        e->pad_owner[ln] = 0;
    }
    if (e->mel_owner && e->mel_mode == MEL_OFF) mel_stop(e, 0);
    if (e->mel_owner && e->pat_sync && e->playing) mel_align(e, 0, 0);
    if (!e->pattern_on && e->repeat == REP_OFF && e->pat_owner) { /* pattern/repeat switched off while held: stop it */
        OwnerRec *r = find_owner(e, e->pat_owner);
        if (r) pat_release_notes(e, r, 0);
        e->pat_owner = 0;
    }
    if (e->pat_owner && e->pat_sync && e->playing) pat_align(e, 0, 0); /* follow the MPC's song position */
    if (e->prog_owner && (!e->prog_play || !e->playing || !e->lp.prog)) prog_stop(e, 0);
    if ((e->bass_owner || e->bass_beat >= 0) && (!bass_on(e) || !e->prog_play || !e->playing || !e->lp.prog)) bass_stop(e, 0);
    if (!e->playing && e->prog_bar >= 0) prog_stop(e, 0);
    if (e->smart == SMART_FOLLOW && g_vc_shared.version != e->shared_seen) { en_smart_update(e); e->events_changed = 1; }
    sort_in(e);
    int ii = 0, pos = 0;
    while (pos < n) {
        while (ii < e->nin && e->in[ii].frame <= pos) handle_midi(e, &e->in[ii++], pos);
        /* due strum notes */
        for (int q = 0; q < e->nqueue;) {
            if (e->queue[q].when <= e->now + pos) {
                QEvent ev = e->queue[q];
                e->queue[q] = e->queue[--e->nqueue];
                OwnerRec *r = find_owner(e, ev.owner);
                if (r) note_on_slot(e, r, ev.slot, pos);
            } else q++;
        }
        for (int k = 0; k < EN_ARP_LANES; k++) {
            ArpLane *l = &e->lanes[k];
            if (!l->owner) continue;
            if (l->order == ARP_OFF) { /* lane switched off while held */
                OwnerRec *r = find_owner(e, l->owner);
                if (r) arp_silence(e, l, r, pos);
                l->owner = 0;
            } else if (l->next <= e->now + pos) arp_step(e, k, pos);
        }
        if (e->pat_owner) {
            if (e->pat_off_at && e->pat_off_at <= e->now + pos) {
                OwnerRec *r = find_owner(e, e->pat_owner);
                if (r) pat_release_notes(e, r, pos);
                e->pat_off_at = 0;
            }
            while (e->pat_owner && e->pat_grid + pat_swing(e, e->pat_k) <= (double)(e->now + pos) + 0.5) {
                int64_t k = e->pat_k;
                e->pat_k++;
                e->pat_grid += pat_step_samples(e);
                if (k > e->pat_last_k) pat_do(e, k, pos);
            }
        }
        if (e->mel_owner) {
            if (e->mel_off_at && e->mel_off_at <= e->now + pos) {
                OwnerRec *m = find_owner(e, e->mel_owner);
                if (m) note_off_slot(e, m, 0, pos);
                e->mel_off_at = 0;
            }
            while (e->mel_owner && e->mel_grid <= (double)(e->now + pos) + 0.5) {
                int64_t k = e->mel_k;
                e->mel_k++;
                e->mel_grid += mel_step_samples(e);
                if (k > e->mel_last_k) mel_step(e, k, pos);
            }
        }

        if (e->prog_play && e->playing && e->lp.prog > 0) { /* a bar line reached at this frame starts its chord */
            int bar = (int)vc_floor((prog_beats(e, pos) + 1e-9) / PROG_BEATS_PER_BAR);
            if (bar != e->prog_bar) prog_start(e, bar, pos);
        }
        if (bass_on(e) && e->prog_play && e->playing && e->lp.prog > 0) { /* a bass step reached at this frame */
            int beat = (int)vc_floor(prog_beats(e, pos) * bass_subdiv(e) + 1e-9);
            if (beat >= 0 && beat != e->bass_beat) bass_beat_start(e, beat, pos);
        }
        int next = n;
        if (ii < e->nin && e->in[ii].frame < next) next = e->in[ii].frame;
        if (bass_on(e) && e->prog_play && e->playing && e->lp.prog > 0) { int d = pos + bass_frames_to_beat(e, pos); if (d < next) next = d; }
        if (e->prog_play && e->playing && e->lp.prog > 0) { int d = pos + prog_frames_to_bar(e, pos); if (d < next) next = d; }
        for (int q = 0; q < e->nqueue; q++) {
            int64_t d = e->queue[q].when - e->now;
            if (d < next) next = (int)d;
        }
        for (int k = 0; k < EN_ARP_LANES; k++) {
            if (!e->lanes[k].owner || e->lanes[k].order == ARP_OFF) continue;
            int64_t d = e->lanes[k].next - e->now;
            if (d < next) next = (int)d;
        }
        if (e->pat_owner) {
            double d = e->pat_grid + pat_swing(e, e->pat_k) - (double)e->now;
            int di = (int)(d + 0.999);
            if (di < next) next = di;
            if (e->pat_off_at) { int64_t o = e->pat_off_at - e->now; if (o < next) next = (int)o; }
        }
        if (e->mel_owner) {
            double d = e->mel_grid - (double)e->now;
            int di = (int)(d + 0.999);
            if (di < next) next = di;
            if (e->mel_off_at) { int64_t o = e->mel_off_at - e->now; if (o < next) next = (int)o; }
        }
        if (next <= pos) next = pos + 1;
        if (next > n) next = n;
        sy_render(&e->synth, L + pos, R + pos, next - pos);
        pos = next;
    }
    while (ii < e->nin) handle_midi(e, &e->in[ii++], n - 1); /* frames beyond the block */
    e->nin = 0;
    e->now += n;
}

int en_owner_count(const Engine *e) {
    int c = 0;
    for (int i = 0; i < EN_OWNERS; i++) c += e->owners[i].owner != 0;
    return c;
}
