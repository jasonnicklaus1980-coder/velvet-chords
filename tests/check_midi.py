#!/usr/bin/env python3
"""Independent checks of exported files (no third-party libraries):
 - every .mid parses as a Standard MIDI File, note-ons and note-offs balance, nothing overlaps itself
 - chord markers line up with note groups
 - database.json MIDI notes equal the notes in the matching .mid file
 - every .progression is valid JSON with MIDI notes 0-127
"""
import json, os, struct, sys

fails = 0
def check(cond, msg):
    global fails
    if not cond:
        fails += 1
        print("FAIL", msg)

def vlq(b, i):
    v = 0
    while True:
        c = b[i]; i += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, i

def parse_smf(path):
    b = open(path, "rb").read()
    assert b[:4] == b"MThd", "header"
    hlen, fmt, ntrk, ppq = struct.unpack(">IHHH", b[4:14])
    i = 8 + hlen
    tracks = []
    for _ in range(ntrk):
        assert b[i:i + 4] == b"MTrk", "track chunk"
        tlen = struct.unpack(">I", b[i + 4:i + 8])[0]
        j, end = i + 8, i + 8 + tlen
        t, run, evs = 0, None, []
        while j < end:
            d, j = vlq(b, j)
            t += d
            st = b[j]
            if st == 0xFF:
                typ = b[j + 1]
                ln, k = vlq(b, j + 2)
                evs.append((t, "meta", typ, b[k:k + ln]))
                j = k + ln
                if typ == 0x2F:
                    break
            elif st & 0x80:
                run = st
                evs.append((t, "midi", st, b[j + 1], b[j + 2]))
                j += 3
            else:
                evs.append((t, "midi", run, b[j], b[j + 1]))
                j += 2
        assert evs and evs[-1][1] == "meta" and evs[-1][2] == 0x2F, "end of track"
        tracks.append(evs)
        i = end
    return fmt, ppq, tracks

def chords_from_track(evs):
    groups = {}
    active = {}
    for e in evs:
        if e[1] != "midi":
            continue
        t, st, note, vel = e[0], e[2] & 0xF0, e[3], e[4]
        if st == 0x90 and vel > 0:
            check(note not in active, "overlapping note %d at %d" % (note, t))
            active[note] = t
            groups.setdefault(t, []).append(note)
        elif st in (0x80, 0x90):
            check(note in active, "note-off without note-on %d" % note)
            active.pop(note, None)
    check(not active, "hanging notes %s" % active)
    return [sorted(groups[t]) for t in sorted(groups)]

def main(dirs):
    nmid = nprog = 0
    mids = {}
    for d in dirs:
        for root, _, files in os.walk(d):
            if os.sep + "Import" in root:  # inputs for the import test, not files we export
                continue
            for f in files:
                p = os.path.join(root, f)
                if f.endswith(".mid"):
                    try:
                        fmt, ppq, tracks = parse_smf(p)
                    except Exception as ex:
                        check(False, "%s: %s" % (p, ex)); continue
                    check(fmt == 1 and ppq == 480 and len(tracks) == 2, p + " format")
                    metas = [e for e in tracks[0] if e[1] == "meta"]
                    check(any(e[2] == 0x51 for e in metas), p + " tempo")
                    check(any(e[2] == 0x58 for e in metas), p + " time signature")
                    groups = chords_from_track(tracks[1])
                    markers = [e for e in tracks[1] if e[1] == "meta" and e[2] == 0x06]
                    check(len(markers) == len(groups), p + " one marker per chord (%d vs %d)" % (len(markers), len(groups)))
                    mids[os.path.splitext(f)[0]] = (groups, [m[3].decode() for m in markers])
                    nmid += 1
                elif f.endswith(".progression"):
                    try:
                        j = json.load(open(p))
                        ch = j["progression"]["chords"]
                        check(len(ch) > 0, p + " has chords")
                        for c in ch:
                            check(c["name"] and all(0 <= n <= 127 for n in c["notes"]) and len(c["notes"]) >= 2, p + " chord ok")
                    except Exception as ex:
                        check(False, "%s: %s" % (p, ex))
                    nprog += 1
    dbp = os.path.join(dirs[0], "database.json")
    ndb = 0
    if os.path.exists(dbp):
        db = json.load(open(dbp))
        for s in db["songs"]:
            for sec in s["sections"]:
                key = "%s - %s - %s" % (s["artist"], s["title"], sec["section_name"])
                key = "".join(c if (c.isalnum() or c in " -_.#") else "_" for c in key)
                check(key in mids, "MIDI file for " + key)
                if key not in mids:
                    continue
                groups, markers = mids[key]
                ev = sec["chord_events"]
                check(len(ev) == len(groups), key + " event count")
                for a, g, m in zip(ev, groups, markers):
                    check(sorted(a["midi_note_numbers"]) == g, "%s: %s database notes == MIDI notes" % (key, a["chord_symbol"]))
                    check(a["chord_symbol"] == m, "%s: marker %s == %s" % (key, m, a["chord_symbol"]))
                    ndb += 1
    print("checked %d MIDI files, %d .progression files, %d database chord events: %d failures" % (nmid, nprog, ndb, fails))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
