DREADNOUGHT PLUGIN (beta)
=========================

The FM-1 firmware's app and engines, as an instrument plugin: the same screen, pages, sounds, sequencer and
effects, played from the computer. The sound is made at 44.1 kHz (as on the FM-1) and resampled to your project's
rate.

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

PLAYING IT
----------
- The panel is the FM-1's: the encoders SELECT, PRESETS and ALGORITHM and OCT on the left, the knobs K1-K4 and
  the two rows of buttons on the right, the 27 keys below. Drag a knob up / down or scroll it (shift: 4 at a time).
- Shift-click (or right-click) a button to hold it down, for the combinations: SAVE + FX, HOME + SELECT, SEQ + a key.
  Click it again to let go.
- The computer's keys play the keys when the plugin window has focus: Z S X D C V ... (F3 up) and Q 2 W 3 E ...
- MIDI from the DAW: channels 1-8 play tracks 1-8; channels 9-16 play the track the screen shows. A BREAKS track
  (and SAMPLER's pads) takes C1 (note 36) as its first slice, C#1 the second, and so on.
- FOLLOW THE HOST (on by default): the sequencer starts and stops with the DAW, at the DAW's tempo (rounded to a
  whole BPM, 60-200). Off: PLAY on the panel starts it, at its own tempo.
- MIDI tracks send their notes to the DAW (route the plugin's MIDI out to another instrument).
- The project (sounds, patterns, song, settings) is saved with the DAW's project: the plugin does SAVE for you
  when the DAW saves.

YOUR SAMPLES, THE WEB EDITOR (macOS, Linux)
-------------------------------------------
The plugin opens a MIDI port called "DREADNOUGHT Plugin", and the web editor talks to it as it talks to an FM-1:
  1. Open the plugin (in the DAW, or the standalone app).
  2. Press OPEN THE WEB EDITOR (or go to twistedgenetics.github.io/DREADNOUGHT/editor in Chrome or Edge).
  3. Connect. The editor finds "DREADNOUGHT Plugin" (if it picks another port first, choose it in Diagnostics).
     The plugin shows WEB EDITOR: CONNECTED.
Then everything the editor does works on the plugin: drop WAV / MP3 / AIFF files on the Project tab and send them
(they play on BREAKS and SAMPLER), set slices on the breaks page, load DX7 banks and Serum wavetables, edit every
sound, the patterns and the song, and save or load whole projects (.dnproj): the same files as the FM-1's, so a
project moves between the FM-1 and the DAW both ways. The samples are kept in the DAW's project with the rest.
The sample memory is the FM-1's (224 KB), so what you make in the plugin fits on the FM-1.
On Windows there is no port yet (Windows can't make one without an extra driver).

FOR NOW
-------
- One DREADNOUGHT per project (a second one says so and stays quiet).
- A new plugin starts with the two built-in loops (ROLLER, STEPPER) in its first sample slot: send your own over them.
- The sequencer starts from its bar 1 when the DAW plays: start the DAW on a bar.

Source, licence (GPL-3.0; JUCE under AGPL-3.0): https://github.com/TwistedGenetics/DREADNOUGHT
