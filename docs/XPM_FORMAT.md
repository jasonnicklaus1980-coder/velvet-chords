# MPC keygroup (XPM) format: what the reference reader expects

Status: **taken from a third-party reader, not checked against an MPC-made file.** No `.xpm` file from an MPC has been
available to this project yet. Treat every name and range below as a claim to verify on the first real file.

Source: the `format/akai` package of ConvertWithMoss (redtide/ConvertWithMoss, LGPLv3), files `MPCKeygroupTag.java`,
`MPCKeygroupDetectorTask.java`, `ZonePlay.java`. These notes record the field names and conversions only. They don't copy the code.

## Shape of the file
An XML document. The root element is `MPCVObject`, with one `Program` child:

    MPCVObject
      Program  (attribute type = "Keygroup" or "Drum")
        ProgramName
        KeygroupNumKeygroups        (default 128)
        KeygroupPitchBendRange      (in octaves; 0 = none)
        Instruments
          Instrument  (attribute number = the keygroup / pad number)
            LowNote, HighNote       (MIDI notes; drums use the number instead)
            ZonePlay                (0 = Cycle, 1 = Velocity, 2 = Random)
            TriggerMode             (0 = One-shot, 1 = Release, 2 = Attack)
            OneShot                 (used only when TriggerMode is missing: "True")
            IgnoreBaseNote          ("True" or other)
            FilterType, Cutoff, Resonance, FilterEnvAmt, ...
            VolumeAttack, VolumeHold, VolumeDecay, VolumeSustain, VolumeRelease
            PitchAttack, PitchHold, PitchDecay, PitchSustain, PitchRelease, PitchEnvAmount
            Layers
              Layer  (one per velocity layer)
                VelStart, VelEnd          (velocity range)
                SampleName                (file name without ".WAV")
                Active                    ("True" or other: other = layer off)
                Volume                    (0..1, see gain)
                Pan                       (0..1, centre 0.5)
                Pitch                     (fine tune value) or TuneCoarse + TuneFine
                RootNote                  (see the off-by-one note)
                KeyTrack                  ("True" or other)
                SliceStart, SliceEnd      (sample start and end, in frames)
                SliceLoop, SliceLoopStart, SliceLoopCrossFadeLength, SliceTailPosition, SliceTailLength
                LoopStart, LoopEnd, LoopCrossfadeLength, LoopTune
    PadNoteMap (drum programs): PadNote entries with Note and number attributes

## Where the samples are
The sample for a layer is `<folder of the .xpm>/<SampleName>.WAV`. The reader in use looks for the upper-case `.WAV`
extension, so check the extension on the MPC's files (and on case-sensitive storage).

## Conversions
- **Gain:** `gain_dB = (Volume - 0.353) * 18 / (1.0 - 0.353) - 12`. Volume 0.353 is -12 dB; Volume 1.0 is +6 dB.
- **Pan:** `pan = Pan * 2 - 1` (clamped to -1..1).
- **Envelope times:** a normalised 0..1 value is scaled to 0..30 for attack, hold, decay and release, and sustain is
  scaled to 0..1. A negative value means "use the default".
- **Root note:** the file's `RootNote` is one more than the real root note (the reader subtracts 1).

## Things the reader ignores or doesn't check
Filter and envelope fields beyond the names above, the pad-to-note map for drums (read but not in detail), and
any element not listed here. The writer side (what an MPC needs to load a file) is not described here.

## To verify on the first real XPM
1. The root element and the `Program` type attribute.
2. The `RootNote` off-by-one.
3. The sample file extension (`.WAV` vs `.wav`) and its folder.
4. The volume and envelope scales, by loading a known file and comparing the values on the MPC's screen.
