# Gain Staging Tests

`Tests/Engine/GainStaging/` guards against hidden amplifiers. Headless.

**Why.** A module whose non-gain parameter adds level pushes the Master sum past 0 dBFS, and the
device's hard clip then erases everything quieter in the mix. Only gain controls may add gain.

| Suite | What it covers |
|-------|----------------|
| ModuleGainAudit | Every float/choice parameter of every audio-processing module (Filter, VCA, Math, Voice Mixer, Channel Strip, Master and the FX modules), swept min/default/max — each choice for choice params — with three signals (0.5 sine 220 Hz, ±1 square 55 Hz, hot ±4 square; the same signal on both inputs for two-input modules). Output/input RMS gain above +6 dB fails unless `(module, param)` is on the allow-list with a stated reason and ceiling: explicit gain controls, resonant feedback, the `juce::dsp::Limiter` threshold makeup, Math Sum/Mult. Writes the full table to `<repo>/.buildlogs/gain-audit.txt` (git-ignored). Ring Modulator Drive is deliberately not listed — it was normalised, and `RingModulatorGainTests` pins the numbers |
| MixAudibility | A real hosted graph: two tones through 9×9 FX-chain pairs into two Channel Strips and Master, with the second strip at −12/0/+6/+12 dB. Whenever the Master output stays ≤ 1.0, the first tone's fundamental must stay within 3 dB of its solo level — the engine never masks one source with another. Overloaded cases are hard-clipped at ±1 to simulate the device and the first tone's loss is only reported (up to about −14 dB), because output clipping is the device's behaviour and is not asserted here. Table in `<repo>/.buildlogs/mix-audibility.txt` |
| SourceModulePeakTest | Oscillator, Wavetable and Noise sources stay within unity peak at default and maximum level |

A new allow-list entry needs a real justification, not a raised ceiling. A new module goes into
`ModuleGainAudit`'s spec list.

See also [`test-layers.md`](test-layers.md#output-level) for the shared opt-in output-level stage,
and the Ring Modulator section of [`../fx_modules.md`](../fx_modules.md).
