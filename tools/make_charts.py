#!/usr/bin/env python3
"""Write SONG_CHARTS.md and VERIFICATION_REPORT.md for the real (non-original) songs.

Reads data/songs.json (metadata, corrections, unresolved items) and the exporter's database.json (the exact MIDI
notes the plugin plays), so the charts always match what the pads and MIDI files play.
usage: make_charts.py <database.json> <out dir>
"""
import json, os, sys

NAMES_SHARP = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
NAMES_FLAT = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]
NAMES_MIXED = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def spelling(m):
    """same rule as the plugin (vc_prefer_flats): flat keys use flats, C major / A minor use the mixed set"""
    r = m["key_root"]
    pc = (NAMES_SHARP.index(r[0]) + (1 if r[1:2] == "#" else -1 if r[1:2] == "b" else 0)) % 12
    if (not m["minor"] and pc == 0) or (m["minor"] and pc == 9): return NAMES_MIXED
    flat = pc in ((2, 7, 0, 5, 10, 3, 8) if m["minor"] else (5, 10, 3, 8, 1))
    return NAMES_FLAT if flat else NAMES_SHARP


HEADERS = ["INTRO", "VERSE", "PRE-CHORUS", "CHORUS", "BRIDGE", "INSTRUMENTAL", "OUTRO"]


def simplify(sym):
    """triad reduction for quick performance: keep root and major/minor/dim/aug/sus colour, drop extensions and slash bass"""
    root = sym[0] + (sym[1] if len(sym) > 1 and sym[1] in "#b" else "")
    q = sym[len(root):].split("/")[0]
    if q.startswith("m7b5") or q.startswith("dim"): return root + "dim"
    if q.startswith("aug") or "#5" in q and not q.startswith("m"): return root + "aug"
    if q.startswith("m") and not q.startswith("maj"): return root + "m"
    if "sus4" in q or q == "sus": return root + "sus4"
    if "sus2" in q: return root + "sus2"
    return root


def header_for(section):
    n = section.lower()
    for h, keys in (("PRE-CHORUS", ["pre-chorus"]), ("CHORUS", ["chorus", "refrain", "hook"]), ("BRIDGE", ["bridge"]),
                    ("INTRO", ["intro"]), ("OUTRO", ["outro", "ending"]), ("INSTRUMENTAL", ["instrumental", "solo"])):
        if any(k in n for k in keys): return h
    return "VERSE"  # verse, main loop / cycle / vamp / groove


def main():
    db = json.load(open(sys.argv[1]))
    out = sys.argv[2]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    meta = {s["id"]: s for s in json.load(open(os.path.join(root, "data", "songs.json")))["songs"]}
    songs = [s for s in db["songs"] if meta[s["song_id"]]["status"] != "original"]

    c = ["# Velvet Chords: real-song chord charts", "",
         "**None of these is verified against the original recording.** They come from recalled common charts, "
         "updated with the user's reference corrections. Check each against the record before relying on it; "
         "unresolved points are listed per song and in VERIFICATION_REPORT.md.", "",
         "Notes use the MPC naming convention (MIDI 60 = C3). *Voicing* is the plugin's generated performance voicing "
         "(bass note + voice-led upper structure), not the voicing on the record. *Pad* is where the chord sits in Song "
         "mode (A01 = bank A, pad 1, bottom-left). Time signature 4/4 is assumed where not verified.", ""]
    for n, s in enumerate(songs, 1):
        m = meta[s["song_id"]]
        names = spelling(m)
        c += ["---", "", "## %d. %s" % (n, m["title"]), "",
              "| | |", "|---|---|",
              "| SONG | %s |" % m["title"], "| ARTIST | %s |" % m["artist"],
              "| RECORDING VERSION | %s |" % m.get("recording", ""), "| KEY | %s |" % m["key"],
              "| BPM | %s |" % ("%d" % m["bpm"] if m.get("bpm") else "Not verified"),
              "| TIME SIGNATURE | %s |" % (m["time_signature"] + " (assumed)" if m.get("time_signature") else "Not verified (4/4 grid used)"),
              "| VERIFICATION STATUS | UNVERIFIED (%s, confidence %s) |" % (m["status"], m["confidence"]), ""]
        covered = set()
        for sec in s["sections"]:
            h = header_for(sec["section_name"])
            covered.add(h)
            ev = sec["chord_events"]
            c += ["### %s: %s" % (h, sec["section_name"]), ""]
            c += ["**Full:** " + " · ".join(e["chord_symbol"] for e in ev), "",
                  "**Simplified (triads):** " + " · ".join(simplify(e["chord_symbol"]) for e in ev), "",
                  "| Bar | Beat | Chord | Root / bass | Chord notes | Beats | MIDI notes | Voicing | Pad |",
                  "|---|---|---|---|---|---|---|---|---|"]
            for e in ev:
                pcs = []
                for x in e["midi_note_numbers"]:
                    if x % 12 not in pcs: pcs.append(x % 12)
                rootpc = NAMES_SHARP.index(e["root_note"]) if e["root_note"] in NAMES_SHARP else NAMES_FLAT.index(e["root_note"])
                pcs.sort(key=lambda p: (p - rootpc) % 12)
                c.append("| %d | %g | %s | %s / %s | %s | %g | %s | %s | %s |" % (
                    e["bar_number"], e["beat_position"], e["chord_symbol"], e["root_note"], e["bass_note"],
                    " ".join(names[p] for p in pcs), e["duration_beats"], " ".join(str(x) for x in e["midi_note_numbers"]),
                    e["voicing_notes"], e["pad_number"]))
            c.append("")
        missing = [h for h in HEADERS if h not in covered]
        if missing:
            c += ["**Not transcribed (UNVERIFIED):** " + ", ".join(missing), ""]
    open(os.path.join(out, "SONG_CHARTS.md"), "w").write("\n".join(c) + "\n")

    r = ["# Verification report: 18 real songs", "",
         "**Verified against the original recording: 0 of 18.** Audio analysis and licensed transcriptions were not "
         "available, so no song is marked verified. Corrections below come from the user's reference list; everything "
         "listed as unresolved still needs checking by ear or against a trusted transcription.", "",
         "To verify a song: play along with the record, confirm key, chords, chord qualities, bar lengths and sections, "
         "then report it so its status can be changed to *verified* with the source noted.", ""]
    nc = sum(1 for s in songs if meta[s["song_id"]].get("corrections"))
    nu = sum(len(meta[s["song_id"]].get("unresolved", [])) for s in songs)
    r += ["Songs changed: %d · Unresolved items: %d" % (nc, nu), ""]
    for n, s in enumerate(songs, 1):
        m = meta[s["song_id"]]
        r += ["## %d. %s - %s (%d)" % (n, m["title"], m["artist"], m["year"]), "",
              "Status: **UNVERIFIED** (%s, confidence %s) · Key: %s" % (m["status"], m["confidence"], m["key"]), ""]
        if m.get("corrections"):
            r += ["Corrections made:"] + ["- " + x for x in m["corrections"]] + [""]
        else:
            r += ["Corrections made: none (no specific chord change in the reference, or it matched)", ""]
        r += ["Unresolved:"] + ["- " + x for x in m.get("unresolved", [])] + [""]
    open(os.path.join(out, "VERIFICATION_REPORT.md"), "w").write("\n".join(r) + "\n")
    print("charts: %d songs, %d corrected, %d unresolved items" % (len(songs), nc, nu))


if __name__ == "__main__":
    main()
