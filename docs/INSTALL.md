# Velvet Chords 1.0.0

A chord-pad instrument plugin for MPC OS. Each pad plays a full chord through a built-in electric piano, pad or organ sound, with song sections, a Smart Scale mode, an R&B chord kit and 64 editable custom pads.

It is a native MPC OS plugin (VST2 `.so`) loaded by MPC's built-in plugin host, installed the same way as community plugins such as the JV-880 port.

## What's new in 1.1

- **900 factory presets.** The presets are 90 genres with 10 variations each. The genres include trap, drill, phonk, G-Funk and Southern styles, plus house, soul, Latin, African, rock and more. The **Preset genre** and **Preset variation**
  knobs (Play page Q-Links) pick one: genre first, then the variation within it, so every preset is reachable by knob.
  **Preset** and Preset prev / next still step through all 900 in order. The top left of the Play page shows the preset in use, with prev / next beside it, and the MPC's own preset box lists Manual and all 900 presets, so you can pick one from there too. A preset sets the
  tone, voicing, strum, key, scale and Smart Scale pads. A preset never picks the instrument, the riff or the rhythm: the Sound, Sound extra, riff, pattern, its rate, swing and gate, and the arpeggio stay as you set them. (A preset's rhythm would play the chord as a run, so it isn't switched on.) It never changes your song or your custom pads; the layout
  pads follow the new key and scale.
  Manual changes nothing.
  Nothing turns on by itself: a preset never replaces **Silent** and never turns the arpeggio on. It sets the arpeggio's
  order, rate and octaves only when you already have an arpeggio running. A pattern you have chosen plays instead of the arpeggio.
  The presets use the built-in sounds Electric piano, Soft pad and Organ.
- **Multi arp: four arpeggiator lanes that run at the same time.** Lane 1 is **Arp**, **Arp rate**, **Arp octaves** and
  **Arp transpose**; lanes 2 to 4 are **Arp 2**, **Arp 3** and **Arp 4**, each with its own order, rate, octaves and
  transpose (-12 to +12). Orders: Up, Down, Up-down, Random, **Converge** (outside in) and **Diverge** (inside out).
- **Riffs.** **Riff** plays one of six riffs on the pattern grid (*Root and fifth*, *Octave bounce*, *Thirds run*,
  *Pentatonic lift*, *Syncopated fifths*, *Tonic pedal*). Each of the 16 **Riff step** values is a rest or an offset from
  the held chord's root, from -12 to +24 semitones. Pick the slot to edit with **Riff slot**. The riff follows every chord.
  (The existing **Melody** control is the Tools-page melody generator, not the riffs.)
- **Note-by-note chords.** **Edit note** picks *Whole chord* or *Note 2* to *Note 8* of the edited pad, and **Edit note
  interval** sets it from -12 to +24 semitones, or removes it. Changing Edit type, root or bass starts the chord again.
- **Shift keys** (**Shift keys** = On by default). The black keys in the octave below the pads act without playing a chord:
  C# moves the key down a semitone, D# up, F# to the previous scale, G# to the next scale, A# to the next inversion.
  If you learn pad 1 to a note in that octave, shift keys take those pads.
- **Lanes page** (the sixth page): the preset selector, shift keys, the riff, the four arp lanes, the riff slot and steps,
  and the note editor. The riff steps are on the Q-Links there.

Projects saved with 1.1 keep their riffs and note lists. Older projects load without them.

## Requirements

- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live, Live II, One, Key 61, Force). The installer refuses anything else.
- **Root shell access (SSH).** Stock MPC OS doesn't offer this; you need a modded unit.
- Installing plugins this way is unofficial and not supported by Akai. Back up first and use at your own risk.

## Install (scripted)

1. Unzip, then copy the whole folder to the device:
   `scp -r VelvetChords-1.0.0 root@<device-ip>:/tmp/`
2. Run the installer:
   `ssh root@<device-ip> sh /tmp/VelvetChords-1.0.0/install.sh`

