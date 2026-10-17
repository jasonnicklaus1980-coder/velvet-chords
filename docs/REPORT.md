# Velvet Chords 1.0.0: verification report

## Integration method

MPC OS on first-generation devices runs a JUCE-based plugin host that loads VST2 shared libraries listed under
`pluginList-arm` in `MPC.settings`. This was learned from a working community plugin package (JV-880) supplied by the
user, not from Akai documentation. Velvet Chords uses the same route: a 32-bit ARM hard-float VST2 `.so` in `/sdcard/vst`.
It is an **instrument** plugin: MPC pads send notes to it, and it plays chords with its own sound engine. That avoids
depending on whether MPC routes MIDI *out* of plugins, which is unknown.

## What was tested, and where

| Area | How it was tested | Result |
|---|---|---|
| Chord types (40) | Each checked against an independent spelled-note table, plus aliases | Pass |
| Voicings | 192,000 combinations: every type x 12 roots x slash basses x 5 styles x 4 inversions x 5 octaves; pitch classes, bass note, order, range | Pass |
| Transposition | Every type in all 12 keys keeps its interval shape; transposed song pads keep quality | Pass |
| Voice leading | Top voice of a voice-led progression moves no more than a 4th | Pass |
| Smart Scale | Diatonic chords for C major and A harmonic minor checked by name; all 17 scales x 12 keys stay in scale | Pass |
| Song data | All chord symbols parse, whole bars only, no duplicate ids, none claims `verified` | Pass |
| Note handling | 4,000 random blocks of overlapping notes, sustain, strum, arp, layout changes; then all off | No held chords, no stuck voices, MIDI out on/off balanced |
| Retrigger, sustain | Double note-on with one note-off; pedal hold and release | Pass |
| Timing | Strum gaps exact to the sample (2,400 at 50 ms); arp steps exact (6,000 samples at 120 BPM 1/16) | Pass |
| Plugin binary interface | Test host loads the `.so` like a VST2 host: 33 parameters, displays, all 15 songs sound, rapid triggering, Learn, edits, project save/restore, bad-data rejection, Export | 433 checks pass |
| Exported files | 67 MIDI and 67 `.progression` files parsed by an independent validator; database notes equal MIDI notes | Pass |
| Pitch | Demo render analysed by FFT: Dbmaj7 sounds C#, F, G#, C within 0.03 semitone | Pass |
| Pattern generator | Free and MPC-synced timing exact to the sample: step spacing, ties, gate, swing (7,500/4,500-sample alternation at 50%), accents, bass-only and upper-only steps, late presses waiting for the next step; 3,000 random blocks of presses, rate/swing/sync/pattern changes with balanced MIDI and nothing stuck; all 15 presets valid | Pass |
| MIDI output | Through the plugin interface: Silent sound makes no audio while patterns send MIDI to the host; output channel; feedback guard ignores the plugin's own channel; editing a preset step converts it to Custom | Pass |
| Virtual MIDI port | Against a recording stand-in for the ALSA library: creates client *Velvet Chords* with a readable, subscribable port *Velvet Chords Out*; events go directly to subscribers on the right channel, in the 28-byte ALSA event layout; every note-on gets a note-off, even when an instance is closed mid-chord; the port closes when switched off. With the real ALSA library (no MIDI kernel here) it fails gracefully and reports why | Pass (stand-in only) |
| Smart Scale | Through the plugin interface: pads 9-16 follow each new chord (Dbmaj7 then C7), show note names and degrees, play single notes; Scale mode merges the song key's scale (C7 in F minor gives C Db Eb E F G Ab Bb); a second instance in Follow + Bass mode turns its pads into an Fm7 bassline the moment the first plays Fm7, with the first instance's spelling; smart pads switched on and off during the random stress test with nothing stuck | Pass |
| Screen skin | Format copied from the working JV-880 skin (TUI.json pages, component definitions, Q-Links files). Every binding resolves to a real parameter, all 217 images exist, regenerating gives identical files, and all three pages render correctly in an offline preview driven by the plugin's live parameter text | Pass (offline) |
| Screen helpers | Through the plugin interface: on-screen chord labels follow mode, key and bank; scale dots follow key and scale; prev/next buttons step and reset; tapping a key auditions it and stops by itself; playing a pad moves the screen to its bank and lights it; helpers can't be automated | Pass |
| Installer | Install, upgrade, uninstall, damaged-file detection, settings with and without an existing plugin list, on a simulated MPC filesystem with stubbed system commands | Pass |

All of the above ran on x86_64 Linux. **Nothing has run on ARM or on an MPC yet.** `scripts/build-arm.sh` runs the
same test host on the ARM build under the qemu emulator; that is the next level of confidence, still not hardware.

