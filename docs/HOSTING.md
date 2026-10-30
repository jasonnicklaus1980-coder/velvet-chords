# Hosted VST2 instruments

Velvet Chords can load a VST2 instrument from the MPC's plugin folder and play it with its chords.

## Using it
1. Put the instrument's `.so` file in `/sdcard/vst` (the same place as other plugins).
2. Press **Scan plugins**. Velvet Chords lists the instruments it finds (VST2 instruments only; effects and Velvet Chords itself are not listed).
3. Pick the instrument with **Host plugin** (1 to 16). **None** unloads it.
4. Set **Host volume**. This level is on top of the built-in sound.
5. To hear only the hosted instrument, set **Sound** to **Silent (MIDI only)**. The chords and notes still go to it.

The built-in sound and the hosted instrument play together unless you silence the built-in one.

## Limits (read before relying on it)
- **Crashes:** a hosted plugin runs inside Velvet Chords. If it crashes, the MPC's plugin process can crash with it.
  Scanning loads every plugin in the folder once, so a bad plugin can crash during a scan.
- **Saved projects:** the project saves the Host plugin number, not the file name. After reopening a project, press
  **Scan plugins** first, so the number points at the same instrument.
- **Plugin state:** a hosted plugin's own settings and presets are not saved in the project yet.
- **Plugin windows:** a hosted plugin's own editor window is not shown. Its parameters are not on the Velvet Chords screen yet.
- **Keygroups:** MPC keygroup programs are not supported. A plugin has no way to load one, and their file format is not documented here.
- **MIDI out from the hosted plugin** is not routed anywhere.
- **Tested here:** the hosting code runs against a test instrument on the computer and under ARM emulation. It has not been
  tested with a real plugin on an MPC.