The installer checks the device and file checksums, **stops MPC** (save your project first), copies the plugin to `/sdcard/vst/velvetchords.so`, the screen skin to `/sdcard/Synths/Velvet Chords - VST - Velvet Chords/`, copies the song files (MIDI, `.progression`, pad reference) to `Velvet Chords` on the internal drive, backs up `MPC.settings`, adds the plugin to MPC's plugin list and starts MPC again. Running it again upgrades in place. Add `-y` to skip the prompt.

## First use

1. Make a new plugin track and choose **Velvet Chords** from the plugin browser (instrument plugins).
2. Hit pad A01. If you hear nothing, open the plugin's parameters, turn **Learn pad 1** on, and hit the pad you want as pad 1. Pads 2-64 follow from there (16 per bank, banks A-D).
3. Pick a **Mode**: Chords, Types, Custom, Chrom notes, Scale notes or Chrom chords. Chords is the diatonic layout (the Scale in the Key and Scale fields); Chrom notes puts every semitone on a pad, Scale notes the scale's notes up the bank, and Chrom chords one chord root on every semitone. (Song mode has been removed from the screen; an old project saved in Song mode opens in Scale.)

## The screen

The plugin comes with its own MPC screen skin, six pages on the function keys:

- **Play**: mode buttons, song and section pickers with the song's data status, Smart Scale controls, 16 chord keys showing each pad's chord and function (the key you last played lights up; tap a key to hear it), the bank buttons A-D, a one-octave keyboard for the key with dots on the notes of the scale, the scale picker, and a readout of the exact notes playing.
- **Perform**: sound, tone, release, volume, velocity curve, strum, arpeggiator, voicing, inversion, octave, transpose, voice leading, MIDI out, pad 1 note with Learn, and Export MIDI.
- **Pattern**: rhythm presets, the 16-step grid, swing, note length and timing, plus MIDI output settings.
- **Edit**: tap a chord key to pick it, then set its root on the keyboard, its bass note, chord type, inversion and octave. Copy to Custom starts you from whatever layout you were playing.

The Q-Links follow the page. If the skin doesn't appear (for example `/sdcard/Synths` isn't in MPC's content locations; the installer warns about this), the plugin still works through MPC's generic parameter view.

## Using it like ChordPrism: chords and patterns into any instrument

Velvet Chords can work as a pure chord-and-pattern generator that plays **another** instrument, the way ChordPrism sits in front of a synth in a DAW. It sends its chords out as MIDI and stays silent itself.

1. **Track 1: Velvet Chords.** On the Pattern page set *Built-in sound* to **Silent** and *Send chords as MIDI* to **Virtual port**. Output is on channel 16 by default.
2. **Track 2: your instrument.** Any keygroup or plugin track: an MPC piano, an AIR synth, a Rhodes. In that track's MIDI input settings, choose the input port **Velvet Chords Out**, channel 16, with monitoring on so it plays while Track 1 is selected. (Menu names differ between MPC OS versions.)
3. Select Track 1 and play the pads. Each pad sends its chord, or its rhythm pattern, to Track 2.

The *MIDI port* readout on the Pattern page shows whether the port opened. If MPC doesn't list *Velvet Chords Out* as an input, save the project, then reload it or restart MPC with the plugin loaded so MPC rescans its MIDI ports.

While the port is on, the plugin ignores notes arriving on its own output channel, so a track set to "all inputs" can't loop chords back into it.

*To MPC* sends the same MIDI to MPC through the plugin interface instead. Whether MPC forwards that anywhere is untested; *Both* uses both routes.

## Smart Scale pads

Set **Smart Scale pads** (Play page) to **Pads 9-16** and the top two rows of every bank (pads 9-16) stop playing chords and play single notes that fit the chord you last played on the bottom two rows (pads 1-8). Play a chord with your left hand and a melody, bassline or riff with your right, and nothing clashes. The keys on screen show each note and what it is (root, 3rd, 5th, b7...), and they relabel the moment you change chords.

