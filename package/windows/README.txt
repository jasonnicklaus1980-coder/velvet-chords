Velvet Chords for Windows (64-bit VST2)

Install
1. Copy velvetchords.dll AND the skin folder into your VST2 plugin folder, for
   example C:\Program Files\Steinberg\VstPlugins. The skin folder must sit next
   to the DLL:
       VstPlugins\velvetchords.dll
       VstPlugins\skin\*.png
2. Rescan plugins in your DAW and load "Velvet Chords" as an instrument.
3. Open the plugin window. The six tabs along the bottom (Play, Perform, Edit,
   Pattern, Tools, Lanes) are the MPC skin's pages.

Using the window
- Left click a control to step it or set it. Right click steps it back.
- Segments set their value on click.
- Each bank pad shows its chord name and function (for example Cmaj7 and Imaj7).
- The preset arrows on the Play page step through the presets.
- Pads are not clickable yet.
- Controls with no picture are drawn as boxes with their name and value.

Tested
- Under Wine (Windows compatibility layer) in a VST2 test host: the DLL loads,
  the editor opens and takes clicks, notes make sound, a saved state restores,
  and the preset arrows step. The check runs in CI (make win-test).
- Not yet opened in a real Windows DAW.

Notes
- The virtual MIDI port (Velvet Chords Out) is not available on Windows.
- The window reads the skin's pictures with GDI+ (part of Windows).
