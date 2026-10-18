# Velvet Chords: status, verification and what is left

This is the feature-completion matrix, the known limitations, and the verification results for the 1.1 work, written so someone can
pick it up without the conversation that produced it. "Tested" means an automated test in `make test` passes. "Not verified" means
no test or device run covers it. Nothing here has been run on an MPC.

**Scope decision:** every item marked *Not built* below has been dropped. It is not planned, and nothing in the repository depends on it.

## How it is built and tested

- `make test` builds natively and runs: the engine tests (`tests/test_engine.c`, about 2.8 million checks), the plugin host tests
  (`tests/test_host.c`, loads the `.so` like a VST2 host, about 970 checks, with an allocation probe preloaded), the MIDI export
  checks, and the skin checks.
- `make arm` builds the 32-bit ARM plugin for MPC OS (needs an ARM cross compiler). The GitHub workflow `build-from-zip.yml` builds
  it with Zig against glibc 2.17 from `velvet-chords-repo.zip` and runs `scripts/check_so.sh` (no libm, no too-new glibc symbols).
- ARM tests under emulation (`make arm-test`) were not run here, and nothing was run on an MPC.

## Feature matrix (against the Chord Prism style development brief)

| Phase | Item | State |
|---|---|---|
| 2 chords | 41 chord qualities (triads, sus, 6, add, 7ths, 9/11/13, altered, quartal), slash chords, custom note lists | Tested: every quality, root, slash bass, voicing, inversion and octave |
| 2 chords | Voicings (bass+upper, close, drop 2, spread, upper only), inversions, octave, voice leading | Tested |
| 3 progressions | Roman numeral progressions in any key, 8 templates, fill pads 1.., transposes with the key | Tested |
| 3 progressions | Four saved user progressions, stored with the project | Tested (recall, empty steps, save and reload) |
| 3 progressions | Drag-and-drop reordering, a step-by-step progression editor | Not built |
| 3 progressions | Chord detection from a WAV file (STFT chroma, median harmonic/percussive separation, bass tracking, chord templates, key estimate) into a progression slot | Tested on generated audio with drums only. Not tested on real recordings. Analyze button on the Edit page |
| 3 progressions | Recording audio, sample region selection, preview, trim and loop, waveform and chord timeline views | Not built (the plugin has no audio input, and the MPC screen has no waveform view yet) |
| 3 progressions | Sample slice mode (pads trigger the original audio) | Not built |
| 3 progressions | Tempo estimate | Not built |
| 4 Smart Scale | Chord tones, scale, bass modes; Relative, Absolute, Chromatic maps; all twelve keys | Tested |
| 4 Smart Scale | User-defined scales | Not built |
| 5 harmonic context | Key, scale, progression position shared by progression play and bass | Tested. Cross-instance key sync exists (the Key sync switch) but is not tested here |
| 6 melody/pattern | Seeded, repeatable pattern chance, pattern length 4/8/12/16, riffs, melody generator | Tested |
| 6 melody/pattern | Humanization, step density controls, piano-roll view | Not built |
| 7 arpeggiator | Four lanes, orders, octaves, rates, transpose | Tested (timing, order, stuck notes) |
| 8 sequencer | 16-step pattern, ties, accents, swing, gate, chord-triggered patterns, host sync | Tested |
| 8 sequencer | Pattern chaining, per-step probability and velocity, genre sequencer templates | Not built (chance is one value for the whole pattern) |
| 9 bass/harmony | Bassline (root, root + fifth, octaves, walking) following the progression on the beat grid | Tested |
| 9 bass/harmony | Harmony voice on smart-pad notes (third up/down, sixth up, octave) in the key's scale | Tested in all twelve keys |
| 10 MIDI | MIDI file export of a pad bank; MIDI import with chord naming; host MIDI out and a virtual port | Tested against files and a fake ALSA; **not verified on an MPC** |
| 10 MIDI | Recording generated MIDI, drag-and-drop export | Not built, and not claimed |
| 11 presets | 530 factory presets (53 genres x 10 variations) setting sound, voicing, strum, pattern, smart pads, key and scale | Tested. A preset never replaces Silent or turns the arpeggio on |
| 11 presets | Four user presets, saved with the project; export to and import from a versioned text file | Tested (see `docs/INSTALL.md` for the file format) |
| 11 presets | Starred factory presets (star, previous and next starred), clearing a user preset slot | Tested |
| 11 presets | Search, rename | Not built (the MPC screen has no text entry, so rename would need a different design) |
| 12 UI | Six pages, Roman numerals on chord tiles, Progression, Bassline, Harmony, pattern and preset controls | Skin checked and previewed (`docs/skin-preview/`). **Not seen on an MPC** |
| 13 real time | No heap allocation on the audio path | Tested with an allocation probe: 0 calls in about 12,000 blocks |
| 13 real time | No stuck notes under random play | Tested (random pads, sustain, patterns, arpeggio, progression, bass, transport) |
| 14 testing | Sanitizers (AddressSanitizer, UBSan) on the engine and host suites | Clean on the final code (commit `b2424f1`). Leak checking was off for the host suite, because the test never closes its plugin instances |