**Smart notes** chooses what those pads play:
- **Chord**: only the chord's notes, rising across two octaves.
- **Scale**: the chord's notes plus the rest of the key's scale , for melodies with passing notes.
- **Bass**: the chord's notes down in the bass register, starting on the slash-bass note for chords like Fmaj7/G.

*Smart octave* (a Q-Link on the Play page) moves them up or down.

**Smart map** chooses how the smart pads are laid out. **Follow mode** (the default) uses Smart notes and the chord above.
The other three ignore the chord, and the 16 pads walk up from the root:
- **Relative**: the key's scale from the key root (pad 1 is the key). Works in every key.
- **Absolute**: the scale from C, whatever the key is, so the notes don't move when you change key.
- **Chromatic**: all twelve semitones from the key, with no scale restriction.

Smart map is a separate control, so projects saved before it existed keep Follow mode.

**Progression** (Play page Q-Link) fills pads 1 onward with a progression in the current key: I-V-vi-IV, ii-V-I, i-VI-III-VII,
Andalusian, Axis, Pop I-IV, Neo-soul ii-V-I or 12-bar blues. Pad 1 plays step 1, pad 2 plays step 2, and so on. The steps are
written as Roman numerals, so the same progression works in every key. Off puts the layout back, and choosing a progression
replaces imported pads. Pads after the progression keep the layout. The Screen bank Q-Link was replaced; the bank buttons still work.

**Progression play** (Play page Q-Link) plays the progression by itself while the host plays: one chord per bar, starting on
each bar line, the previous chord released on the line, and nothing after the host stops. It assumes 4/4 and uses the host's
tempo and position. The Voice lead Q-Link was replaced to make room; Voice lead is still on the Perform page.


**Harmony** (Tools Q-Link) adds a second voice to every note the smart pads play, in the key's scale: **Third up**, **Sixth up**, **Third down**
or **Octave up**. It follows the key and scale you've chosen, so the extra note is always in key, and it is released with the note it
belongs to. It works on the smart pads only (Smart Scale pads on Pads 9-16 or Follow). The Release Q-Link on the Tools page was replaced
by Harmony; Release is still on the Perform page.

**Your presets** (Tools page, bottom right): pick a slot (1-4) and press **Save** to keep your current settings in it, or **Load** to bring them
back. A user preset holds the sound, tone, release and volume, voicing, inversion, octave and voice leading, strum, all four arpeggio lanes,
velocity, the pattern and its 16 steps, rate, swing, gate, length, chance and seed, the smart pads settings, the riff, the key and scale,
harmony, and the progression with Progression play. Unlike a factory preset it restores all of that, including Silent or an
arpeggio, because you saved it. Slots are stored with the project. It does not hold your custom pads, saved progressions or imported chords.

**Analyze a sample** (Edit page, bottom): put a WAV file in `Velvet Chords/Analyze/` (the installer and the first press of **Analyze sample** create the `Analyze` folder). The file can have any name: if there's no `sample.wav`, Velvet uses the first WAV in the folder. It reads 16-bit, 24-bit PCM and 32-bit float WAV (16-bit, 24-bit PCM or 32-bit float, mono or
stereo, up to 120 seconds is read). Pick the progression slot with the **Progression slot** Q-Link on the Tools page, then press
**Analyze sample**. Velvet estimates the chords, the key and how sure it is, and writes the chords into that slot in time order, one
per pad from pad 1. Choose the slot's progression on the Play page (**Progression**) to play them. The status line says what it found,
for example "5 chords in progression 1, key Am (72% sure)". A slot that already holds chords is never overwritten: press **Clear
progression** on the Edit page first. Results are estimates; the confidence is in the readout, and a wrong chord can be fixed by
editing the custom pad it came from (Copy layout to Custom, then the Edit page).

The analysis separates sustained harmony from drum hits, reads the bass note, and matches each stretch against the chord table
(triads, sevenths, ninths, sus, altered, and more). It has been tested on generated audio with drums; it has not been tested on
real recordings or on the MPC.

