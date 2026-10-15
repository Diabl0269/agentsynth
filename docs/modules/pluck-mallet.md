# Pluck and Mallet Modules

Two polyphonic MIDI instruments that follow the [Drum Kit](modules.md#drum-kit-module) pattern: a pure source
(no audio or CV inputs), eight voices of their own with oldest-note stealing, and a mono-summed stereo pair out.
Each owns its amplitude, so the add-track flow builds no ADSR or VCA for them and neither has a poly mode.

## Pluck Module
A plucked string on one MIDI track: guitar-like, harp, koto. Eight voices of its own, oldest-note stealing, no external envelope. A pure source (no inputs, a mono-summed stereo pair out, Dual I/O ships split like the Drum Kit), `acceptsMidi`. Code: `Source/Modules/Pluck/` (`PluckModule` for notes, parameters, body and mix; `PluckVoices.h` for the string); the shared note-to-Hz, quiet-voice and voice-steal helpers are `Source/Modules/InstrumentVoiceUtil.h`.

A note-on starts a string at the note's pitch (12-TET, A4 = 440 Hz, shifted by `Tune` and `Fine`). Velocity scales the level linearly and, by `Velocity brightness`, opens the pick and the damping. A note-off damps the string over `Release`; the voice is freed when it falls below -100 dB.

| Parameter (id) | Range, default | What it does |
| :-- | :-- | :-- |
| `level` | 0..1, 0.8 | Output level (smoothed 10 ms) |
| `tune`, `fine` | -24..24 st, 0; -100..100 cents, 0 | Pitch offset, read at note-on |
| `decay` | 0.1..20 s, 3 | T60 of the fundamental while the key is held, at C3 |
| `decayTrack` | 0..1, 0.7 | Decay tracking: how much shorter higher notes ring. The T60 is `decay * (f0 / 130.81 Hz)^-decayTrack` (at 0.5 a C5 rings half as long as a C3), floored at 0.05 s. Added after measuring against harp recordings, whose high notes die far sooner than their low ones; the default is the harp's best measured value |
| `brightness` | 0..1, 0.5 | Less high-frequency loss per round trip of the string |
| `pickPosition` | 0.02..0.5, 0.13 | Where along the string it is plucked: low = near the bridge (thin), 0.5 = middle (round) |
| `pickHardness` | 0..1, 0.6 | Soft finger (dark) to hard pick (bright) |
| `body` | 0..1, 0.3 | Mix of the small body resonator (smoothed 20 ms) |
| `stiffness` | 0..1, 0 | Inharmonicity: upper partials run sharp, like a thick wound string |
| `release` | 0.01..5 s, 0.3 | T60 once the key is up |
| `velBright` | 0..1, 0.5 | How much velocity opens the pick hardness and the brightness |

Everything except `level` and `body` is read when a note starts (`release` when it ends), so a change lands on the next note.

- **Algorithm**: an extended Karplus-Strong string, after the classic Karplus-Strong algorithm and its Jaffe-Smith extensions (written from the textbook description, no third-party code). Each voice is a circular delay line recirculating through a first-order allpass (fractional delay, for tuning), a one-pole lowpass (damping) and a chain of four first-order allpasses (stiffness dispersion), times a loop gain.
- **Tuning**: the loop must last one period at the fundamental. At note-on the exact phase delay of the lowpass and of the dispersion allpass at the fundamental (`-arg(H(e^jw))/w`) is taken off the period, the integer part sets the delay-line length and the fractional part the tuning allpass (refined a few iterations until its phase delay at the fundamental is exact). That is why notes stay within 5 cents from E0 (20.6 Hz) to C7, at 44.1 to 96 kHz. Pitches below 20 Hz are held at 20 Hz; each delay line is sized for it in `prepareToPlay` (never on the audio thread).
- **Decay and brightness**: the loop loses `lambda = ln(1000) / (decay * f0)` nepers per round trip at the fundamental. A share `kappa = 0.1 + 0.8 * (1 - brightness)` of it is taken by the lowpass (which also damps the high partials much harder), the rest by the loop gain, so the fundamental's T60 is `decay` whatever the brightness and the loop gain is always below 1. Release swaps the target gain so the fundamental falls 60 dB in `release` seconds (never longer than `decay`), slewed over 2 ms.
- **Excitation**: the delay line is filled with white noise, lowpassed (one pole, 400 Hz x 30^hardness), then combed with `x[n] - x[n - pickPosition * length]` (circular, so there is no DC), then scaled to the velocity.
- **Stiffness**: four first-order allpasses in series, each with coefficient `-0.45 * stiffness` delays low frequencies more than high ones, so upper partials sit sharp; the fundamental stays in tune because its delay is compensated like the lowpass's.
- **Body**: three parallel bandpass biquads (100 Hz Q 4, 250 Hz Q 3, 2.5 kHz Q 1.5) on the summed strings, mixed in by `body`.
- **Headroom**: a default velocity-100 note peaks about where a default Drum Kit kick does; no parameter raises the peak by more than 6 dB over its default (`PluckModule.NoParameterAddsMoreThanSixDecibelsOverItsDefault`, and `SourceModulePeakTest` at Level 1); a pile-up of strings is rounded softly above 0.9.
- **Bypass / mute**: both clear the output (pure-source exception) as two branches, and either one cuts every string still ringing.
- **AI**: `addInstrumentTrack` accepts `"instrument": "Pluck"` (no `poly`, no `envelope`); `instrumentParams` may set any parameter above. `evaluatePatch` counts it as a sound source. Not telemetry-counted yet (the counters live in the shared contracts).

## Mallet Module
A struck bar on one MIDI track: marimba, vibraphone, xylophone, glockenspiel and bell. Eight voices of its own, oldest-note stealing, no external envelope. A pure source like the Pluck (no inputs, a mono-summed stereo pair out, Dual I/O ships split), `acceptsMidi`. Code: `Source/Modules/Mallet/` (`MalletModule` for notes, parameters, tremolo and mix; `MalletVoices.h` for the bar).

A note-on strikes a bar at the note's pitch (12-TET, A4 = 440 Hz, shifted by `Tune` and `Fine`). Velocity scales the level linearly and, by `Velocity brightness`, hardens the mallet. A note-off damps the bar over `Release`; the voice is freed when it falls below -100 dB.

| Parameter (id) | Range, default | What it does |
| :-- | :-- | :-- |
| `level` | 0..1, 0.8 | Output level (smoothed 10 ms) |
| `material` | Marimba, Vibraphone, Xylophone, Glockenspiel, Bell; Marimba | Which partial ratios and gains the bar has |
| `tune`, `fine` | -24..24 st, 0; -100..100 cents, 0 | Pitch offset, read at note-on |
| `decay` | 0.05..10 s, 1.2 | T60 of the fundamental |
| `damping` | 0..1, 0.5 | How much faster the upper modes die |
| `brightness` | 0..1, 0.5 | Tilts the mode gains toward the upper modes (total loudness is kept) |
| `hardness` | 0..1, 0.5 | Mallet contact: soft yarn (long, dark) to hard stick (short, bright, with a click) |
| `resonator` | 0..1, 0.5 | Tube-resonator boost on the fundamental: x0.5 to x1.5 |
| `tremolo` | 0..1, 0 | Depth of a 5.5 Hz amplitude wobble (vibraphone motor), smoothed 20 ms |
| `release` | 0.01..5 s, 0.5 | T60 once the key is up |
| `velBright` | 0..1, 0.5 | How much velocity hardens the mallet and brightens the tone |

Everything except `level` and `tremolo` is read when a note starts (`release` when it ends).

| Material | Partial ratios | Relative gains |
| :-- | :-- | :-- |
| Marimba | 1, 3.9, 9.2, 16.0 | 1, 0.45, 0.2, 0.08 |
| Vibraphone | 1, 4.0, 10.0 | 1, 0.3, 0.12 |
| Xylophone | 1, 3.0, 6.0, 9.8 | 1, 0.6, 0.4, 0.25 |
| Glockenspiel | 1, 2.76, 5.40, 8.93 | 1, 0.6, 0.4, 0.3 |
| Bell | 0.5, 1, 1.2, 1.5, 2.0, 2.5, 3.0 | 0.5, 1, 0.7, 0.6, 0.5, 0.4, 0.3 |

The note's pitch is always the partial with ratio 1 (the Bell's 0.5 is the hum an octave below it). A partial above 0.45 x the sample rate is skipped.

