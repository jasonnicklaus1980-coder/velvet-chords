Velvet Chords for macOS (Intel, VST2)

Install
1. Copy "Velvet Chords.vst" into one of these folders:
     /Library/Audio/Plug-Ins/VST/           (all users)
     ~/Library/Audio/Plug-Ins/VST/          (just you)
2. Rescan plugins in your DAW and load "Velvet Chords" as an instrument.

If macOS says the plugin can't be opened because it is from an unidentified
developer, run this in Terminal (adjust the path to where you copied it):
     xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST/Velvet Chords.vst"

Notes
- Intel Macs only (x86_64). Apple Silicon Macs need Rosetta 2 for this build.
- The plugin has no window yet on macOS. Its parameters show in the DAW's
  generic parameter view.
- The virtual MIDI port (Velvet Chords Out) is not available on macOS yet.
- Built with zig (x86_64-macos) and clang. Not yet loaded in a Mac DAW.