**Starred presets** (Tools page, bottom left): with a factory preset chosen, **Star preset** marks it (press again to un-star), and
**Prev star** and **Next star** jump to the previous or next starred preset, wrapping round. The readout shows whether the current preset is
starred and how many you have starred. Stars are stored with the project. **Clear** (Tools page, next to Export and Import) empties the
chosen user preset slot.

**Sharing your presets:** **Export file** writes the chosen slot to `user-preset-N.vcpreset` in the Velvet Chords folder (the same folder as
Export MIDI), and **Import file** reads that file back into the slot. Copy the file to another MPC or keep it as a backup. The file is
plain text, version 1: the first line is `VELVET-PRESET 1`, then lines starting with `#` are comments, and every other line is
`name=value`, where `name` is the setting's name as shown by the plugin (Sound, Tone, Swing, Step 1 ...) and `value` is the plugin's own
0 to 1 value for it. On import, a file without the header is refused, a file from a newer version is refused, names the plugin
doesn't know are ignored, settings the file doesn't mention keep their default, and values that aren't numbers between 0 and 1 are
skipped. The readout next to the buttons says what happened, and a refused file never changes the slot.

**Pattern length, chance and seed** (Pattern page, bottom row): **Pattern length** loops the pattern after 4, 8, 12 or 16 steps.
**Pattern chance** plays 25, 50, 75 or 100% of its hits. Which hits are skipped is set by **Pattern seed** (1-4): the same seed and
settings always play the same hits, so a pattern you like can be reproduced. Rests and ties are not affected by the chance.

**Your own progressions:** the 16 chords on bank A can be saved as one of four user progressions. Set **Progression slot** (Tools
Q-Link), then press **Save progression** (Perform page). The progression control then lists **User 1-4**, and recalling one puts
its chords on pads 1-16 in the order they were saved, as the same chords (not transposed). Chords with custom notes are
saved as empty steps, so their pads keep the layout. Saved slots are part of the project, so they reload with it.
The Strum Q-Link on the Tools page was replaced by Progression slot; Strum is still on the Perform page.

### Two instances: chords on one, bass or melody on the other

Load Velvet Chords on two tracks. On the second, set **Smart pads** to **Follow**: all of its pads become Smart notes that follow whatever chord the *first* instance is playing, live. Set the second to **Bass** for a bassline that always matches, or **Scale** for a lead. Give each its own sound, or route each to a different instrument with the virtual port and different output channels. Instances share the chord directly inside MPC, so no MIDI routing is needed between them.

## The Tools page

- **Hold:** tap a pad and its chord keeps playing. Another pad replaces it; the same pad again stops it.
- **Repeat:** the playing chord retriggers in time with the MPC (1/4 to 1/32). A Pattern, if one is chosen, takes priority.
- **Melody generator:** while a chord plays, a melody runs above it: chord notes on the strong beats, scale notes in between, moving mostly in steps. *Simple* leaves more space, *Busy* plays more notes. It follows the MPC's song position like patterns do.
- **Key and scale: follow another copy:** with two copies of Velvet Chords, turn this on in the second one and it takes the first one's key and scale whenever you change them there. (Smart pads → Follow makes it follow the chords too.)
- **MIDI import:** copy `.mid` files into the **Velvet Chords/Import** folder on the MPC's internal drive (the plugin creates it). Pick a file and press **Load chords**: notes that start together become a chord, each chord is named (slash chords from the lowest note), and they're laid out on the pads in order with the file's exact notes (up to 64 chords, banks A–D). Drum tracks are ignored. **Pads play → Layout** goes back to your normal pads; imported chords are saved with your project. Use **Copy to Custom** on the Edit page to change them.

## The Pattern page