- **Algorithm**: modal synthesis, after the classic modal-synthesis literature (written from the textbook description, no third-party code). Each mode is a two-pole resonator `y[n] = 2 r cos(w) y[n-1] - r^2 y[n-2] + x[n]` with `r = exp(-ln(1000) / (T60 * fs))`, kept in double precision so low notes stay in tune; the mallet is a raised-cosine pulse of unit area fed to all modes (scaled by `sin(w)` so each mode rings at its own gain). Notes stay within 5 cents of the requested pitch.
- **Damping formula**: `T60(mode) = decay / (1 + damping * 0.6 * max(0, ratio - 1))`. The fundamental (ratio 1) always has the full `decay`; at `damping` 1 the marimba's 3.9 mode lasts 1/2.7 as long and its 16.0 mode 1/10.
- **Mallet**: pulse width `6 ms x 0.025^hardness` (6 ms down to 0.15 ms), never longer than half the note's period so a high note keeps its fundamental. Hardness also adds a short (about 0.6 ms) noise click, `0.25 x hardness^2` of full scale. `velBright` shifts hardness and brightness by `velBright x (velocity - 0.5)`.
- **Brightness**: mode gains are multiplied by `ratio^((brightness - 0.5) x 1.2)` and the set is rescaled to its original sum, so it moves energy without making the note louder or quieter.
- **Headroom**: the output is divided by the sum of the material's gains, so every material peaks about where a default Drum Kit kick does at velocity 100; no parameter raises the peak by more than 6 dB over its default (`MalletModule.NoParameterAddsMoreThanSixDecibelsOverItsDefault`, and `SourceModulePeakTest` at Level 1); a pile-up of bars is rounded softly above 0.9.
- **Bypass / mute**: both clear the output (pure-source exception) as two branches, and either one cuts every bar still ringing.
- **AI**: `addInstrumentTrack` accepts `"instrument": "Mallet"` (no `poly`, no `envelope`); `instrumentParams` may set any parameter above (`material` by name). `evaluatePatch` counts it as a sound source. Not telemetry-counted yet (the counters live in the shared contracts).


