# Velvet Chords

Chord-pad instrument plugin for first-generation (32-bit ARM) MPC OS devices, plus MIDI and `.progression` song files.
See `docs/INSTALL.md` for installing on the MPC and `docs/REPORT.md` for what is tested and what isn't.

## Getting the MPC version

GitHub builds it for you. Every push to `main` runs **Actions → Build MPC plugin**, which compiles the 32-bit ARM
plugin, runs all tests (including the plugin under ARM emulation) and produces `VelvetChords-<version>.zip`.

- Latest build: **Actions** tab → newest green run → **Artifacts** → *VelvetChords-MPC*.
- Releases: push a tag (`git tag v1.0.0 && git push origin v1.0.0`) and the zip is attached to a release on the **Releases** page.

Install it on the MPC with `docs/INSTALL.md`.

## Build the MPC version yourself (one command)

You need a computer with Docker (Windows, Mac or Linux). From this folder:

    sh scripts/build-arm.sh

This builds and tests on your computer, cross-compiles `velvetchords.so` for 32-bit ARM, runs the plugin test host on
that ARM build under the qemu emulator, checks the binary's library dependencies, and writes
`dist/VelvetChords-1.0.0.zip` ready to copy to the MPC.

Without Docker, on Debian or Ubuntu: `sudo apt install gcc-arm-linux-gnueabihf qemu-user make python3 zip`, then
`make && make arm-test && make release`. Prefer an older distribution (Debian 11) so the binary only needs old glibc
symbols; `scripts/check_so.sh` refuses builds that need anything newer than glibc 2.17.

## Layout

| Path | Contents |
|---|---|
| `src/chords.c` | chord types, symbol parsing, voicings, voice leading |
| `src/layouts.c` | scales, Smart Scale, chord types, song-section and custom pad layouts |
| `src/engine.c` | pad triggering, pattern generator, strum, arpeggiator, sustain, stuck-note protection, MIDI out |
| `src/synth.c` | built-in electric piano, pad and organ |
| `src/midiport.c` | virtual ALSA MIDI output port (*Velvet Chords Out*) |
| `src/export.c` | Standard MIDI File and `.progression` writers |
| `src/plugin.c` | VST2 interface: parameters, displays, project save/restore, export |
| `src/vst2_abi.h` | independently written VST2 binary-interface declarations (no Steinberg SDK) |
| `data/songs.json` | the song library, kept separate from code |
| `tools/gen_songs.py` | turns `songs.json` into `src/songs_gen.c` |
| `tools/export_cli.c` | writes all MIDI, `.progression`, `database.json` and `PAD_REFERENCE.md` |
| `tests/` | engine tests, a VST2 test host, and an independent MIDI/progression validator |
| `package/` | installer, uninstaller, plugin list entry |
| `package/skin/` | the MPC screen skin (generated; `make skin` rebuilds it, `docs/skin-preview` shows it) |
| `tools/make_skin.py`, `tools/preview_skin.py` | skin generator and offline preview renderer |

## Adding or correcting a song

Edit `data/songs.json`. Bars are separated by `|`; chords inside a bar split it evenly:

    "progression": "Am7 | C7 | Fmaj7 | Bm7b5 E7"

Set `status` honestly (`verified` only after checking the recording and a licensed source) and run `make`.
The tests reject unknown chord symbols, duplicate songs and partial bars.
