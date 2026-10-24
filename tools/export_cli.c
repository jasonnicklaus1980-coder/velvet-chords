/* vc_export: writes every song section as MIDI + .progression files, plus a JSON
 * database where each chord symbol sits next to the exact MIDI notes it plays.
 * Uses the same chord/voicing code as the plugin, so files and pads can't disagree. */
#include "../src/export.h"
#include "../src/layouts.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void mkdirs(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0775); *p = '/'; }
    mkdir(tmp, 0775);
}

static void jstr(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) { if (*s == '"' || *s == '\\') fputc('\\', f); fputc(*s, f); }
    fputc('"', f);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "out";
    char dir[512], path[700], name[200], fname[200], text[800];
    MfChord ch[256];
    int files = 0;
    LayoutParams lp;
    memset(&lp, 0, sizeof(lp));
    lp.voicing = VC_VOICE_BASS_UPPER;
    lp.voice_lead = 1;

    snprintf(path, sizeof(path), "%s/database.json", out);
    mkdirs(out);
    FILE *db = fopen(path, "w");
    if (!db) { perror(path); return 1; }
    fprintf(db, "{\n  \"generator\": \"Velvet Chords vc_export\",\n  \"note_numbering\": \"MIDI note numbers; names use MPC convention (60 = C3)\",\n"
                "  \"voicing\": \"performance_voicing = generated 'Bass + upper' voicing with voice leading; NOT a transcription of the original record\",\n  \"songs\": [\n");

    for (int si = 0; si < VC_NUM_SONGS; si++) {
        const Song *s = &VC_SONGS[si];
        lp.song = si;
        int orig = !strcmp(s->status, "original");
        char style[64] = "", *w = style; /* folder per style, e.g. originals-minneapolis-funk */
        for (const char *g = s->genre; *g && *g != '(' && w < style + 60; g++) *w++ = (*g == ' ' || *g == '/') ? '-' : (*g >= 'A' && *g <= 'Z' ? *g + 32 : *g);
        while (w > style && w[-1] == '-') w--;
        *w = 0;
        if (orig) snprintf(dir, sizeof(dir), "%s/midi/originals-%s", out, style);
        else snprintf(dir, sizeof(dir), "%s/midi/%ds", out, (s->year / 10) * 10);
        mkdirs(dir);
        char pdir[96];
        snprintf(pdir, sizeof(pdir), orig ? "originals-%s" : "songs", style);
        snprintf(path, sizeof(path), "%s/progressions/%s", out, pdir);
        mkdirs(path);
        fprintf(db, "    {\"song_id\": "); jstr(db, s->id);
        fprintf(db, ", \"title\": "); jstr(db, s->title);
        fprintf(db, ", \"artist\": "); jstr(db, s->artist);
        fprintf(db, ", \"writers\": "); jstr(db, s->writers);
        if (orig) fprintf(db, ", \"release_year\": null, \"decade\": null, \"style\": ");
        else fprintf(db, ", \"release_year\": %d, \"decade\": \"%ds\", \"style\": ", s->year, (s->year / 10) * 10);
        jstr(db, s->genre);
        fprintf(db, ", \"original_key\": "); jstr(db, s->key);
        if (s->bpm > 0) fprintf(db, ", \"tempo_bpm\": %.0f, \"time_signature\": ", s->bpm); else fprintf(db, ", \"tempo_bpm\": null, \"time_signature\": ");
        if (s->timesig) jstr(db, s->timesig); else fprintf(db, "null");
        fprintf(db, ", \"recording_version\": "); jstr(db, s->recording);
        fprintf(db, ", \"verification_status\": "); jstr(db, s->status);
        fprintf(db, ", \"confidence_level\": "); jstr(db, s->confidence);
        fprintf(db, ", \"analysis_notes\": "); jstr(db, s->notes);
        fprintf(db, ", \"source_reference\": %s", orig ? "\"Original composition by Velvet Chords; no source recording.\"" : "\"Recalled common charts and harmonic analysis; no licensed transcription consulted. Verify against the recording.\"");
        fprintf(db, ",\n     \"sections\": [\n");
        for (int sec = 0; sec < s->nsections; sec++) {
            lp.section = sec;
            int n = vc_section_chords(s, sec, &lp, ch, 256);
            snprintf(name, sizeof(name), "%s - %s - %s", s->artist, s->title, s->sections[sec].name);
            vc_safe_filename(name, fname, sizeof(fname));
            vc_provenance_text(s, &lp, text, sizeof(text));
            snprintf(path, sizeof(path), "%s/%s.mid", dir, fname);
            if (vc_midi_write(path, name, text, ch, n, s->bpm > 0 ? s->bpm : 90.0, s->ts_num, s->ts_den)) files++;
            /* .progression: one entry per pad (repeats collapsed), same as the plugin's song bank */
            static PadChord pads[VC_PADS];
            static Voicing v[VC_PADS];
            LayoutParams sp = lp;
            sp.mode = VC_MODE_SONG;
            vc_build_pads(&sp, 0, pads);
            vc_voice_pads(&sp, pads, v);
            MfChord pc[16];
            int pn = vc_bank_chords(pads, v, 0, pc, 16);
            snprintf(path, sizeof(path), "%s/progressions/%s/%s.progression", out, pdir, fname);
            if (vc_progression_write(path, name, vc_pc_name(s->key_root, vc_song_flats(s, 0)), pc, pn)) files++;

            fprintf(db, "       {\"section_name\": "); jstr(db, s->sections[sec].name);
            fprintf(db, ", \"chord_events\": [\n");
            int padno = 0;
            const char *lastn = 0;
            for (int i = 0; i < n; i++) {
                if (!lastn || strcmp(lastn, ch[i].name)) padno++;
                lastn = ch[i].name;
                ChordSpec c;
                vc_chord_parse(ch[i].name, &c);
                char nn[8];
                fprintf(db, "         {\"bar_number\": %d, \"beat_position\": %.2f, \"duration_beats\": %.2f, \"chord_symbol\": ",
                        (int)(ch[i].start_beats / (s->ts_num * 4.0f / s->ts_den)) + 1,
                        ch[i].start_beats - (int)(ch[i].start_beats / (s->ts_num * 4.0f / s->ts_den)) * (s->ts_num * 4.0f / s->ts_den) + 1.f,
                        ch[i].dur_beats);
                jstr(db, ch[i].name);
                fprintf(db, ", \"root_note\": \"%s\", \"bass_note\": \"%s\", \"midi_note_numbers\": [", vc_pc_name(c.root, vc_song_flats(s, 0)),
                        vc_pc_name(c.bass >= 0 ? c.bass : c.root, vc_song_flats(s, 0)));
                for (int j = 0; j < ch[i].n; j++) fprintf(db, "%s%d", j ? ", " : "", ch[i].notes[j]);
                fprintf(db, "], \"voicing_notes\": \"");
                for (int j = 0; j < ch[i].n; j++) { vc_note_name(ch[i].notes[j], vc_song_flats(s, 0), nn, sizeof(nn)); fprintf(db, "%s%s", j ? " " : "", nn); }
                fprintf(db, "\", \"bass_in_pad_chord\": true, \"pad_number\": \"A%02d\"}%s\n", padno, i + 1 < n ? "," : "");
            }
            fprintf(db, "       ]}%s\n", sec + 1 < s->nsections ? "," : "");
        }
        fprintf(db, "     ]}%s\n", si + 1 < VC_NUM_SONGS ? "," : "");
    }
    fprintf(db, "  ]\n}\n");
    fclose(db);

    /* pad reference: which pad plays which chord (default settings: Bass + upper voicing, voice leading on) */
    snprintf(path, sizeof(path), "%s/PAD_REFERENCE.md", out);
    FILE *pr = fopen(path, "w");
    if (pr) {
        fprintf(pr, "# Pad reference\n\nPads A01-A16 = MIDI notes 36-51 by default (bank B 52-67, C 68-83, D 84-99); use *Learn pad 1* if your pads send other notes. "
                    "Note names use the MPC convention (60 = C3). Voicings are generated performance voicings, not transcriptions of the records.\n\n## Songs\n");
        for (int si = 0; si < VC_NUM_SONGS; si++) {
            const Song *s = &VC_SONGS[si];
            static PadChord pads[VC_PADS];
            static Voicing v[VC_PADS];
            LayoutParams sp = lp;
            sp.mode = VC_MODE_SONG; sp.song = si; sp.section = 0;
            vc_build_pads(&sp, 0, pads);
            vc_voice_pads(&sp, pads, v);
            if (!strcmp(s->status, "original"))
                fprintf(pr, "\n### %s (original, %s)\n%s, %.0f BPM\n\n| Pad | Section | Chord | Function | Notes |\n|---|---|---|---|---|\n",
                        s->title, s->genre, s->key, s->bpm);
            else
                fprintf(pr, "\n### %s - %s (%d)\n%s, confidence %s, key %s\n\n| Pad | Section | Chord | Function | Notes |\n|---|---|---|---|---|\n",
                        s->title, s->artist, s->year, s->status, s->confidence, s->key);
            for (int p = 0; p < VC_PADS; p++) {
                if (!pads[p].valid) continue;
                int sec = vc_song_section_index(&sp, p / 16);
                fprintf(pr, "| %c%02d | %s | %s | %s | ", 'A' + p / 16, p % 16 + 1, s->sections[sec].name, pads[p].name, pads[p].roman);
                for (int j = 0; j < v[p].n; j++) { char nn[8]; vc_note_name(v[p].notes[j], vc_song_flats(s, 0), nn, sizeof(nn)); fprintf(pr, "%s%s", j ? " " : "", nn); }
                fprintf(pr, " |\n");
            }
        }
        fclose(pr);
    }
    printf("wrote %d files + database.json to %s\n", files, out);
    return 0;
}