## Measured starting points

These Pluck settings were tuned by rendering each candidate through the app's own engine and measuring it against public-domain recordings (VSCO 2 Community Edition harp and solo contrabass pizzicato) on the same notes: the mean difference of log-mel spectrograms, long-term 1/3-octave spectra and 10 ms loudness curves, in dB (0 = identical). They are starting points, not presets; the AI prompt lists the same values.

| Sound | instrumentParams | Inserts in the measured patch | Distance (dB) |
| :--- | :--- | :--- | :--- |
| Concert harp | decay 12, decayTrack 0.7, brightness 1, pickPosition 0.3, pickHardness 1, body 0.6, stiffness 0.15, release 1.5 | Filter LPF24 1 kHz key-tracked 50%, EQ +5 dB at 90 Hz and -7 dB at 800 Hz, Distortion Soft drive 3 mix 0.5, Reverb room 0.5 wet 0.25 | 2.9 (a default Oscillator track: 10.9) |
| Plucked upright bass | decay 2.8, decayTrack 1, brightness 0.6, pickPosition 0.3, pickHardness 0, body 0.3, stiffness 0.08, release 0.8 | Filter LPF12 200 Hz res 0.5, EQ +7 dB at 40 Hz and +3 dB at 82 Hz, -6 dB at 800 Hz, Reverb room 0.6 wet 0.35 | 4.6 (a default Oscillator track: 10.4) |
