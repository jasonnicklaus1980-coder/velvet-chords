# Hosted VST2 instruments

Velvet Chords can load a VST2 instrument from the MPC's plugin folder and play it with its chords.

## Using it
1. Add the instrument to the MPC's plugin list as you would any plugin (its `.so` file in `/sdcard/vst`).
2. Press **Scan plugins**. Velvet Chords reads the MPC's own plugin list (`MPC.settings`) and lists the VST2 instruments in it, in the same order the MPC shows them. Effects and Velvet Chords itself are not listed.
3. Pick the instrument with **Host plugin**; Host prev and Host next step through the list, and **None** unloads it.
4. Set **Host volume**. This level is on top of the built-in sound.
5. To hear only the hosted instrument, set **Sound** to **Silent (MIDI only)**. The chords and notes still go to it.

The built-in sound and the hosted instrument play together unless you silence the built-in one.

## Limits (read before relying on it)
- **Crashes:** a hosted plugin runs inside Velvet Chords. If it crashes, the MPC's plugin process can crash with it.
  Choosing an instrument loads it, so a bad plugin can crash on selection.
  Without an MPC settings file (on a computer), Scan falls back to listing the plugin folder.
- **Saved projects:** the project saves the Host plugin number, not the file name. After reopening a project, press
  **Scan plugins** first, so the number points at the same instrument.
- **Plugin state:** a hosted plugin's own settings and presets are not saved in the project yet.
- **Plugin windows:** a hosted plugin's own editor window is not shown. Its parameters are not on the Velvet Chords screen yet.
- **Keygroups:** MPC keygroup programs are not supported. A plugin has no way to load one, and their file format is not documented here.
- **MIDI out from the hosted plugin** is not routed anywhere.
- **Tested here:** the hosting code runs against a test instrument on the computer and under ARM emulation.
  Six real MPC plugins (ARMv7 builds from this repo's history) were loaded under emulation with `tools/vhost_run.c`:
  all six load and report their names and parameter counts. Da Sample Lab is the only instrument among them and
  made no sound on A4 with no sample loaded. The other five are effects in their own metadata (audio in and out),
  so the Scan list leaves them out. Hosting effects is not built yet. None of this has run on an MPC.