## Must be tested on the MPC

1. The plugin appears in the plugin browser and loads without crashing MPC.
2. How parameters display if the skin isn't found, and which ones the Q-Links reach.
3. Which notes the pads send on a plugin track (Learn pad 1 handles any answer).
4. Latency and CPU use with long chords, strum and arp at your project's buffer size.
5. Project save and reload keeps custom pads.
6. Export MIDI writes to the internal drive's `Velvet Chords` folder and the files show up in the MPC browser.
7. Whether MPC passes the plugin's MIDI out to other tracks or external MIDI (default off).
8. Whether MPC accepts the `.progression` files (compare with one saved from your MPC).
9. **The virtual port: the key test for ChordPrism-style use.** That MPC lists *Velvet Chords Out* as a MIDI input (possibly only after reloading the project or restarting MPC), that a second track set to that input plays the chords, and the latency of that route. Also whether *To MPC* (plugin MIDI output) is forwarded anywhere.
10. Smart Scale with two instances on two tracks: that the second follows the first live (they share the chord inside the MPC program, which assumes MPC loads both from the same plugin file, as it should).
11. Pattern timing against the MPC sequencer with Follow MPC, including swing and tempo changes.
12. The skin: that MPC finds it under `/sdcard/Synths` by its folder name, that live text uses the MPC's Titillium Web font at the sizes chosen, that segment buttons set the right values, that the screen repaints when you hit a pad (the plugin asks the host to refresh), and that double-tapping a value field opens MPC's knob overlay.

## Song library status

15 songs, 17 sections, 1961-2013. **Verified: 0.** All are `unverified-analysis`: recalled common charts and harmonic
analysis, with no licensed transcription consulted. Confidence: high 8, medium 6, low 1. Partial entries are noted in each
song's notes (for example, My Girl's later modulations and Superstition's chorus are not included). No lyrics or
notation are included.

The 500-song goal is not met and shouldn't be met by guessing. Each new song needs checking against the recording
before its status can be raised.

## Voicings

Every pad and MIDI voicing is a **generated performance voicing** (default: bass note plus a voice-led upper
structure). None is claimed to be the voicing on the original record. Chord symbols and notes are stored together in
`database.json` so the name and the sound can't disagree.

## Known limitations

- The skin format was learned from one community skin, not from Akai documentation. It uses only building blocks that skin uses (images, buttons, value labels, overlays), no knobs or popups.
- One pad = one chord; songs with more than 16 chord changes in a section are cut to 16 pads (MIDI export keeps the full section).
- Pad display names on the MPC pads themselves aren't possible from a plugin; the *Now playing* parameter shows the chord.
- Tempo and time signature for songs are placeholders (90 BPM, 4/4) because none is verified; host tempo is used for exports.


## 1.1 additions (merged into the 1.0 build)

| Check | Where | Result |
|---|---|---|
| Presets: 200, unique names, values in range; applied through the plugin interface | `tests/test_engine.c`, `tests/test_host.c` | pass |
| Multi arp: lanes at different rates and orders, lane off, release, new chord restarts every lane; orders checked against computed sequences | `tests/test_engine.c` | pass |
| Riffs: transposed to the chord root, rests, chord restored after a riff; state round-trip | `tests/test_engine.c`, `tests/test_host.c` | pass |
| Shift keys: the octave-below black keys queue the action and do not play pads; C# moves the key through the plugin | `tests/test_engine.c`, `tests/test_host.c` | pass |
| Note-by-note chords: naming, custom intervals, and chord restore on the edit path | `tests/test_engine.c`, `tests/test_host.c` | pass |
| MIDI import on hand-written files: running status, velocity-0 offs, meta events, two tracks, held pedal | `tests/test_engine.c`, `tests/midi/` | pass |
| MIDI import on corrupt files (fuzzing 4,000 files under AddressSanitizer): no crashes | run once, two inputs kept as regression tests | pass |
| Chord MIDI out stays on its channel; every note-on has its note-off | `tests/test_engine.c` | pass |
| Sanitizers (AddressSanitizer, UndefinedBehaviorSanitizer) on the engine and host suites | run once | clean |
| 32-bit ARM build | `make arm` | builds; the glibc gate refuses it for `dlopen`, `dlsym`, `__nanosleep64`, `__clock_gettime64` (the same four as the 1.0 build on this toolchain) |
| Skin: six pages, binding and image check | `make test` | pass (166 parameters bound) |
| Not run | — | ARM tests under emulation (no qemu here), and anything on an MPC |
