DREADNOUGHT PLUGIN (beta)
=========================

The FM-1 firmware's instruments, sequencer and effects as a plugin: REESE, SUB, FM, WAVE, JUNO, SAMPLER (two),
BREAKS (two) and MIDI tracks, eight tracks, FX A and FX B. The sound is made at 44.1 kHz (as on the FM-1) and
resampled to your project's rate.

INSTALL
-------
macOS (Apple silicon and Intel):
  DREADNOUGHT.component  ->  ~/Library/Audio/Plug-Ins/Components/      (Logic, GarageBand, Ableton, ...)
  DREADNOUGHT.vst3       ->  ~/Library/Audio/Plug-Ins/VST3/            (Ableton, Bitwig, Reaper, Cubase, ...)
  DREADNOUGHT.app        ->  Applications (runs on its own, no DAW)
  The files are not signed by Apple yet. After copying them, open Terminal and run once:
      xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/DREADNOUGHT.component
      xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/DREADNOUGHT.vst3
      xattr -dr com.apple.quarantine /Applications/DREADNOUGHT.app
  then restart the DAW (Logic: it may need a rescan in Settings > Plug-in Manager).

Windows:
  DREADNOUGHT.vst3  ->  C:\Program Files\Common Files\VST3\
  DREADNOUGHT.exe   ->  anywhere (runs on its own)

Linux:
  DREADNOUGHT.vst3  ->  ~/.vst3/
  DREADNOUGHT       ->  anywhere (runs on its own)

THE WINDOW
----------
- Left: the eight tracks. Click one to edit it; CHANGE picks its instrument; M mutes it.
- SOUND: every value of the track's instrument. Drag a knob up / down (shift: finer), scroll it, or double-click it
  to type a value; a box with a word in it is a list: click it. FM shows its six operators as one table; BREAKS shows
  its loop: click to hear a slice, drag a line to move it, double-click for a new slice, right-click a line to take
  it out, AUTO SLICES to go back. SAMPLER shows its pads (or zones): click one to hear it and edit it.
- PATTERN: the track's 8 patterns (* has notes; picked while playing: it comes in next), its length, the swing.
  Click in the grid for a note; drag its end for its length, drag it for its pitch; double- or right-click: gone.
  The lane below is velocity. Click a note to see all of it below: slide, accent, condition, chord or hit FX, retrig.
- EFFECTS: every track's sends, the two returns, FX A and FX B (pick the effect, then its values).
- SAMPLES: drop WAV / AIFF / FLAC / MP3 files anywhere on the plugin (or ADD SAMPLES...). They take the rate and the
  mono fold picked at the top, and fit in the FM-1's 224 KB, so what you make here goes on the FM-1 too. BREAKS plays
  them as loops (BR1, BR2 ...), SAMPLER on its pads and zones. HEAR, rename, REMOVE.
- Top: the instrument's presets (< > or click the name), KEEP AS USER (REESE, SUB: your own sounds), BPM, PLAY,
  FOLLOW THE DAW (on: the sequencer starts and stops with the DAW, at its tempo), WEB EDITOR.
- The window scales: drag its corner. The button under DREADNOUGHT picks the skin: BLOCKS (the FM-1's own) or LIGHT.

THE DAW
-------
- MIDI channels 1-8 play tracks 1-8; channels 9-16 play the track selected. A BREAKS track (and SAMPLER's pads)
  takes C1 (note 36) as its first slice, C#1 the second, and so on.
- MIDI tracks send their notes out of the plugin: route them to another instrument.
- Each instrument's main values are the DAW's parameters (automation), whichever track the instrument is on.
- The project (sounds, patterns, song, samples) is saved with the DAW's project.

THE WEB EDITOR (macOS, Linux)
-----------------------------
The plugin opens a MIDI port called "DREADNOUGHT Plugin" and the web editor talks to it as it talks to an FM-1:
press WEB EDITOR (or go to twistedgenetics.github.io/DREADNOUGHT/editor in Chrome or Edge) and Connect. There you can
also load DX7 banks and Serum wavetables, and save or load whole projects (.dnproj): the same files as the FM-1's,
so a project moves between the FM-1 and the DAW both ways.

FOR NOW
-------
- One DREADNOUGHT per project (a second one says so and stays quiet).
- No web editor port on Windows (Windows can't make one without an extra driver).
- The sequencer starts from its bar 1 when the DAW plays: start the DAW on a bar.

Source, licence (GPL-3.0; JUCE under AGPL-3.0): https://github.com/TwistedGenetics/DREADNOUGHT