Pick a rhythm and every pad you hold plays its chord in that rhythm instead of a single hit. There are 15 presets (Sustain, Quarters, Neo-soul push, Slow jam, Gospel drive, Funk 16ths, Bass and stabs, Reggae skank, Caribbean 3-3-2, Dancehall, Afrobeats, Half-time and more).

The grid shows the 16 steps. Tap a cell to set what a step does: **Accent** or **Chord** plays the whole chord (accent louder), **Upper** / **Upper acc** plays only the notes above the bass, **Bass** plays only the bass note, **Tie** holds the previous step, and **Rest** is silence. Editing a preset saves it as your Custom pattern. On this page the 16 Q-Links edit the 16 steps.

**Step length** sets the grid (1/8 to 1/32, with triplets), **Swing** pushes every second step late, and **Note length** shortens each hit. **Timing: Follow MPC** locks the pattern to the MPC's song position while the sequencer runs. **Free** starts the pattern from the moment you hit the pad. The pattern takes priority over the arpeggiator.

## Parameters

| Parameter | What it does |
|---|---|
| Mode | Chords, Types, Custom, Chrom notes, Scale notes, Chrom chords |
| Section | Section on bank A; banks B-D get the following sections |
| Key, Scale | Key and scale for Smart scale and Chord types (223 scales) |
| Transpose | Shifts the chords by -12 to +12 semitones |
| Voicing | Bass + upper (default), Close, Drop 2, Spread, Upper only |
| Inversion, Octave | Global chord inversion and register |
| Voice lead | Pads in a bank are voiced to move smoothly from pad to pad |
| Strum, Strum dir | Delay between chord notes (0-150 ms), up/down/alternate |
| Arp, Arp rate | Arpeggiator (up, down, up-down, random), synced to the MPC tempo |
| Velocity | Linear, soft, hard or fixed response |
| Sound, Tone, Release, Volume | Built-in sound: Electric piano, Rhodes, Wurlitzer, Bright Rhodes, Clavinet, Piano, Soft pad, Poly synth, Strings, Supersaw, Choir, Brass, Harp, Organ, Marimba, Vibraphone, Nylon guitar, Pluck, Music box, or *Silent* (MIDI out only). *Tone* brightens or darkens every sound |
| Sound extra | Four more sounds: FM bell, Synth bass, Glass pad, Saw lead. Picking a sound from the Instrument grid turns it off again |
| Reverb | Room reverb on the built-in sound. 25% by default; 0% is dry, the sound as it was before reverb |
| Humanize | How much each pad hit's velocity wanders, up to 20% at 100%. 0% plays every hit at the same velocity |
| Pad 1 note, Learn pad 1 | Which incoming note is pad A01 (pads 2-64 follow in order) |
| Learn all 16 pads | If the pads come out in the wrong order: press it, then hit MPC pads 1 to 16 in order, one hit each. Hitting a pad that is already learned does nothing, so press the next pad. Velvet Chords remembers each pad's note (banks B-D follow) and saves it with the project. Make sure the MPC's own PAD PERFORM is off on this track first, because it changes which notes the pads send |
| MIDI out | Off, To MPC (plugin MIDI output, routing untested), Virtual port (*Velvet Chords Out*), or both |
| MIDI channel | Output channel (default 16) |
| Pattern, Step length, Swing, Note length, Timing, Step 1-16 | Pattern generator (see above) |
| Smart pads, Smart notes, Smart octave | Smart Scale pads and instance following (see above) |
| Edit pad / root / type / bass / inversion / octave | Edit a custom pad. Editing switches Mode to Custom |
| Edit follows pad | In Custom mode, hitting a pad selects it for editing |
| Copy to Custom | Copies the current layout into the custom pads so you can tweak it |
| Export MIDI | Saves the current section (or bank) as `.mid` and `.progression` in the `Velvet Chords` folder |
| Now playing | Last pad: chord, function and exact notes |

Custom pads, edits and settings are saved inside the MPC project.

## Keep chord MIDI away from other tracks

