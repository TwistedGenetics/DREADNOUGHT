# How a Reese is made — research notes for the REESE engine

What producers and tutorials agree on, the physics behind it, and what that means for DREADNOUGHT's engine.
Sources are listed at the bottom. Dogs on Acid threads were the first choice but couldn't be read from here
(the site blocks automated access); their titles alone show the topics producers keep coming back to: phase /
flange / tear, layering, mixing, resampling chains and the Noisia / techstep sounds.

## Where it comes from

- **Kevin Saunderson as Reese, "Just Want Another Chance" (1988).** He made it on a Casio CZ (the CZ-1000 or
  CZ-5000; he has said both, and they share the same engine), chasing a dark, deep Paradise Garage feel.
- **On the CZ, the Reese is built in.** Its LINE SELECT has a mode called **1+1'**: line 1 mixed with a detuned
  copy of itself. Detune goes in steps of 1/60 semitone (about 1.67 cents). That one switch *is* the classic
  two-oscillator Reese.
- **Jungle took it from the record.** Renegade (Ray Keith) "Terrorist" (1994) built its bassline from a single
  sampled note of that bass, played across a sampler's keyboard. That sampled, pitched character is part of the
  jungle Reese.
- **Techstep and neuro (Ed Rush & Optical, Matrix, Bad Company, later Noisia)** pushed it through distortion,
  filters, flangers and phasers, then resampled it again and again.

## The physics: why two detuned saws sound like that

Two saws at *f* and *f · 2^(c/1200)* drift in and out of phase. Each harmonic *n* beats at *n* times the
fundamental's rate, so the cancellation moves *up the spectrum* over time. Summing two detuned saws is a
**moving comb filter**: the phasing, flanging, "tearing" movement is the detune itself, before any effect.

How fast it moves depends on the note when the detune is in cents:

| Root | 10 cents | 30 cents | 60 cents | 100 cents |
|---|---|---|---|---|
| E1 41 Hz | 0.24 Hz | 0.72 Hz | 1.45 Hz | 2.45 Hz |
| A1 55 Hz | 0.32 Hz | 0.96 Hz | 1.94 Hz | 3.27 Hz |
| E2 82 Hz | 0.48 Hz | 1.44 Hz | 2.91 Hz | 4.90 Hz |

So a Reese moves faster on higher notes. Some producers want that; others want the same movement on every
note, which needs a fixed difference in Hz rather than cents (or the beat locked to the tempo).

The same cancellation makes the **level and the low end pump**: when the two fundamentals are opposed, the
bottom drops out. That's why every modern recipe splits the low end off.

## What the sources agree on

**Oscillators**
- Two saws is the core; more (unison) thickens it. Saw + square or saw + pulse adds grit (techstep, CZ-style).
  Detuned sines give the soft, round original-style Reese.
- Mono, legato, with glide. Pitch bends and slides are "half the sound".

**Detune** (total spread between the two)
- Mellow / classic jungle: about 10–30 cents. Common default: ±15 to ±30 cents (30–60 total).
- CZ-style recreations: about 60 cents. Neuro: 50–100 cents.

**Low end**
- Keep the sub a clean **mono sine**, tuned to the note. Split at **80–120 Hz**: high-pass the Reese layer,
  low-pass the sub. Nothing stereo below about 100–150 Hz.
- Distort only above the sub (multiband): the bottom clean, the mids and tops dirty.

**Filter and movement**
- Low-pass, often 24 dB, around **650 Hz–2 kHz**, modest resonance (10–40 %).
- **Notch or band-pass sweeps** are the most-cited extra movement ("very very effective"); a flanger or comb
  filter is the same idea with feedback. Slow LFOs (½–4 bars) or tempo-synced ones (1/8, 1/16 for neuro).
- Key-tracking the filter keeps the tone even across the line.

**Distortion**
- Several gentle stages beat one heavy one (three at 6 dB drive sound cleaner than one at 20 dB).
- Detune and sweep *before* distortion: heavy distortion first smears the movement away.
- Common chain: oscillators → (sweep) → saturation / distortion → filters → chorus / flanger / phaser.

