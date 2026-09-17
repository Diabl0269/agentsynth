# Gain Staging Tests (3 tests)

`Tests/Engine/GainStaging/` — guards against hidden amplifiers. A module whose non-gain parameter adds level pushes the Master sum past 0 dBFS, and the device's hard clip then erases everything quieter in the mix. Headless.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| ModuleGainAudit | 1 | Every float/choice parameter of every audio-processing module (Filter, VCA, Math, Voice Mixer, Channel Strip, Master and the FX modules), swept min/default/max (each choice for choice params) with three signals (0.5 sine 220 Hz, ±1 square 55 Hz, hot ±4 square; same signal on both inputs for two-input modules). Output/input RMS gain above +6 dB fails unless `(module, param)` is on the allow-list with a stated reason and ceiling: explicit gain controls, resonant feedback, the `juce::dsp::Limiter` threshold makeup, Math Sum/Mult. Writes the full table to `<repo>/.buildlogs/gain-audit.txt` (git-ignored). Ring Modulator Drive is deliberately not listed (it was normalised; `RingModulatorGainTests` pins the numbers) |
| MixAudibility | 1 | Real hosted graph: two tones through 9×9 FX-chain pairs into two Channel Strips and Master, with the second strip at −12/0/+6/+12 dB. Whenever the Master output stays ≤ 1.0, the first tone's fundamental must stay within 3 dB of its solo level (the engine never masks one source with another). Overloaded cases are hard-clipped at ±1 to simulate the device and the first tone's loss is only reported (up to about −14 dB), because output clipping is the device's behaviour and not asserted here. Table in `<repo>/.buildlogs/mix-audibility.txt` |
| SourceModulePeakTest | 1 | Oscillator, Wavetable and Noise sources stay within unity peak at default and maximum level |

See also [`testing.md`](testing.md) and the Ring Modulator section of [`fx_modules.md`](fx_modules.md).
