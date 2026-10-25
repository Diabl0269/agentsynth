# Gain Staging Tests

`Tests/Engine/GainStaging/` guards against hidden amplifiers. Headless.

**Why.** A module whose non-gain parameter adds level pushes the Master sum past 0 dBFS, and the
device's hard clip then erases everything quieter in the mix. Only gain controls may add gain.

| Suite | What it covers |
|-------|----------------|
| ModuleGainAudit | Every float/choice parameter of every audio-processing module (Filter, VCA, Math, Voice Mixer, Channel Strip, Master and the FX modules), swept min/default/max — each choice for choice params — with three signals (0.5 sine 220 Hz, ±1 square 55 Hz, hot ±4 square; the same signal on both inputs for two-input modules). Output/input RMS gain above +6 dB fails unless `(module, param)` is on the allow-list with a stated reason and ceiling: explicit gain controls, resonant feedback, the `juce::dsp::Limiter` threshold makeup, Math Sum/Mult. The shared `mix` and `pan` of the output stage ([Mix and Pan](../modules/fx-modules.md#mix-and-pan-shared-stage)) are swept like any other parameter and need no entry: a blend of dry and wet and a balance pan never exceed the louder leg. Writes the full table to `<repo>/.buildlogs/gain-audit.txt` (git-ignored). Ring Modulator Drive is deliberately not listed — it was normalised, and `RingModulatorGainTests` pins the numbers |
| MixAudibility | A real hosted graph: two tones through 9×9 FX-chain pairs into two Channel Strips and Master, with the second strip at −12/0/+6/+12 dB. Whenever the Master output stays ≤ 1.0, the first tone's fundamental must stay within 3 dB of its solo level — the engine never masks one source with another. Overloaded cases are hard-clipped at ±1 to simulate the device and the first tone's loss is only reported (up to about −14 dB), because output clipping is the device's behaviour and is not asserted here. Table in `<repo>/.buildlogs/mix-audibility.txt` |
| SourceModulePeakTest | Oscillator, Wavetable and Noise sources stay within unity peak at default and maximum level |
| OscillatorVoiceFeel (source peak) | The Oscillator's Blend (0, 0.5, 1) and Width (0, 1) at 8 detuned unison saws: peak at most sqrt(N), RMS within 1.5 dB of one voice; no allow-list entry, since neither adds gain |

**Rules.** Mono signals and the Voice Mixer pass a note at unity. Polyphonic voices are summed in the VCA's
poly mode at a fixed per-voice gain, `synth::kPolyVoiceGain` (0.5, `Source/Modules/SoftLimit.h`), never
normalised by the voice count or by a dynamic headroom gain: every note adds the same level, so a chord is louder
than one note, as on other polyphonic synths. The VCA poly sum and the Voice Mixer end in the shared soft limiter
(`SoftLimit.h`), which is the identity at or below 0.9 and rounds larger sums towards 0.98; it is the safety net for
chords bigger than the fixed gain leaves room for, not part of the sound of a normal chord. A unison stack is
constant power: the Oscillator and the Wavetable divide it by the root of the summed squared voice gains, so its
loudness does not change with Unison. Voices that start in phase can peak at sqrt(N); headroom for that is handled
downstream (soft limiter, channel faders).

The default instrument chain's VCA (mono and poly alike) sits at gain 0.5, so each mono note leaves 6 dB of headroom;
in poly mode the per-voice gain takes another 6 dB off each voice, so a poly note is half as loud as the same note
through the mono path.

**Why a fixed gain, and why 0.5.** Three full-level sines at VCA gain 0.5 summed to 1.5 when voices added at unity,
and the soft limiter above 0.9 turned that into clipping (12 % distortion for three notes, 32 % for eight, the broken
telephone sound). A dynamic headroom gain that followed the voices' peaks fixed the distortion but made eight notes
about as loud as one, which is not how a chord sounds on other polyphonic synths: they sum each voice at a fixed gain
and let the sum grow. The value is the largest of 0.5, 0.4, 0.35 and 0.28 for which a three-note chord stays under 1.5 %
distortion at every block size `OscillatorPolyChord.ThreeNoteChordStaysTheSumOfItsSines` renders: at 0.5 three notes read
0.02 % distortion at a peak of 0.75, 4.8 dB above one note, and eight notes are 8.5 dB above one (and reach the soft
limiter: about 13 % distortion at the default level). Smaller values only trade level for a cleaner eight-note chord
(0.28 reads 0.8 % at eight notes). Pinned by `OscillatorPolyChord.*` (`Tests/Modules/OscillatorPolyChordTests.cpp`), which
renders Poly MIDI, Oscillator, ADSR and VCA over several block sizes and loop offsets, fits the sines of the played notes
to the output, and checks that level grows with every note and stays under the soft-limit ceiling.

A new allow-list entry needs a real justification, not a raised ceiling. A new module goes into
`ModuleGainAudit`'s spec list.

See also [`test-layers.md`](test-layers.md#output-level) for the shared opt-in output-level stage,
and the Ring Modulator section of [`../modules/fx-modules.md#ring-modulator-module`](../modules/fx-modules.md#ring-modulator-module).