**Resampling (techstep / neuro)**
- Bounce, re-detune the audio against a copy of itself, filter, flange, distort, bounce again; several passes.
  Most renders get thrown away; the best 2–5 second pieces get chopped into the line.

## What this means for the REESE engine

| Finding | Engine |
|---|---|
| The CZ 1+1' pair is the original | **OSC** modes: 2 saws, 3 / 5 / 7-saw stacks, saw + square, two sines |
| Detune in cents, 10–100 | **DTN** is the total spread in cents, 0–100, finer at the low end |
| Same movement on every note, or locked to the beat | **BEAT**: OFF (cents), or the beat locked to 1/1 … 1/16 at the song tempo |
| Clean mono sine sub, split at 80–120 Hz | **SUB** adds a sine on the fundamental; when it's up, the saws are high-passed at ~110 Hz |
| Distort only above the sub | Drive works on the high-passed saws only |
| Detune and sweep before distortion | Order: saws → crossover → drive → ladder → notch sweep → + sub |
| Notch sweeps are the key extra movement | **NTCH**: a swept comb/notch, slow, deeper and a little faster as it goes up |
| Modest resonance, 650 Hz–2 kHz | Presets sit there |
| Mono, legato, glide | Engine poly 2 (legato release), presets with glide |

Resampling chains belong to the sampler and FX work later (BREAKS and the resampler).

## Sources

- Attack Magazine: [Reese, "Just Want Another Chance" deconstructed](https://www.attackmagazine.com/technique/deconstructed/reese-just-want-another-chance/),
  [Reese bass redux](https://www.attackmagazine.com/technique/tutorials/reese-bass-redux/),
  [Reese bass with Virtual CZ](https://www.attackmagazine.com/technique/synth-secrets/reese-bass-with-virtual-cz/)
- Casio [CZ-1000 manual, detune and line select](https://www.manualslib.com/manual/1160175/Casio-Cz-1000.html?page=32)
- MusicRadar: [Reese in VirtualCZ](https://www.musicradar.com/tuition/tech/how-to-create-a-reese-bass-sound-in-virtualcz-617736)
- Native Instruments blog: [Reese bass](https://blog.native-instruments.com/reese-bass/)
- Polarity: [The classic Reese (multiple ways)](https://polarity.me/bitwig-classic-sounddesign/002-reece-or-reese-bass/)
- The Dystopian Collective: [Complete guide to Reese bass](https://www.thedystopiancollective.com/tutorials-2/how-to-create-reese-bass-the-complete-guide-to-the-iconic-drum-amp-bass-sound)
- Sample Market: [Advanced Reese techniques](https://samplemarket.co.uk/blog/4-advanced-reese-bass-techniques)
- Beat Kitchen: [Bass design and the low end](https://beatkitchen.io/guides/electronic-music/04-bass-design/)
- Transmission Samples: [How to create a Reese bass](https://www.transmissionsamples.com/tutorials/sound-design/reese-bass-create)
- dnbforum: [Reese bass](https://dnbforum.com/threads/reese-bass.14272/),
  [How to bassline thread](https://dnbforum.com/threads/how-to-bassline-thread.71837/page-13),
  [Resampling a Reese](https://dnbforum.com/threads/good-tutorial-explanation-on-resampling-reese-bass.171142/),
  [Resampling and sound manipulation](https://dnbforum.com/threads/resampling-sound-manipulation.158835/)
- Renoise forum: [Darkstep / techstep Reese](https://forum.renoise.com/t/how-to-create-a-darkstep-techstep-skullstep-reese-bass/28171)
- MusicRadar: [Ed Rush & Optical's studio](https://www.musicradar.com/news/tech/in-pictures-ed-rush-and-opticals-london-studio-636281)
- Dogs on Acid threads (titles only, could not be read): making ripping basses (phase / flange / tear),
  original Reese tutorial, layering, mixing, resampling chains, Noisia's Reese, Ed Rush & Optical Wormhole era.