When **MIDI out** is *To MPC* or *Both*, Velvet Chords sends its chords on **MIDI channel 16** (the MIDI channel setting on
the Pattern page). Any track set to **all inputs** receives that MIDI too, and a note-off on the same note can cut off a
drum or synth sound that is still ringing, so it can seem to mute that track.

- Leave **MIDI out** on *Off* unless you need the chords as MIDI.
- Set drum and synth tracks to a specific input or channel, not **all inputs**, if you use MIDI out.
- Or use *Virtual port* and send Velvet Chords to a track that you choose on purpose.

### MIDI log (for diagnosis)

On the Tools page, **MIDI log** writes every MIDI event Velvet Chords receives (`I`), sends (`O`) and gets from the host on
start or stop (`M`) to `Velvet Chords/MIDI log.txt`. Each line is the time in seconds, the direction, and the three bytes in hex.
Channel 16 on the port shows as `9F` (note on) and `8F` (note off). Turn it on, reproduce the problem, turn it off, and send the
file. The file keeps growing while the log is on, so turn it off when you are done.

Velvet Chords only sends note messages on its own channel, and every note-on has its note-off. The routing inside MPC
decides which tracks hear them.

## Parameters added in 1.1

| Parameter | What it does |
|---|---|
| Preset, Preset prev, Preset next | Manual, or one of the 900 factory presets |
| Preset genre, Preset variation | Manual or one of the 90 genres, then one of its 10 variations (Play page Q-Links) |
| Arp octaves, Arp 2 to Arp 4 (with rate, octaves, transpose) | The four arp lanes (see above) |
| Arp transpose | Transposes lane 1 by -12 to +12 semitones |
| Shift keys | On: the black keys in the octave below the pads move the key, scale and inversion |
| Riff, Riff slot, Riff step 1-16 | The riff (Off or 1 to 6), the slot shown, and its 16 steps (rest or -12 to +24 semitones) |
| Edit note, Edit note interval | Whole chord or Note 2 to Note 8 of the edited pad, and its interval (-12 to +24, or Remove) |

## Uninstall

`ssh root@<device-ip> sh /tmp/VelvetChords-1.0.0/uninstall.sh` removes the plugin, its screen skin and its plugin-list entry (it also stops and restarts MPC). Your `Velvet Chords` folder with exports is kept. Projects that used the plugin load without it.

## Install by hand

1. Copy `payload/vst/velvetchords.so` to `/sdcard/vst/velvetchords.so`, `payload/Synths/Velvet Chords - VST - Velvet Chords/` to `/sdcard/Synths/`, and `payload/data/` to a `Velvet Chords` folder on the internal drive.
2. Stop MPC: `systemctl stop acvs`
3. Back up `MPC.settings` (`/media/az01-internal/Settings/*/MPC.settings`).
4. In `MPC.settings`, inside `<VALUE name="pluginList-arm"><KNOWNPLUGINS>`, add the line from `plugin.xml`. If there is no `pluginList-arm` value, add this just before `</PROPERTIES>`:
   ```xml
   <VALUE name="pluginList-arm">
     <KNOWNPLUGINS>
       (the line from plugin.xml)
     </KNOWNPLUGINS>
   </VALUE>
   ```
5. Start MPC: `systemctl start acvs`. If MPC shows default settings, restore your backup (the XML was malformed).

## Using the song files without the plugin

`payload/data/midi` holds a MIDI file per song section, importable into any MPC sequence or DAW. `payload/data/progressions` holds Pad Perform `.progression` files. Their format follows commonly shared `.progression` files and has not been confirmed on a device, so compare one with a progression saved from your own MPC before relying on them.

## Song data (removed from the screen)

Song mode is no longer on the screen. The song data stays in the source for reference. None of the songs is marked verified. Every entry is labeled `unverified-analysis` with a confidence level: the chord functions are well known, but extensions, bar placement and exact keys still need checking against the recordings. Pad voicings are generated performance voicings, not transcriptions of the records. To correct or add songs, edit `data/songs.json` in the source and rebuild.
