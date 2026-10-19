# Oscillator voice feel

How each unison voice of the [Oscillator](modules.md#oscillator-module) starts a note and sits in the stack. Code: `Source/Modules/Oscillator/` and `Source/Modules/UnisonVoicing.h`.

## Phase and Rand

**Phase and Rand** (`phase` 0-360 degrees, `randomPhase` 0-100 %, defaults 0): on every note start each unison voice restarts at `frac(Phase / 360 + Rand x random)`, the random drawn independently per voice from a seeded generator (`phaseRandom`, reseeded in `prepareToPlay`) so a render repeats exactly. They are sampled at the note start, so they have no CV jack. Between notes the oscillator still free-runs. **Mono** is MIDI driven: `scanMidi` records each note-on's sample offset and `processMonoMode` restarts the phases at that exact sample (`OscillatorModuleNotes.cpp`). A restart may land while the previous note still sounds, so the old phases keep running in a fade copy and are blended out over 64 samples (`CROSSFADE_SAMPLES`); with no restart the output is byte-identical. Held notes are tracked in `synth::HeldNotes`; an overlapping note-on restarts today, and `shouldRestartOnNoteOn(overlapping)` is the one place a legato mode gates that. **Poly** restarts a voice when its pitch CV goes from silent (below 20 Hz) to sounding, the Wavetable's rule, and voice 0 also on a MIDI note-on (at the block start, fading out of the old phases only if it was already sounding). Known limitation: Poly MIDI holds a voice's pitch after release, so a later note on the same voice is not seen as new and does not restart.
## Blend, Width and Detune

**Blend and Width** (`blend` 0-1 default 1, `width` 0-1 default 0): Blend leaves the voices nearest the root (the middle voice for an odd count, the middle two for an even one) at full level and scales the others; the stack is divided by the sum of the gains, so Blend 1 is exactly the plain average. Width pans the voices across Audio L / Audio R with the Wavetable's law (`synth::unisonPanGains`, `UnisonVoicing.h`, shared by both modules); Pan then balances the result. At Width 0 the stack renders mono and Audio R is a copy of Audio L, as before. Both are smoothed (10 ms) and ramped inside the block. Spreading never adds level: the balance law only attenuates the far leg, so the combined L+R energy at Width 1 is at or below Width 0. Detune keeps its detune offsets; the knob is skewed (centre at 25 cents) for fine control while the stored value stays in cents, and Detune CV adds cents linearly (+1.0 = 100 cents).

## Blend and Width CV

Blend and Width have CV jacks, appended after Glide: Blend on raw channel 18, Width on 19, the same channel in mono and poly (inputs 18 -> 20, still below the 22 outputs). Like Unison, Detune, Pulse Width and Glide they alias the Audio R output block, so `readGlobalParamCV` reads them before anything is written or cleared. Phase and Rand have no jack.

## Glide and Legato (mono feel)

Three Oscillator parameters, all in the card's More controls row:

- **Glide Mode** (`glideMode`): `Always` (default) takes the Glide time for any interval. `Scaled` takes Glide x |interval in semitones| / 12, so an octave takes the Glide time and a semitone a twelfth of it.
- **Glide Curve** (`glideCurve`): `Linear` (default) or `Exponential`, an RC-style approach in pitch (fast start, slow settle) that is timed to reach the target by the set time, then snaps.
- **Legato** (`legato`, mono only, default off): glide happens only for overlapping notes (tracked by `HeldNotes`); a note that does not overlap another jumps. An overlapping note does not restart the phase, and releasing the current note while others are held glides back to the most recent held note. With Legato off the behaviour is unchanged.

The ADSR has a matching Legato switch (see `modules.md`). Code: `Oscillator/PitchGlide.h`, `OscillatorModuleNotes.cpp`.
