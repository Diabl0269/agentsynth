# Oscillator voice feel

How each unison voice of the [Oscillator](modules.md#oscillator-module) starts a note and sits in the stack. Code: `Source/Modules/Oscillator/` and `Source/Modules/UnisonVoicing.h`.

## Phase and Rand

**Phase and Rand** (`phase` 0-360 degrees, `randomPhase` 0-100 %, defaults 0): on every note start each unison voice restarts at `frac(Phase / 360 + Rand x random)`, the random drawn independently per voice from a seeded generator (`phaseRandom`, reseeded in `prepareToPlay`) so a render repeats exactly. They are sampled at the note start, so they have no CV jack. Between notes the oscillator still free-runs. **Mono** is MIDI driven: `scanMidi` records each note-on's sample offset and `processMonoMode` restarts the phases at that exact sample (`OscillatorModuleNotes.cpp`). A restart may land while the previous note still sounds, so the old phases keep running as a "ghost" and are blended out over 64 samples (`CROSSFADE_SAMPLES`) along a raised cosine (`Source/Modules/RestartFade.h`), which has no corner where the hand-over starts or ends (a straight ramp bent the sound there, a short tick on every note). A restart while a hand-over is still running turns the stack it interrupts into one more ghost with the share it holds at that sample, up to five at once, and each ghost leaves along its own curve, so the output never steps however fast restarts come; with no restart the output is byte-identical. Held notes are tracked in `synth::HeldNotes`; an overlapping note-on restarts today, and `shouldRestartOnNoteOn(overlapping)` is the one place a legato mode gates that. **Poly** restarts a voice on every rising edge of its **Restart** (Gate) input (below), on the exact sample, fading out of the old phases. Without a gate it falls back to restarting a voice when its pitch CV goes from silent (below 20 Hz) to sounding, the Wavetable's rule, and voice 0 also on a MIDI note-on (at the block start, fading out of the old phases only if it was already sounding).

**Gate input.** Poly MIDI holds a voice's pitch after release, so the pitch alone cannot show a new note on a voice that already played. The note start comes from a gate edge instead, the way modular systems reset an oscillator (a gate or trigger into its reset input) and the way voice-based synths restart each voice on note-on. The jack is the last input, shown as **Restart** on the card (its hover text: "Restarts the wave on each rising edge, like oscillator sync; poly instruments wire it automatically"); the name is display only, its channel, its `PortRole::Gate` and every saved cable are unchanged: in poly an 8-wide fan on raw ch20-27 (`kGateBase`), in mono one channel on raw ch20. `GateEdges` (`Source/Modules/GateEdges.h`, shared with the Wavetable) finds each voice's rising edges in the block with a Schmitt trigger at 0.1, low enough that a soft Vel -> Gate note still restarts. Poly MIDI drops a re-used voice's gate to 0 for at least 1 ms before every note, so every note-on is an edge. In mono a gate edge joins the MIDI note-ons in the per-sample restart list. Patch a slow source into it for a restart in time with it, or an audio-rate oscillator for a sync-style sound. `GateEdges` keeps an edge only if it comes at least 16 samples (`kMinSpacing`, about 3 kHz at 48 kHz) after the last kept one, counted across blocks, and holds up to 256 per block, so a fast gate restarts on the same samples whatever the host's block size. An unpatched gate is silent and changes nothing. The Poly switch (`PolyVoiceGraph`) and new poly instrument tracks wire Poly MIDI's Gate fan (ch8-15) to it, and a cable dragged from Poly Out onto Gate pairs the gate fan by role. Raw ch20-27 sit above the original 22 outputs, so the module now declares 28 outputs (ch22-27 silent): JUCE only gives a module a private copy of a shared input on channels below its output count, and without that a bypassed or muted Oscillator would clear Poly MIDI's gate for the ADSR. ch20-21 alias Audio R (voices 6-7) and are read before anything is written, like ch14-19.
## Restart UX options

Open for a later pick, none built: (1) keep the Restart jack on every Oscillator, clearly named (today); (2) show the jack only while Poly is on, since mono users rarely meet it and poly wiring is automatic; (3) a separate **Sync** input for patched oscillators and keep Restart for the gate fan only.

## Blend, Width and Detune

**Blend and Width** (`blend` 0-1 default 1, `width` 0-1 default 0): Blend leaves the voices nearest the root (the middle voice for an odd count, the middle two for an even one) at full level and scales the others; the stack is divided by the square root of the summed squared gains (constant power), so uncorrelated voices keep the loudness of one voice at any Unison count; one voice divides by exactly 1, and in-phase onsets can peak up to the square root of the voice count. Width pans the voices across Audio L / Audio R with the Wavetable's law (`synth::unisonPanGains`, `UnisonVoicing.h`, shared by both modules); Pan then balances the result. At Width 0 the stack renders mono and Audio R is a copy of Audio L, as before. Both are smoothed (10 ms) and ramped inside the block. Spreading never adds level: the balance law only attenuates the far leg, so the combined L+R energy at Width 1 is at or below Width 0. Detune keeps its detune offsets; the knob is skewed (centre at 25 cents) for fine control while the stored value stays in cents, and Detune CV adds cents linearly (+1.0 = 100 cents).

## Blend and Width CV

Blend and Width have CV jacks, appended after Glide: Blend on raw channel 18, Width on 19, the same channel in mono and poly (inputs 18 -> 20; the Gate fan later took 20 -> 28 and the outputs grew to 28 with it). Like Unison, Detune, Pulse Width and Glide they alias the Audio R output block, so `readGlobalParamCV` reads them before anything is written or cleared. Phase and Rand have no jack.

## Glide and Legato (mono feel)

Three Oscillator parameters, all in the card's More controls row:

- **Glide Mode** (`glideMode`): `Always` (default) takes the Glide time for any interval. `Scaled` takes Glide x |interval in semitones| / 12, so an octave takes the Glide time and a semitone a twelfth of it.
- **Glide Curve** (`glideCurve`): `Linear` (default) or `Exponential`, an RC-style approach in pitch (fast start, slow settle) that is timed to reach the target by the set time, then snaps.
- **Legato** (`legato`, mono only, default off): glide happens only for overlapping notes (tracked by `HeldNotes`); a note that does not overlap another jumps. An overlapping note does not restart the phase, and releasing the current note while others are held glides back to the most recent held note. With Legato off the behaviour is unchanged.

The ADSR has a matching Legato switch (see `modules.md`). Code: `Oscillator/PitchGlide.h`, `OscillatorModuleNotes.cpp`.

## Fixed rate

`rateMode` (Rate: Follow notes / Fixed, default Follow notes) and `fixedRate` (Hz, 0.1 to 2000, skewed around 20 Hz, default 50) let the oscillator run at a set frequency whatever note is played, like a DX7 operator's Fixed mode or a free-running Eurorack VCO. Its main use is an audio-rate modulation source: a 53 Hz square patched into another Oscillator's Level makes a frog croak.

- **Formula**: in Fixed mode the frequency is `fixedRate * 2^((12*octave + coarse + fine/100 + CV shifts) / 12)`. The note term is dropped; the knobs and the Octave/Coarse/Fine CV still offset it. Follow notes is unchanged.
- **Floor**: Follow notes clamps a voice to 20-20000 Hz; Fixed clamps to 0.1-20000 Hz, so sub-20 Hz rates (LFO-like) work.
- **Poly voices stay note-gated**: a voice is live only while its pitch CV is at least 20 Hz (voice 0 falls back to the MIDI note); a live voice plays the Fixed rate instead of its note, an inactive one stays silent.
- **Phase restarts on notes** (note-on, Gate edge, a poly voice going live) exactly as in Follow notes, so a fixed-rate modulator keeps its pulses aligned to each call.
- **Glide has no effect** in Fixed mode: the target does not move with notes.
- **On the card**: Rate waits in the More row, so the card keeps its size; the Fixed rate knob takes Glide's cell while Rate is Fixed (no notes to glide between).
- **No jack for Fixed rate** (to keep the channel layout); automate the knob instead.

The shared logic is `OscillatorModule::baseFrequencyHz()` and `minRenderHz()` in `Source/Modules/Oscillator/OscillatorModuleRate.cpp`; tests: `Tests/Modules/OscillatorFixedRateTests.cpp`.
