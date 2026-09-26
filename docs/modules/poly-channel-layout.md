# Poly Channel Layout

The raw `AudioProcessorGraph` channel assignments for every poly-capable module, and the rule a new
one follows. [`modulation.md`](modulation.md) covers how `getModulationRoutings()` and the
logical-port API collapse these raw channels into visible wires.

## Rule for new poly modules

In poly mode, voices occupy channels 0-7 (audio/pitch/gate) and the shared-CV block starts at channel
8. **Declare `numOutputs >= the highest CV input channel index you read**, to avoid JUCE
`AudioProcessorGraph` buffer aliasing when `inputChan >= getTotalNumOutputChannels()`. Raising the
declared output count also means JUCE hands the node private copies of shared CV buffers, so an
end-of-block CV clear can only ever zero its own copy.

Declare a per-voice **output** fan in `mapOutputChannel()` if the module actually emits one signal per
voice — Oscillator, Filter and Noise all fan raw ch0-7 onto a single Audio jack this way. Do not, if
the module sums voices down instead: VCA has no `mapOutputChannel()` override because its poly mode
sums all 8 voices to stereo, a plain jack rather than a fan. Getting this wrong misdirects
`GraphEditor::resolvePolyLink` (see
[`modulation.md`](modulation.md#creating-poly-connections)) into fanning a dragged cable out of an
output that only ever produces one signal.

**A second audio leg goes on a dedicated `kRightBase` block, never on ch1.** On the voice modules ch1
is already a CV input (Waveform, Position, Cutoff, gain), so relabelling it "Right" would repoint
every saved patch that modulates it. Each module's `kRightBase` is `kNumInputs` — the first channel
above its whole input block — and an end-of-block CV clear must be bounded at `kRightBase` rather
than running to `getNumChannels()`, or it erases the right leg.

## Channel table

| Module | Channel | Direction | Content |
|--------|---------|-----------|---------|
| **PolyMidi** | ch0-7 | Out | Pitch CV per voice (Hz) |
| **PolyMidi** | ch8-15 | Out | Gate CV per voice (0/1) |
| **Oscillator (poly)** | ch0-7 | In | Per-voice pitch CV (Hz) |
| **Oscillator (poly)** | ch8 | In | Shared Waveform CV |
| **Oscillator (poly)** | ch9 | In | Shared Octave CV |
| **Oscillator (poly)** | ch10 | In | Shared Coarse CV |
| **Oscillator (poly)** | ch11 | In | Shared Fine CV |
| **Oscillator (poly)** | ch12 | In | Shared Level CV |
| **Oscillator (poly)** | ch13 | In | Shared Pan CV |
| **Oscillator (poly/mono)** | ch14 | In | Unison CV (FRO314, same channel both modes) |
| **Oscillator (poly/mono)** | ch15 | In | Detune CV (FRO314, same channel both modes) |
| **Oscillator (poly)** | ch0-7 | Out | Per-voice audio — `Audio L` |
| **Oscillator (poly)** | ch8-13 | Out | Silent pass-throughs (prevent buffer aliasing) |
| **Oscillator (poly)** | ch14-21 | Out | Per-voice audio — `Audio R` (`kRightBase`); ch14/15 alias the Unison/Detune CV inputs above |
| **Oscillator (mono)** | ch0 | In/Out | Pitch CV in / `Audio L` out (shared channel, CV saved before clear) |
| **Oscillator (mono)** | ch1 | In | Waveform CV |
| **Oscillator (mono)** | ch2 | In | Octave CV |
| **Oscillator (mono)** | ch3 | In | Coarse CV |
| **Oscillator (mono)** | ch4 | In | Fine CV |
| **Oscillator (mono)** | ch5 | In | Level CV |
| **Oscillator (mono)** | ch6 | In | Pan CV |
| **Oscillator (mono)** | ch14 | Out | `Audio R` (`kRightBase`); aliases the Unison CV input above |
| **Wavetable (poly)** | ch0-7 | In | Per-voice pitch CV (Hz) |
| **Wavetable (poly)** | ch8 | In | Shared Position CV |
| **Wavetable (poly)** | ch9 | In | Shared Octave CV |
| **Wavetable (poly)** | ch10 | In | Shared Coarse CV |
| **Wavetable (poly)** | ch11 | In | Shared Fine CV |
| **Wavetable (poly)** | ch12 | In | Shared Level CV |
| **Wavetable (poly)** | ch13-22 | In | Shared Warp, Phase, Rand, Detune, Spread, Width, Blend, Sub, Pan, Sync CV |
| **Wavetable (poly)** | ch0-7 | Out | Per-voice audio **L** |
| **Wavetable (poly)** | ch8-22 | Out | Silent pass-throughs (prevent buffer aliasing) |
| **Wavetable (poly)** | ch23-30 | Out | Per-voice audio **R** (`kRightBase`) |
| **Wavetable (mono)** | ch0 | In/Out | Pitch CV in (inert) / Audio L out (shared channel) |
| **Wavetable (mono)** | ch1 | In | Position CV |
| **Wavetable (mono)** | ch2 | In | Octave CV |
| **Wavetable (mono)** | ch3 | In | Coarse CV |
| **Wavetable (mono)** | ch4 | In | Fine CV |
| **Wavetable (mono)** | ch5 | In | Level CV |
| **Wavetable (mono)** | ch6-15 | In | Warp, Phase, Rand, Detune, Spread, Width, Blend, Sub, Pan, Sync CV |
| **Wavetable (mono)** | ch23 | Out | Audio R (`kRightBase`) |
| **Filter (poly)** | ch0-7 | In/Out | Per-voice audio — `Audio L`, filtered in place |
| **Filter (poly)** | ch8 | In | Shared Cutoff CV |
| **Filter (poly)** | ch9 | In | Shared Resonance CV |
| **Filter (poly)** | ch10 | In | Shared Drive CV |
| **Filter (poly)** | ch11-18 | In/Out | Per-voice audio — `Audio R` (`kRightBase`), own ladder per voice |
| **Filter (mono)** | ch0 | In/Out | `Audio L` in / filtered out |
| **Filter (mono)** | ch1 | In | Cutoff CV |
| **Filter (mono)** | ch2 | In | Resonance CV |
| **Filter (mono)** | ch3 | In | Drive CV |
| **Filter (mono)** | ch11 | In/Out | `Audio R` (`kRightBase`) in / filtered out |
| **VCA (poly)** | ch0-7 | In | Per-voice audio — `Audio L` |
| **VCA (poly)** | ch8-15 | In | Per-voice envelope/CV |
| **VCA (poly)** | ch16-23 | In | Per-voice audio — `Audio R` (`kRightBase`) |
| **VCA (poly)** | ch0 | Out | `Audio L` — sum of the left voice block |
| **VCA (poly)** | ch1 | Out | Vestigial duplicate of the left sum (the old mono→stereo affordance) |
| **VCA (poly)** | ch16 | Out | `Audio R` — sum of the right voice block |
| **VCA (mono)** | ch0 | In/Out | `Audio L` in / gated out |
| **VCA (mono)** | ch1 | In | Gain CV (also overwritten on the way out — see [`modules.md`](modules.md#vca-amplifier-module)) |
| **VCA (mono)** | ch16 | In/Out | `Audio R` (`kRightBase`) in / gated out |
| **ADSR (poly)** | ch0-7 | In | Per-voice gate CV |
| **ADSR** | ch8 | In | Threshold CV (shared) |
| **ADSR** | ch9 | In | Shared Attack CV |
| **ADSR** | ch10 | In | Shared Hold CV |
| **ADSR** | ch11 | In | Shared Decay CV |
| **ADSR** | ch12 | In | Shared Sustain CV |
| **ADSR** | ch13 | In | Shared Release CV |
| **ADSR (poly)** | ch0-7 | Out | Per-voice envelope (0–1) |
| **ADSR** | ch8-13 | Out | Silent pass-throughs (prevent buffer aliasing) |
| **Sample & Hold** | ch0 | In/Out | Signal in / held CV out (shared channel; read before overwrite) |
| **Sample & Hold** | ch1 | In | Trigger / gate |
| **Sample & Hold** | ch2 | In | Rate CV |
| **Sample & Hold** | ch3 | In | Slew CV |
| **Sample & Hold** | ch4 | In | Level CV |
| **Sample & Hold** | ch5 | In | Offset CV |
| **Sample & Hold** | ch6 | In | Threshold CV |
| **Sample & Hold** | ch1-6 | Out | Silent (cleared each block so CV does not leak downstream) |
| **Comparator** | ch0 | In/Out | Signal in / Gate out (shared channel; read before overwrite) |
| **Comparator** | ch1 | In/Out | Threshold CV in / Inverse gate out |