## Known limitations

- Nothing has run on an MPC, so pad layout, screen readability, Q-Link mapping, and timing against the MPC's own bar lines are unconfirmed.
- Progression play and the bassline assume 4/4 and use the host's tempo and position.
- Imported MIDI chord naming uses chords struck together, so heavily arpeggiated files can come out wrong.
- `setParameter` can open or close the virtual MIDI port and read the Import folder; if a host calls it on the audio thread those can block.
- The muting bug (drums stop when Velvet Chords plays chords) was not reproduced. See the test below.
- The song-pad display after loading a song can stay stale until the next audio block. Not fixed.
- Some Q-Links were replaced to make room. Play page: Screen bank, Transpose, Voice lead and Smart octave. Tools page: Strum, Tone and Release. Those controls are still on other pages (bank buttons, Perform, Tools).

## Checklist for the first run on an MPC (about 20 minutes)

Install with the steps in `docs/INSTALL.md`, then:

1. Load the plugin on a track and play pads 1-16: you should hear chords, and the Play page tiles show chord names with Roman numerals.
2. Turn the **Preset genre** and **Preset variation** Q-Links: the preset name changes and the sound changes. Set Sound to Silent, change preset: it stays Silent.
3. Set **Progression** to I-V-vi-IV: pads 1-4 become four chords. Change Key: they follow the key.
4. Turn on **Progression play** and **Bassline**, and play the sequence: one chord per bar on the bar line, bass notes on the beats. Stop the sequence: everything goes silent. **Report if the chords start early or late against the MPC grid.**
5. Set Smart pads to Pads 9-16 and **Harmony** to Third up: each smart pad plays two notes.
6. On the Pattern page set length 4, chance 50%, a seed: the pattern loops after 4 steps and skips some hits, the same ones every time.
7. **Save progression** (Perform page), then **Save** a user preset (Tools page); change things, **Load** it back; **Export file** and check `user-preset-N.vcpreset` appears in the Velvet Chords folder.
8. The muting check: put drums on one track and Velvet Chords on another, record or play drums while holding chords. Note the MIDI out setting, the MIDI channel, whether the track input is "all inputs", and the MPC OS version, and report whether the drums stop.

## Dropped (decided not to build)

- Drag-and-drop reordering and a step-by-step progression editor.
- User-defined scales.
- Humanization, step-density controls and a piano-roll view.
- Pattern chaining, per-step probability and velocity, and genre sequencer templates.
- Preset search, and naming user presets.
- MIDI recording and drag-and-drop export.
- A full UI pass beyond the pages that exist.

What remains useful is the device check above: run the checklist on an MPC and report what it shows.
