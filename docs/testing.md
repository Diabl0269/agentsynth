# Testing Guide

All tests use GoogleTest and run headless (no audio device, no GUI window). 4696 tests across 483 suites (`./build/Tests/Tests` reports the authoritative count; the per-section totals below are approximate).

```bash
# Run all tests (ENABLE_TESTS defaults OFF — must be passed explicitly)
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target Tests
./build/Tests/Tests

# Run a specific suite
./build/Tests/Tests --gtest_filter="E2EWorkflow*"

# Check coverage (threshold: 85%)
bash scripts/coverage.sh
```

By default, builds skip tests to save time. Use the `-DENABLE_TESTS=ON` flag to enable them.

## Test Layers

### Audio Rendering Tests (~217 tests)

Headless DSP tests that render audio through individual modules and verify output characteristics — RMS levels, silence detection, frequency response, waveform accuracy.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| OscillatorTest | 11 | Waveform generation (sine, saw, square, triangle), MIDI response, tuning, frequency accuracy |
| FilterTest | 10 | Low-pass/high-pass filtering, cutoff/resonance parameters, frequency response across 7 filter types |
| EnvelopeGeneratorTest | 21 | `Tests/Modules/Envelope/EnvelopeGeneratorTests.cpp` — the shared `synth::EnvelopeGenerator` engine ADSRModule is built on, independent of the module wrapper: zero-duration-stage cascade (a 0 ms stage costs no samples), exact stage timing across a curve sweep (`CurveSweep/EnvelopeGeneratorStageDurationTest`, parameterised over attack/decay/release x 3 curve amounts = 9 of the 21), `shape()` endpoint/monotonicity, release from mid-attack/mid-decay starting at the current level, retrigger-from-current-level (never 0, never instant), a mid-ramp time change with no level discontinuity, sustain 0/1 edge cases, and a gapless note sequence at sustain 0 |
| ADSRTest | 29 | `Tests/Modules/ADSR/` — split by concern into `ADSRStageTests.cpp` (attack/hold/decay/sustain/release shapes, retriggering, zero-sustain and sustain==1 edge cases, parameter changes during playback), `ADSRGateTests.cpp` (**mono Gate CV**: rising edge starts, falling edge releases, MIDI-only still works, MIDI+Gate OR so note-off does not release while CV is high), `ADSRPolyTests.cpp` (poly mode independent envelopes and gate edge detection), `ADSRThresholdTests.cpp` (**Threshold**: default 0.5, raised threshold rejects a sub-threshold gate, Threshold CV, hysteresis, port layout). Eight of these are the FRO110 regression suite: the mono held-note bitset fix (a MIDI note-on re-articulates even when a note-off + note-on land on the same sample, and releasing one of two held notes keeps the envelope up) and the zero-sustain/sustain==1 release-amputation fixes (`EnvelopeGenerator`'s progress-based stages have no rate that can compute to 0 and self-amputate) |
| EnvelopeFollowerModuleTest | 28 | Peak/RMS detection accuracy (unit sine → 1/√2 in RMS), attack/release time-constant ordering, unipolar clamped output, Attack/Release/Sensitivity CV modulation, CV channels cleared on output, bypass/mute emitting no CV, port roles (Audio in / ModCV out), state round-trip, zero-channel/zero-sample/zero-sample-rate robustness |
| LFOModuleTest | 11 | LFO waveform output, rate modulation, sync behavior |
| VCAModuleTest | 5 | Gain application, envelope following, silence detection |
| AttenuverterModuleTest | 4 | CV signal attenuation, bipolar control, CV modulation |
| SampleHoldModuleTest | 51 | Port topology & modulation targets, Sample-mode rising-edge latch + hold across blocks, Track-mode follow/freeze, internal free-running clock and Rate CV, random source range, Level/Offset/Slew shaping + their CV inputs, **Schmitt-trigger threshold** (low-amplitude and negative-threshold gates, hysteresis rejects dither and in-gap dips, full release re-arms, Threshold CV), **trigger meter telemetry** (signed block peak, live on internal clock, armed state, capture count, reset on bypass/mute), `ThresholdControlComponent` static helpers (`valueToX` clamping, unipolar / dB mapping, `needsRepaint` idle-gate, preferred height) + paint smoke test, trigger/CV channel hygiene, bypass dry pass-through & mute silence, zero-length/zero-channel/single-sample buffers, zero sample rate, state round-trip |
| ComparatorModuleTest | 17 | Port topology (Signal / Threshold in, Gate / Inverse out), gate high above threshold and low below, inverse complements gate, hysteresis, Threshold CV, bypass/mute silence both outputs, meter peak, trigger count, default threshold 0.5 |
| SchmittTriggerTest | 6 | Shared rising-arm / falling-rearm helper: starts low, arms above threshold, holds inside the 0.05 gap, falling edge, reset, equal-to-threshold does not arm |
| MacroControlModuleTest | 14 | Port model (16 channels, 8 visible by default, no MIDI, ModCV role), per-macro CV output, channels above the `Knobs` count silent, hidden knobs keep their values when the bank is re-grown, bipolar mapping, 20 ms smoothing, bypass/mute silence, zero-channel buffer; factory creation, `macroCount`+knob JSON round-trip, one macro fanned out to two destinations |
| FX module tests | 53 | Delay (passthrough, feedback), Distortion (clipping, drive), Reverb (room size), Chorus, Phaser, Compressor, Flanger, Limiter, Bitcrusher (downsampling, quantization, CV), Ring Modulator (diode-ring, oversampling aliasing) |
| GateModuleTest | 13 | `Tests/FX/GateModuleTests.cpp`. Hysteresis Schmitt trigger (opens at Threshold, closes only below Threshold - hysteresis, a level sitting in the gap keeps whichever state the gate was already in), Attack/Hold/Release timing measured in samples against the analytic linear-ramp model, Range floor is the parameterised gain (e.g. -20 dB -> 0.1 amplitude) not silence, both stereo legs gated identically by the linked max(\|L\|,\|R\|) detector, port labels/counts, module type/category. Bypass dry pass-through and mute silence live in `FXBypassTest` alongside every other FX module |
| PitchShifterModuleTest | 22 | Pitch mode transposition ratios (spectral peak), Frequency mode SSB offset + sideband suppression, CV routing, feedback stability, state round-trip |
| SamplerModuleTest | 31 | Registration + port/parameter surface; WAV load (success, missing file, unreadable file, failed load keeps the previous sample, clear); Sample mode playback verified sample-exact against a ramp file at unity rate, at `pitch = +12` (2×), via MIDI note transpose, and with `start = 0.5`; monotonic anti-click fade-in; one-shot falls silent at the last frame vs loop keeps going; Granular mode produces bounded finite audio and stays silent with no sample loaded, including at max density × max grain size; gate precedence (free-run with nothing patched, trigger-CV latch silences a low gate, retrigger, a gate rising mid-block is not mistaken for an unpatched jack); bypass/mute clear; CV channels do not leak to the output; level CV sums with the parameter; zero-channel buffer is safe; `getExtraState`/`setExtraState` round trip, restored through `graphToJSON` → `applyJSONToGraph` on the trusted path and **dropped** on the untrusted path |
| SampleWaveformPeaks | 4 | `SampleWaveformComponent::computePeaks` — empty inputs, columns span the buffer and track min/max extremes, channels averaged (opposite phase cancels), more columns than frames |
| SampleWaveformPaint | 2 | Paints the empty state ("No sample loaded") and a loaded sample with a live playhead into a `juce::Image`; repeat `timerCallback()` with nothing changed is a no-op; zero-width component is safe |
| SamplerFormats | 2 | `getSupportedFormatWildcard()` is non-empty and includes `*.wav`; `isSupportedAudioFile` accepts wav/WAV/aiff and rejects .json/.txt/extensionless/directories (extension-only check, so drag-hover stays cheap) |
| Parametric EQ tests | 64 | `Tests/FX/ParametricEQModuleTests.cpp`. `ParametricEQModuleTest` — identity, 6-in/2-out channel layout, port labels, mod targets, logical-port roles, slot types and row labels, **all four bands start disabled**, enable round-trip and count, a disabled band with a big gain still contributing nothing, setters clamping to range, bypass dry pass-through / CV clearing / mute. `ParametricEQPointPlacement` — `findBandForNewPoint` picks the nearest free slot on a log axis, skips slots in use, returns -1 when full, and resolves out-of-range frequencies. `ParametricEQResponse` — `bandMagnitudeDb` anchor points (bell hits its gain at centre and 0 dB two decades out; cut is the exact mirror of boost; higher Q narrows the bell; a shelf sits at half its gain at the corner; zero-gain bands are flat everywhere; degenerate inputs return unity, not NaN) and `responseDb` skipping disabled bands plus adding the output trim. `ParametricEQCoefficients` — the RBJ digital biquad agrees with the analog prototype it came from within 0.6 dB, zero gain yields a literal pass-through biquad (`b == a`), and centres past Nyquist / a zero sample rate stay finite. `ParametricEQAudio` — real-audio level measurements (all-off is a straight wire; a configured-but-disabled band does not touch audio; enabling applies and disabling restores unity; ±12 dB bells move their band by ±12 dB; a narrow boost leaves distant tones alone; shelves only touch their own end; output gain scales everything; stereo channels come out identical) plus `MeasuredResponseTracksTheAnalyticCurve`, which locks the drawn curve to the measured DSP within 1 dB. `ParametricEQCV` — freq CV is exponential over the full range, gain CV maps onto ±24 dB and clamps, near-silent CV is gated to exactly the unmodulated value while CV above the gate gets through, and the shelves are provably *not* CV-modulated. `ParametricEQEdgeCases` — zero-length/zero-channel/mono buffers, processing without `prepareToPlay`, re-preparing at a new sample rate, band response independent of sample rate, state round-trip preserving enable flags, and every band exposing the full parameter set |
| AntiClickTest | 4 | ADSR's 1 ms attack/release defaults don't click and an explicit 0 ms is honoured (no clamp), smooth parameter transitions |
| AutomationZipperTest / AutomationZipperCoverage | 131 | `Tests/Timeline/AutomationZipperTests.cpp` — the zipper net for timeline automation. `EveryAutomatableFloatParam/AutomationZipperTest` is parameterised over **(factory module × float parameter), generated at runtime from `AIStateMapper::moduleFactoryTypeNames()` and each module's live parameter list**, so a new module or a new float parameter is swept without touching the test. Each case renders 62 blocks at 48 kHz: 50 walking the parameter's full range through the applier's own write path (`param->setValue(param->convertTo0to1(v))`, once per block), then a square wave between the two extremes — four instantaneous full-range jumps, which is the case smoothing exists for. Asserts every output sample is finite (universal, never relaxed) and that the worst inter-sample delta stays under 0.6, with per-module/per-parameter overrides carrying a written justification (Noise and Sample & Hold are staircases by construction; Bitcrusher is a quantiser; the EQ's `bandNGain` cannot absorb an instant ±24 dB coefficient swap). The three `AutomationZipperCoverage` tests are the enforcement half, mirroring `WavetableWarpAliasTest`: a factory module with float parameters that is neither configured nor excluded fails the build, a config entry naming a module with no float parameters is rejected as stale, every configured name must still resolve through the factory, and every exclusion / raised bound must carry a reason |
| EdgeCaseTests | 21 | Zero-length buffers, extreme parameters, single-sample buffers, rapid parameter changes, large buffers |
| AudioRenderingTests | 26 | Snapshot-based tests comparing bit-perfect output against reference files; covers full chains (Osc->Filter->VCA), modulation accuracy, and External MIDI input |

### Transport / offline render tests (64 tests)

The timeline's clock and the headless render harness built on it. No audio device, no sleeps, fully deterministic: engines here are always `HostMode::Hosted` (a Standalone engine must never have `initialise()` called on it in a test — that opens real hardware) and the graph is clocked through `prepareForHost()` / `processHostBlock()`, which funnel into the same `renderNextBlock()` the standalone device callback uses.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| TransportServiceTest | 21 | `Source/Transport/TransportService.*` in isolation — command FIFO ordering and drop-when-full, play/stop/locate/loop/BPM/time-signature, `BlockTimeInfo` per block, loop wrap (including the exact-boundary and tiny-loop cases), beats-are-canonical behaviour across BPM and sample-rate changes, `getPositionSnapshot()` / `juce::AudioPlayHead` reads staying consistent, including under concurrent readers |
| AudioEngineTransportTest | 6 | `Tests/Engine/AudioEngineTransportTests.cpp` — the transport's wiring inside `AudioEngine`: exactly one `tick()` per rendered block in both host modes, a stopped transport freezing the position, `prepareForHost` handing the host sample rate to the transport, the playhead installed once on the graph and re-applied by JUCE to every node (including nodes re-created by an undo restore), a node reading block-start position through `getPlayHead()`, latency reported not compensated |
| OfflineTransportDriverTest | 9 | `Tests/Engine/OfflineTransportDriverTests.cpp` — `synth::OfflineTransportDriver`: `renderBlocks(n)` returns exactly n×blockSize samples and advances the transport by one block per block; `renderToBeat` renders whole blocks until `endPpq` reaches the target (from beat 0 that is exactly `ceil(sampleFromBeat(beat) / blockSize) × blockSize` samples, overshooting by < 1 block) and returns an empty buffer without spinning when the transport is stopped or the target is behind the playhead; the per-block callback sees consecutive `BlockTimeInfo` (block start samples 0, 512, 1024 …); rendered audio is finite and in range; a free-running oscillator patched to Audio Output is audible **whether or not the transport is playing** — the transport is a conductor, not the engine. `renderBlocks`/`renderToBeat` delegate to the non-accumulating `streamBlocks`/`streamToBeat`, so every assertion here covers both shapes; the streaming pair's own consumer is `BounceExporterTest`; the optional pre-block `BlockGate` ends either stream* loop **without rendering the refused block** (the transport's position proves it never ran) and no gate means every requested block |
| BounceExporterTest | 13 | `Tests/Engine/BounceExporter/` — the offline bounce. Everything asserts on the **file**, opened with JUCE's own `WavAudioFormat` reader, using the same RMS windows and guard bands `TimelineE2ETest` uses on the in-memory render (the patch is duplicated from it, plus an optional fully-wet Delay for the tail): a `[0,8]`-beat bounce is exactly `ceil(sampleFromBeat(8) / blockSize) × blockSize` = 375 × 512 samples with the requested rate/depth/channels, and `bitDepth 32` really is an IEEE-float WAV; energy sits exactly inside each note window and each gap is silent; a 1 s tail is still ringing where the range ended and decays across it (RMS of the first 100 ms > the last 100 ms) while `tailSeconds = 0` stops dead at the range with the same Delay still ringing; two fixtures bounced once each are **byte-identical**; a bounce taken from a playing transport at beat 3 leaves the playhead at beat 3, **stopped**, the engine back on its previous prepare format and Track In still audible on the next live render; cancelling from the progress callback leaves neither the target nor a stray temp file and still restores the transport; a nonexistent destination directory fails with a message, writes nothing, creates no directories and leaves the engine renderable; invalid options (backwards range, 20-bit depth, zero sample rate) are rejected before anything is written; progress is non-decreasing, ≤ 1 and lands on exactly 1.0 over a range + tail render. Three more cover the parts a synth-only patch cannot reach: a bounced **Track Audio** clip is bit-exact against its 32-bit float asset from its **first frame** with `streamDropouts == 0` (with the prefetch thread paused, so the bounce's own `waitUntilPrimed` call is the only thing that ever fills a ring), the same holds for a range starting two beats *into* a clip with the **real** prefetch thread running, and cancelling on the first tail block writes exactly range + 1 blocks |
| StemExportTest | 11 | `Tests/Engine/StemExportTests.cpp` — offline stem export (P9-8, `docs/mixer.md` §5.12): N strips produce N files named `"NN - <name>.<ext>"`, all the same length; THE correctness check — `sum(stems)` reproduces the pre-Master mix, proven against a **non-unity** Master gain (+6 dB) against a normal `BounceExporterTest`-style bounce of the same patch, since a post-Master tap would fail by exactly that gain factor, and separately with a non-zero tail so `StemSession::stepTail` is exercised too (every stem grows by the tail's block count and the sum property holds through it); non-default strip gain/pan land in each stem exactly (proving the tap is post-fader); a muted strip's file is silent while the sum property still holds; a soloed strip during export still writes every strip (non-soloed ones silent) and leaves solo/mute state unchanged; a no-strips patch fails with a clear message before touching the destination; cancellation and an unwritable/blocked destination leave no stem files, never touch a pre-existing file, and disarm every `ChannelStripModule` tap; two module-level tests pin the tap's exact post-fader/muted/solo-gated output directly |
| TimelineE2ETest | 6 | `Tests/Timeline/TimelineE2ETests.cpp` — the clip → Track In → Poly MIDI → Oscillator/VCA → Audio Output regression net every later scheduling change must keep tripping: rendered RMS is high exactly inside each timeline note's window and silent exactly inside each gap (guard bands ±2400/4800 samples absorb the ADSR-free chain's only shaping, PolyMidi's 5 ms gate smoothing); a stopped transport, a muted track and a bypassed Track In all render full silence; a looped range re-fires the same note every pass; the note's audio frequency (not just its gate) measures 440 Hz end to end |
| AutomationApplierTest | 10 | `Tests/Timeline/AutomationApplierTests.cpp` — the engine-level net for a `TimelineDoc` lane reaching a live `juce::RangedAudioParameter`: a ramp tracked block by block against the transport's beat position; a stopped transport leaving the knob untouched; deleting the bound node mid-render staying safe (the binding's refcounted `Node::Ptr`) and dropping out on the next `publishTimeline`; unresolvable lanes (unknown uuid, unknown paramID) producing no binding without poisoning the resolvable ones; a lane authored against a wider range clamping at the parameter's endpoints; 100 publishes interleaved with render passes swapping tables cleanly and reclaiming retirees. Plus three record-mode branches driven against a hand-built binding table in `Tests/Timeline/AutomationRecordTests.cpp` (no engine needed): an `Off` lane never writing, a `Write` lane reading like `Read` until global record is armed and going silent once it is, and a `Touch` lane yielding to a claimed parameter and resuming the block after the claim is released |
| AutomationRecordTest | 15 | `Tests/Timeline/AutomationRecordTests.cpp` — the capture half. Mostly headless (bare `TransportService` + a standalone `FilterModule` for a real parameter): a `Touch` gesture committing as **exactly one** undo step whose undo restores the lane byte for byte; the programmatic-write guard (`setValueNotifyingHost` with no gesture captures nothing, on an armed and rolling `Touch` lane) and `ScopedProgrammaticApply` suppressing even a fully gestured write without latching; RDP thinning collapsing 100 collinear points to 2 and keeping a triangle's corner at its **exact** captured value; `Write` overwriting its span, closing with a terminal anchor and auto-dropping to `Touch` on stop *without* undo re-arming it; `Latch` still capturing after the gesture ends; an empty `Touch` gesture being a total no-op vs. an empty `Write` span writing flat anchors; ring overflow raising the flag and still committing; `recordMode` round-tripping through `toVar`/`fromVar` (absent ⇒ `Read`, out of range ⇒ rejected) and reaching `TimelineSnapshot::LaneInfo`. Two hosted-engine tests: `ApplierRespectsClaims` and `RecorderNeverHearsTheApplier` (200 blocks of playback into an armed lane capture nothing) |
| AutomationSlicingTest | 5 | `Tests/Timeline/AutomationSlicingTests.cpp` — the control-rate slicing flag. The flag defaults off; with it on, automation leads the unsliced value by exactly `slope × (blockSize − 64) / samplesPerBeat` at the end of every block (it really does slice); a per-sample chain (Osc → Filter → VCA, DC from a Macro bank into the VCA's CV) renders **bit-identical** audio either way; a Chorus with an LFO on its block-rate Rate CV does **not** — that test asserts only finiteness/audibility and *reports* the measured difference, which is the documented reason the flag ships off; plus a cost tripwire over a 49-node patch that fails only on a >4× blow-up |

**Writing an engine-level timeline test:** use `synth::OfflineTransportDriver` rather than hand-rolling a `processHostBlock` loop. Construct the engine `Hosted`, `initialise()` it, wire whatever the patch needs (before the driver is constructed — its constructor calls `prepareForHost`, which prepares the nodes), then `renderBlocks` / `renderToBeat` and assert on the returned buffer or on the `BlockTimeInfo` stream the block callback hands you. At 48000 Hz / 120 BPM one beat is exactly 24000 samples, which keeps the expected sample counts integers. The default patch may legitimately be silent with no MIDI input, so a non-silence assertion needs a source that runs without MIDI (an `OscillatorModule` is a drone in mono mode) patched to the graph's Audio Output node.

### Audio input / device state tests (11 tests)

`Tests/Engine/AudioInputTests.cpp`. The first tests in the repo that drive `AudioEngine`'s **device-callback** half (before them, no test did — see the apology in `StatusBarTests.cpp`'s `MasterMute_ZeroesOutput`). The fake device itself lives in `Tests/FakeAudioIODevice.h` so the following suite drives the same one.

**The `FakeAudioIODevice` pattern.** A test-local `juce::AudioIODevice` subclass that opens nothing, **starts no thread** (`start()` ignores the callback it is handed) and reports fixed numbers — name "Fake", type "Test", 48000 Hz / 512 samples, settable active input/output channel `BigInteger`s, latency 64 in / 128 out. The test then calls `engine.audioDeviceAboutToStart(&fake)` and `engine.audioDeviceIOCallbackWithContext(...)` **by hand**, in the order a real device would, with synthetic input arrays it can assert against — so there is exactly one thread and every block lands where the test put it. Extend this fake rather than writing a second one. The engine is `HostMode::Standalone` here (that is the mode with a device callback) but `initialise()` is **never** called on it — that is still the rule; the two tests that do exercise `initialise()` subclass the engine and override the `initialiseDevices` seam, which is the only part of it that touches hardware.

| What it covers | |
|-------|-------|
| `InputReachesTheGraph` | device input ch0 (a ramp) arrives at an `AudioGraphIOProcessor(audioInputNode)` and passes through to Audio Output unchanged, while an unconnected output channel renders **silent** rather than leaking the input channel that aliases it |
| `InputCountZeroBehavesAsToday` | a null input array + 0 channels renders the same patch **sample-for-sample** as the hosted `processHostBlock` reference, and dereferences nothing |
| `MoreInputsThanOutputs` | 2 in / 1 out: input ch1 survives the scratch round trip (it is the only thing connected to the single output, so the assertion can't pass without it) |
| `CallbackNeverAllocates…` | the channel-pointer array and scratch are sized in `audioDeviceAboutToStart` — on `max(in, out)`, for a whole device block — via `getDeviceScratchInfo()` |
| `CallbackBeforePrepareStillSilencesTheOutput` | the belt-and-braces path: with no scratch to render through, the callback silences the device's output rather than replaying what was in the block |
| `DeviceStateChangeReachesTheOwnerCallback` | a device-manager change notification reaches `onDeviceStateChanged` exactly once; another broadcaster doesn't; a Hosted engine never persists |
| `SavedDeviceStateSelectsTheRestorePath` | through the `initialiseDevices` seam: no saved state ⇒ legacy defaults path, saved state ⇒ restore path, Hosted ⇒ no device acquisition at all, and the seam leaves the engine unattached |
| `InputLatencyAccessor` | the fake's 64 samples after `audioDeviceAboutToStart`, back to 0 after `audioDeviceStopped`, 0 in Hosted mode |
| `MainComponentDeviceStateTest` (3) | the owner half: the persist callback writes `"audioDeviceState"` (and a null payload writes nothing), and a stored string is parsed and handed to the engine before `initialise()`. Uses the AppProperties isolation pattern below, snapshotting and restoring that one key so a developer's real device choice survives a test run |

### Audio Input module tests (11 tests)

`Tests/Modules/AudioInputModuleTests.cpp` — the module that replaced the graph's raw `audioInputNode`. Two layers: **module-level** tests drive an `AudioInputModule` directly with a bare `synth::TransportService` on its playhead (exactly what the engine does per block, minus the engine — one of them, `NoTransportRendersSilence`, is the no-playhead caveat described in [`architecture.md § AudioEngine`](architecture.md#1-audioengine)), and **engine-level** tests drive the whole path through a real `AudioEngine`, exercising the playhead the engine itself installs.

| What it covers | |
|-------|-------|
| `VisiblePortsFollowDevice` | 0 device channels ⇒ 1 jack (the floor), 2 ⇒ 2, more than `kMaxChannels` ⇒ clamped to 8, and shrinking back shrinks the jack count; the raw **channel** count stays 8 throughout |
| `HiddenChannelsCleared` | a 2-in device leaves channels 2..7 silent every block, and channels 0/1 carry their own input (each channel a differently scaled ramp, so a swap is visible in the value) |
| `BypassClears` | the pure-source exception — bypass clears instead of passing dry through — plus "there is no `muted` parameter by design" |
| `NoTransportRendersSilence` | no playhead (a foreign host, a bare unit test) ⇒ silence and a one-jack card, never the buffer's previous contents |
| `DeviceInputFlowsThroughModule` | standalone, by-hand device callback: input ch1 → output ch0 **crossed**, so the assertion cannot pass on the callback's own channel-for-channel copy-in; one block also teaches the module the device's width |
| `HostedInputFlows` | hosted `processHostBlock` on one in/out buffer: the module emits the host's ORIGINAL input even though the graph renders over that buffer — the input-snapshot pin |
| `DefaultPatchUsesTheModule` | the default patch's "Audio Input" node is an `AudioInputModule`, not an `AudioGraphIOProcessor` |
| `FactoryBuildsTheModule` | factory key, display name, `getFactoryTypeName` and the numbered-suffix fallback all still resolve to "Audio Input" |
| `LegacyPatchLoads` | a patch JSON shaped like an older-format save (same type string, connections on channels 0/1) loads onto the module with both wires intact and `graphToJSON` round-trips the same name |
| `SingletonRuleStillHolds` | `isSingletonIOModule` / `graphHasModuleNamed` still recognise it, a second drop is a no-op, and `extractSnippet` refuses to capture it |
| `DeviceShrinkDropsHiddenRoutings` | 8-in device ⇒ 8 jacks and a cable on ch3 survives; switch to a 2-in device + `refreshIoModulesAfterDeviceChange()` ⇒ ch3's routing is dropped, ch0/ch1 keep theirs, and the card gets shorter |

### Rec Tap / audio recording tests (13 tests)

`Tests/Timeline/RecordTapTests.cpp`. Three layers. **Module-level** tests drive a `RecordTapModule` directly (no engine, no device, no graph), exactly the way `MidiRecorderTests.cpp` drives `synth::MidiRecorder`. **Registration** tests are the internal-only checklist Track In established. **Flow** tests drive `MainComponent` with the device callback suspended, the transport ticked by hand and the tap's `processBlock` called directly. The last two groups exercise the factory entry and the record wiring, both unconditional, always-compiled code.

| What it covers | |
|-------|-------|
| `PassThroughAlways` | armed or not, the output is the input **bit-exactly** — the tap is transparent |
| `CaptureWritesExactWav` | 8 × 512 frames of an exactly-representable ramp: the WAV parses with JUCE's own reader at 48 kHz / 32-bit / IEEE-float / 2 ch, is exactly 4096 frames, and compares **equal sample for sample**; the `.agpk` sidecar's magic/version/bucketSize/channels parse, the bucket count is `ceil(4096/256)`, and every bucket's per-channel min/max is the ramp's first/last frame (ch1 is ch0 negated, so a channel mix-up cannot pass) |
| `PartialFinalPeakBucketCoversOnlyTheFramesThatExist` | a 300-frame take is 2 buckets, the second covering `[256, 300)` — short, never padded |
| `OverrunSetsFlagAndKeepsAudioThreadClean` | a 64-frame ring against 512-frame blocks (deterministic, not a race): the flag is set, the audio still passes through untouched, the take is SHORT but finalised, and `lengthSamples` equals the WAV's real length |
| `BypassPassesDryAndStopsCapturePushes` | bypass passes the dry signal (this module has a dry path — it does **not** clear) and records nothing; un-bypassing resumes on the very next block |
| `StopWithoutStartIsSafe` / `DoubleStartRejected` | stop with no take, a double stop, and processing while disarmed are all inert; a second `startCapture` is refused and leaves the other file untouched; a bad rate/channel count arms nothing |
| `DestructorFinalisesAnInFlightTake` | destroying the module mid-take closes the WAV and writes the sidecar rather than abandoning a half-written file |
| `RegisteredButInternalOnly` / `AbsentFromTheLibraryWithAPinnedSizeEstimate` | factory key, module type, channel counts, `getFactoryTypeName`, non-authorable, Utility cable colour; absent from the library, with `estimateModuleSize("Rec Tap")` measured against the real card |
| `RecordFlowCreatesAudioClip` | bundle-saved project + armed Audio track: Record-on splices exactly one tap between Reverb and Audio Output (both channels re-routed, the direct connections gone) as **one** undo step; capture starts on the poll, not the click; rolling 8 blocks and Record-off produces **one** clip with `assetRef == "Audio/take-1.wav"`, `startBeat` at the punch and a length matching `lengthSamples × bpm / (60 × rate)`; the WAV and `.agpk` exist inside the bundle; a second take re-uses the tap and gets `take-2`; undo removes the clip and **leaves the file** |
| `MidiArmedPathUnchanged` | an armed MIDI track records exactly as the recorder left it — no tap is created at the click or by the poll, and the committed clip has notes and an empty `assetRef` |
| `AudioRecordWithoutAnAudioOutputIsRefused` | no master bus ⇒ refused with a status message, no tap, no clip, and the transport is **not** started |

### Mixer channel tests (32 tests)

`Tests/Mixer/ChannelStripTests.cpp` and `Tests/Mixer/MixerSoloTests.cpp` — the P9-2 `Channel Strip` / `Master` nodes and the solo gate ([`mixer.md`](mixer.md)). Headless. **Module-level** tests (`ChannelStripTest`, `MasterModuleTest`) drive the processors directly, with a bare `synth::TransportService` on the playhead when the gate matters. **Engine-level** `MixerSoloTest` cases build a hosted `AudioEngine` rig (two strips into Master's Mix, one constant source into Direct) and render through it, so the per-block gate is exercised exactly as the engine publishes it. The `MasterSplice*` cases give their bare graph `setPlayConfigDetails(2, 2, ...)` first — without it Audio Output has zero channels and every connection into it is silently refused. Channel macro bypass is covered by `MacroBypassMute.ChannelMacroBypassSkipsSourceAndStripButMuteIncludesTheStrip`.

| What it covers | |
|-------|-------|
| `ChannelStripTest.*Pan*` / `Gain*` | balance-law pan (unity centre, mono feeds both legs, only the far leg attenuates); gain in dB with the -60 dB floor as true silence |
| `ReservedChannelsAreClearedEveryBlock` | ch1..3 between the legs never leak |
| `Bypass*` / `MuteClears` | bypass is dry (a bypassed mono strip still feeds both legs), mute clears — two branches |
| `MeterReportsTheLastBlocksPostFaderPeakWithoutConsumingIt` | the meter is a plain per-block store, readable twice |
| `ShapeIsFixedOnceWritten` / `ShapeLocksOnceTheStripIsLive` / `ExtraStateRoundTripsShapeAndSolo` / `JackMap*` | mono/stereo is fixed for the strip's lifetime; shape + solo round-trip through trusted extra state; the right leg sits on `kRightBase` |
| `MasterModuleTest.*` | Direct summed into Mix before the fader; bypass is a unity sum; four labelled inputs and no Dual I/O toggle |
| `MixerSoloTest` gate cases | a non-soloed strip (bypassed too) and Master's Direct go silent while any strip is soloed; no transport means no gating; solo never writes a mute parameter |
| `DeletingASoloedStripReleasesTheGateAtPublishTimeline` / `UndoRedoAcrossAGraphRebuildSettlesTheGate` | the count is recounted from the graph, so the mix is never stuck silent |
| `MasterSplice*` / `MasterIsASingleton` / `MasterGoesInFrontOfAnExistingRecTap` / `MasterSpliceNeedsAnOutput` | one undo step; strips re-route to Mix, everything else to Direct; at most one Master; spliced ahead of the Rec Tap; refused with no output |

### Channel creation flow tests (29 tests)

`Tests/ChannelFlowTests.cpp` — `Source/Mixer/ChannelFlows/ChannelFlows.h`/`.cpp`'s builders and the three "+ Track"/drag flows that call them ([`mixer.md`](mixer.md) §5.2 / [`mixer_implementation.md`](mixer_implementation.md) item 2: T173a's audio track, T183's instrument track, T184's MIDI-track auto-channel-on-connect). Headless except the `ChannelFlowTest` fixture below, which drives a real `MainComponent` off-screen. `ChannelFlowAutoChannelCore` cases build a bare `AudioEngine`/graph directly (`graph.setPlayConfigDetails(0, 2, 44100.0, 512)` before adding an "Audio Output" node, mirroring `MixerSoloTests.cpp`'s rig, since `AudioEngine`'s default constructor has no IO yet); `ChannelFlowTest` cases go through `MainComponent::newPatchForTest()` for a real, empty-but-seeded graph and drive `ModuleComponent::mouseDown/mouseDrag/mouseUp` with synthesized `juce::MouseEvent`s for the drag gestures (see "Test the real mouse path" below) rather than `GraphEditor`'s own drag API. The P9-3d "Make channel" cases (FRO25, [`mixer.md`](mixer.md) §5.8 / [`mixer_implementation.md`](mixer_implementation.md) item 2) add a render-identity rig — `HostedPatchCFT`, a `HostMode::Hosted` engine rendered offline through `processHostBlock`, built twice from the same legacy patch so a standalone `GraphEditor` can convert one and the two renders be compared sample for sample (`ChannelFlowMakeChannelCore`) — and drive the track header, canvas and module-card right-click menus through their `setShowContextMenuHookForTest`/`setShowCanvasContextMenuHookForTest` seams (`ChannelFlowTest`).

| What it covers | |
|-------|-------|
| `buildDefaultAudioChannel`/`buildChannelForFeeds` cases | EQ(bypassed) -> Compressor(bypassed) -> Strip(Stereo) -> Master chain wiring; a poly instrument gets a Voice Mixer ahead of the strip (`PolyInstrumentGetsVoiceMixerAheadOfStripAndFeedsTheChannel`); latency stays zero after `prepareToPlay` (`BypassedEQAndCompressorReportZeroLatencyAfterPrepare` — a gate, not a tautology: neither module calls `setLatencySamples`) |
| `findUnchanneledOutputFeeds` (T184) | a straight instrument-to-output path is 2 exits (`OnInstrumentToOutputFindsTwoExits`); an already-`ChannelStrip`-ed path is 0 (`OnStripChanneledInstrumentFindsZero`); a dead-end path reaching nothing is 0 (`NotReachingOutputFindsZero`); a hidden `AttenuverterModule` mod/CV leg is neither traversed nor counted, and does not disturb an unrelated instrument's own path (`IgnoresModulationBranchAndLeavesUnrelatedPathUntouched`) |
| `buildChannelForFeeds` exit handling (T184) | exit edges are removed before the chain is built, and wired through to a freshly spliced Master (`RemovesExitEdgesAndWiresThroughToANewMaster`) or an existing one, clearing prior Direct feeds (`ReusesAnExistingMasterAndClearsDirectFeeds`) |
| `ChannelFlowTest.AutoChannelOnConnect_*` (T184, real mouse drag) | toggle ON builds exactly one channel as one undo step (`ToggleOnBuildsOneChannelAsOneUndoStep`); toggle OFF only makes the connection, byte-identical to pre-T184 behaviour (`ToggleOffOnlyConnectsNoChannel`); an instrument that already has a channel gets nothing new when a second Track In connects (`AlreadyChanneledInstrumentGetsNoNewStrip`); the new EQ/Compressor/Strip join the instrument's existing macro when boxing applies (`NewChainNodesJoinTheInstrumentsExistingMacro`) |

**`ChannelFlowTest` file layout.** The suite is split by topic under `Tests/Mixer/ChannelFlow/`, all sharing the `ChannelFlowTest` fixture, `MockProviderCFT`, the plugin-scan stub backend and the FRO25 rig helpers in `Tests/Mixer/ChannelFlow/ChannelFlowTestFixture.h` (header-only, not built on its own):

| File | Covers |
|------|--------|
| `ChannelFlowTests.cpp` | T173a: "+ Track -> Audio Track" builds a whole mixer channel in one undo step |
| `ChannelFlowInstrumentTests.cpp` | T183: "+ Track -> Instrument -> {Oscillator/Wavetable/Sampler}", the MIDI-track mirror of the audio-track flow |
| `ChannelFlowPluginInstrumentTests.cpp` | FRO42 (P9-3h): a hosted plugin as the instrument, plus the review-fix regressions (menu snapshot resolution, format-label disambiguation, self-exclusion) |
| `ChannelFlowAutoChannelTests.cpp` | T184 (P9-3c): auto-create channel on MIDI connect — core-level `findUnchanneledOutputFeeds`/`buildChannelForFeeds`, then real-mouse-gesture coverage through `GraphEditor::endConnectionDrag` |
| `ChannelFlowCreateChannelsTests.cpp` | FRO26 (P9-3e): "Create Channels" for existing projects, wrapping every channel-less track's chain as one undo step |
| `ChannelFlowMakeChannelCoreTests.cpp` | FRO25 (P9-3d): "Make channel" core behaviour through a standalone `GraphEditor` (render-identity comparisons), plus the adversarial no-double-drive / three-way-merge / chained-merge probes |
| `ChannelFlowMakeChannelAppTests.cpp` | FRO25: the real app wiring — track header, canvas selection and module right-click menus, each as one undo step |

### Mixer panel tests (FRO11/P9-5)

`Tests/Mixer/MixerModel/` — headless column-ordering logic (`mixer.md` §5.10), pure `Source/Mixer/MixerModel/` code. `Tests/UI/Mixer/` — the panel UI, on a real off-screen `MainComponent` (`newPatchForTest()` + `simulateAddAudioTrackClick()`, the `ChannelFlowTest` rig's style):

| File | Covers |
|------|--------|
| `MixerDockComponentTests.cpp` | tab strip switches without closing the dock; `toggleMixerPanel` (Cmd+Alt+M) opens on Mixer then closes on a second press; actionId round-trips to `AppCommands::toggleMixerPanel`; active tab persists across an `ApplicationProperties` reload |
| `MixerPanelComponentTests.cpp` | one column per strip plus Direct plus Master; themed PNG render smoke test (Obsidian + Daylight, see the `createComponentSnapshot` pattern above); clicking a column selects its owning macro |
| `MixerFaderTests.cpp` | the fader's `SliderParameterAttachment` binding and dB readout, plus a regression test for a `MixerFader::parameterValueChanged` use-after-free (a `callAsync` lambda captured raw `this`; fixed with `SafePointer`) |

Tests calling `dock.setActiveTab(...)` use `MixerDockActiveTabResetGuardMDT` (see the reset-guard
pattern above). Also updated (not new suites): every `Tests/UI/Timeline/TimelinePanel/*Tests.cpp`,
`PanelAnimationAndLoadingTests.cpp` and `TimelinePlayheadTests.cpp` case asserting the timeline
panel's own bounds/visibility, now that `TimelinePanelComponent` nests inside `MixerDockComponent`
instead of being `MainComponent`'s direct child. `TimelinePanelTestFixture.h` gained
`timelinePanelBoundsInMainComponent(mc)` (`getLocalArea`, bounds now dock-relative) and
`timelinePanelIsOpen(mc)` (`isVisible() && mixerDock.isVisible()`; `isShowing()` needs a real
Desktop peer, unavailable headless).

### Audio clip playback tests (24 tests)

`Tests/Timeline/AudioClipPlaybackTests.cpp`. Five layers. Playback tests render through `synth::OfflineTransportDriver` exactly the way `TimelineE2ETests.cpp` does, but assert **bit-exact sample content** rather than RMS windows, which two things make possible: the test WAV is 32-bit IEEE float carrying exactly-representable values (`n / 65536`), and the streamer's prefetch thread is **paused** (`setPrefetchPausedForTest`) and driven by `pumpForTest()` from the render loop's per-block callback. There is no sleep and no "eventually the ring fills" wait anywhere in the file.

| What it covers | |
|-------|-------|
| `AudioClipSnapshotTest.*` | an Audio track flattens an `audioClips` run and **no** notes (and a MIDI track the reverse); runs sorted by `startBeat`; `gainDb` converted to linear once; a 400-char ref truncated but still NUL-terminated; a soloed **audio** track now sets `anySoloed` |
| `UnresolvableEscapingRefRefused` | with a real file sitting just outside the root, `../…`, `Audio/../../…`, an absolute path and `/etc/passwd` all resolve to nothing, while a legitimate ref still resolves — the streamer never opens outside its roots |
| `RecordingsRefResolvesAgainstTheRecordingsRoot` | the `Recordings/take-1.wav` form resolves against the folder containing `Recordings/`; with no bundle root a bundle-relative ref resolves to nothing rather than falling back |
| `RamStaysBounded` | a **60-second** take: resident capacity is `kRingFrames`, never the file's length, and playing the whole minute in one-second hops does not move `getTotalResidentBytes()` off one ring |
| `PoolCapDropsExcessClipsGracefully` | 33 clips ⇒ exactly 32 stream, the last (document order) is silent, and reading through an absent handle zero-fills instead of crashing |
| `MonoFileIsUpmixedToStereo` | a mono asset fills **both** ring channels, not one and a zero |
| `ClipPlaysWhereItSits` | a clip on beats [2, 4): content equals the source segment sample for sample, silence outside it is **exact**, and the right channel carries the file's right channel |
| `SourceOffsetHonoured` | `sourceStartSeconds = 1.0` ⇒ the block matches `file[48000 …]` |
| `GainAndFadesShapeTheEnvelope` | a DC source, so the rendered sample **is** the envelope: `-6 dB` × a 1-beat linear fade-in × a 2-beat fade-out matches at every sample, with the three landmarks named individually |
| `SeekIntoTheMiddlePlaysFromTheRightFrame` | locate past the pre-filled window: the un-pumped block is **exactly silent** (a seek gap is silence, never garbage), and one pump later playback resumes at the right frame |
| `LoopWrapReplaysTheClip` | four loop passes reproduce the clip bit-exactly on every pass |
| `OverlappingClipsSum` | two clips at the same beat from the same asset ⇒ two streams, output exactly doubled |
| `MissingAssetIsSilentNoCrash` | a well-formed but non-existent ref: no stream, exact silence, no crash |
| `MutedTrackSilent` / `SoloElsewhereSilencesThisTrack` / `BypassClears` / `StoppedSilent` / `UnboundNodePlaysNothing` | the five ways a Track Audio node legitimately renders nothing |
| `RegisteredButInternalOnly` / `AbsentFromTheLibraryWithAPinnedSizeEstimate` | the internal-only checklist: factory key, module type, 0-in/2-out, `getFactoryTypeName`, non-authorable, **Sources** cable colour; absent from the library, with `estimateModuleSize("Track Audio")` measured against the real card |
| `AddAudioTrackFlowTest.AddAudioTrackFlow` | the "+ Track" menu's Audio entry: **one** undo step creating a uuid'd `Track Audio` node wired `0→0`/`1→1` into Audio Output plus an Audio-kind track bound to it; undo removes both |
| `AddAudioTrackFlowTest.AddMidiTrackFromTheSameMenuIsUnchanged` | the MIDI entry still does exactly what it did before the menu existed |

### Hosted plugin tests (15 tests)

`Tests/Plugin/HostedPluginTests.cpp`. Not gated: hosting is independent of the timeline. **No third-party binary is involved.** `Tests/StubPluginInstance.h` supplies a `juce::AudioPluginInstance` subclass (constructor-set channel counts, a ×0.5 gain marker, a round-trippable state blob, a settable latency, and a record of the thread its destructor ran on) plus a `StubBackend` that resolves any identity to it. The stub matches the real formats' threading — `MessageManager::callAsync`, never re-entrant — so every test pumps the message loop with the bounded-poll idiom from `AccountServiceTests`.

| What it covers | |
|-------|-------|
| `PassThroughUntilReady` / `BareModuleShowsOneJackASide` | with no instance the output **is** the input, on all 16 channels; bypass takes the same dry branch (it must not clear) and mute does clear; a bare module shows one jack a side over 16 real channels |
| `AsyncLoadPublishesAndProcesses` | the callback does not fire re-entrantly; after pumping, the ×0.5 marker is on channels 0–1, the visible ports are the instance's **real** 2/2 while `getTotalNumOutputChannels()` stays 16, the instance was prepared to **our** rate/block, and every hidden channel is silent |
| `InstrumentWithNoInputsGetsItsOutputChannelsCleared` | a 0-in/2-out instrument reports 0 input jacks, and its output-only channels arrive cleared so upstream audio cannot leak through as if bypassed |
| `OverMaxRefusedWithMessage` | a 32-channel instance is **refused, not truncated**: no instance, a status message naming both 32 and 16, and the dry path still intact |
| `UnresolvedIdentityStaysAPlaceholderThatRemembersItsPlugin` | "not installed on this machine": the identity survives, the message names the plugin, and `getExtraState` still serializes it so re-saving does not destroy it (the placeholder's foundation) |
| `StateRoundTrip` | load → set a blob → `graphToJSON` → **trusted** `applyJSONToGraph` into a fresh graph with a `ScopedDefault` stub backend → the instance is restored with the same blob. Also asserts the serialized patch contains **no path** — the stub deliberately reports a `fileOrIdentifier`, so this tests something |
| `UntrustedCannotAuthorIt` / `UntrustedApplyNeverReachesSetExtraStateOnAnExistingNode` | the pin: `validatePatch(trusted=false)` rejects the type with `InternalModuleNotAllowed`, with or without a state blob attached, and apply refuses the whole patch; a merge patch aimed at a **live** hosted node cannot repoint it either |
| `AudioThreadNeverFrees` | two instance swaps and an unload while a second thread renders continuously: retired instances are reaped, every destructor ran on the message thread, and none ran on the render thread |
| `PrepareToPlayPropagates` / `LatencyIsPublishedToTheGraph` | a rate/block change re-prepares the **live** instance in place without retiring it, and a plugin loaded afterwards gets the new format; the instance's latency becomes the module's, and unload returns it to 0 |
| `RegisteredButInternalOnly` / `AbsentFromTheLibraryWithAPinnedSizeEstimate` | the internal-only checklist: factory key, module type, **16/16** channels, `getFactoryTypeName`, non-authorable, Utility cable colour, void `getExtraState` when bare; absent from the library, with `estimateModuleSize("Hosted Plugin")` measured against the real card |
| `IdentitySerializationCarriesNoPath` | `PluginIdentity` round-trips through `var`, carries no path, and matches uid-first — a rename still matches, a different format does not |

Three sibling files share `StubPluginInstance.h` and the same message-pump idiom: `HostedPluginEditorWindowTests.cpp` (editor windows, headless, state not pixels), `HostedPluginLaneTests.cpp` (a hosted parameter as an automation lane), and `HostedPluginLatencyTests.cpp`. The last one's acceptance test is worth knowing about: it splits an impulse down a **dry** path and a **hosted-plugin** path into Audio Output on a real `AudioEngine` graph, and asserts both copies land on the same output sample — off-by-zero, at two different latencies, across a `rebuild()`. That only tests anything because the stub genuinely delays its audio by the latency it reports; `WithoutARebuildTheParallelPathsDrift` is the negative control that fails if the rebuild is ever removed.

### Integration Tests (~38 tests)

Test module interactions within the audio graph and cross-system integrations.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| IntegrationTest | 7 | Signal chain routing (Osc->Filter->VCA), LFO->Filter modulation, preset loading with graph structure validation |
| ModMatrixTest | 22 | Add/remove mod routings, amount scaling, channel mapping, modulation chains; Phase 4 adds: `RowHeightIs48` — `kRowHeight == 48`; `ZebraAlternates` — `isZebraRow` false for even, true for odd indices; `HoverStateUpdates` — `setHoveredRow`/`getHoveredRow` round-trip, default -1, redundant-set no-op, reset to -1; `ModMatrixComponentPaintSmokeTest` — paint with two routings, no crash; `HoverResetsAfterRowRemoval` — hover clears to -1 when a routing is removed (componentsChanged path) |
| OllamaProviderTest | 10 | AI LLM HTTP requests, streaming responses, model management, non-blocking discovery; `SendPromptWithNoModelFailsWithoutHittingNetwork` — fail-fast with no network call when `currentModel` is empty; `SendPromptIncludesSelectedModelInRequestBody` — captures the POST body and asserts `"model"` matches `setModel()` (regression lock for f7cba4a / empty-model 400s); `QueuedRequestDuringThreadShutdownStillCompletes` — a request enqueued as the worker winds down still gets a callback (request-loss race); `PendingRequestsAreFailedOnDestruction` — requests still queued at destruction are failed *before* `~OllamaProvider()` returns. Both use bounded `condition_variable` waits and fail on timeout rather than sleeping. **Never call `stopThread(0)` in a test** — it force-kills via `pthread_cancel`, which aborts under glibc |
| AIIntegrationServiceTest | 9 | Module suggestions, parameter recommendations, graph state mapping |

### Component Workflow Tests (~143 tests)

Test UI component interactions using in-process construction (no window, no display).

| Suite | Tests | What it covers |
|-------|-------|----------------|
| MainComponentTest | 27 | AI panel visibility toggle, mod matrix toggle, minimap toggle + button tooltip, default configuration, command manager registration (`CommandManagerHasCommands` is pinned to `ShortcutManager::getActionIds()`, so a bindable action with no command can't ship), redo shortcut, `CopyPasteDuplicateCommandsReachTheCanvas` + `PasteCommandIsInertUntilSomethingHasBeenCopied` (Paste carries `isDisabled` until the clipboard is filled, and `tryToInvoke` refuses an inactive command — that flag, not `perform()`, is what stops a stray Cmd+V); toolbar narrow/wide mode at 480/1600 px, library sidebar toggle + persistence, AI panel persistence, status bar bounds, canvas non-zero at minimum size, timer gating (5 Hz), patch name default + update on preset load, DrawableButton header buttons; `AiProviderGetsModelSelectedOnStartup` — regression lock (f7cba4a) that a model is selected on startup via `aiChatComponent.refreshModels()` called AFTER `setProvider()` |
| AIChatComponentTest | 3 | Initialization/resizing, send-message updates UI + history via mock provider; `RefreshModelsSelectsModelWhenProviderInstalledAfterConstruction` — reproduces MainComponent's member-init ordering (chat component constructed before a provider is installed), asserting `getCurrentModel()` stays empty until a post-construction `setProvider()` + `refreshModels()` |
| SelectionModelTests | 22 | Multi-select primitives (issue #156, `Source/UI/Graph/SelectionModel.h` — pure, no GUI). `SelectionModel` add/remove/toggle/setSelection/clear; `NodeID{0}` rejected as the graph's invalid sentinel; `getSelected()` ordered by uid regardless of insertion order (snippet node order must not depend on click order); `retainOnly` staleness pruning. `marqueeRectFrom` normalises a drag in all four directions. `hitTestMarquee` uses intersection not containment (a clipped edge selects), degenerate band selects nothing, invalid box ids skipped. `unionSelection` for the additive marquee |
| MultiSelectTests | 31 | GraphEditor-level multi-select (issue #156). Selection API (replace vs additive toggle, select-all, clear, prune after a node is removed behind the editor's back). Marquee: touches-to-select, replace vs add semantics, shrinking the band deselects, band over empty canvas deselects, update-without-begin is a no-op. Group drag: followers move by the initiator's delta, `finalizeSelectionDrag` preserves relative layout and snaps the *group* box to the grid, single selection does not engage group drag, `cancelSelectionDrag` leaves off-grid positions untouched, follower positions reach node properties. `deleteSelection` is ONE undo step for the whole group. Snippet drop through `itemDropped` with a `snippet:` payload; plain module drops still work; canvas Delete/Backspace/Escape keys return `false` when nothing is selected so they fall through |
| ClipboardTests | 32 | Copy / paste / duplicate for a multi-module selection (`Source/UI/Graph/ModuleClipboard.h` + the `GraphEditor` actions). **Clipboard state (pure):** empty by default, a payload with zero `nodes` still counts as empty (so Paste can't be offered for a selection of nothing but graph I/O), `kOffsetStep` is a multiple of the layout grid, each `nextPastePosition()` steps one offset further, a new copy restarts the cascade and `anchorAt` re-aims it. **Copy:** refused with nothing selected; an ineligible-only selection (Attenuverters) is refused *without* wiping what was already on the clipboard; the payload is a snapshot, so it still pastes after the originals are deleted; parameter values survive (a 0.5 Hz LFO rate) and so does non-parameter state (`…SoADuplicatedSamplerKeepsItsSample`). **Paste — wiring, the point of the feature:** `RewiresInternalConnectionsBetweenTheCopiesNotBackToTheOriginals` (copy↔copy wired, original↔original untouched, no wire crossing between them), `DropsConnectionsThatLeftTheCopiedSelection` (a half-selected wire is not recreated — the copy arrives with no wires at all), `DoesNotSpliceTheCopiesIntoTheSurroundingPatch` (merge-mode auto-connect stays off, so a pasted module never wires itself to Audio Output), `RebuildsModulationChainsBetweenTheCopies` (attenuverter count 1→2; the attenuverter never becomes a clipboard node). **Paste — placement:** no-op on an empty clipboard, copies left selected and originals not, relative layout preserved, lands one offset from the original rather than on top of it, repeated pastes cascade by exactly one step each, `pasteClipboardAt` snaps an off-grid point and re-anchors the cascade, ONE undo step for the whole group. **Duplicate:** no-op with nothing selected, offset copy with its own internal wiring, clipboard left untouched, works with no prior copy and does not implicitly fill the clipboard, repeats from the *new* selection so a chain walks across the canvas, ONE undo step |
| ModuleLibraryCollapseTests | 30 | Collapsible sidebar sections + Snippets section (issue #156). `DraggableModuleNamesExcludeSnippetsAndThePlaceholder` — `getDraggableModuleNames()` feeds callers that instantiate each name via the module factory, so it filters on `RowKind::Module`; snippet rows and the "No snippets yet" placeholder are visible but are not module types. `HitTestingAgreesWithTheRowLayout` — every row's centre maps back to its own entry (paint and hit-testing share `buildRows()`); rows never overlap; collapse hides a section's rows but keeps its header and shrinks `getTotalContentHeight()`; header click and top-strip click toggle; `toggleAllSections` folds from a partial state rather than unfolding; `onCollapseStateChanged` fires only for user-driven changes, never for the `setCollapsedSections` restore path; blank persisted state expands everything; Snippets section shows an empty hint with no snippets, becomes draggable rows when populated, and sits ahead of the module catalogue |
| ModuleLibraryCollapseAnimationTests | 11 | The 150 ms accordion fold. The tween is VBlank-driven and cannot tick headlessly, so these drive `setSectionProgress()` — the same value the animator writes each frame — and separately assert the snap paths: `CollapsingWhileHiddenSnapsInsteadOfHanging` and `RestoringPersistedStateSnaps` (a component that is not `isShowing()` must land on its final layout, or an off-screen sidebar would freeze mid-fold and every other headless test would see a half-open list). Endpoint parity — progress 1 reproduces the collapsed layout exactly, progress 0 truncates nothing. Band geometry — a half fold shows fewer rows, pulls the next header up by about half the section height, and shrinks `getTotalContentHeight()` monotonically across 0/0.25/0.5/0.75/1. `TheRowStraddlingTheBandEdgeIsTruncatedNotSquashed` — at most one row is clipped and none is emitted at zero height; `RowsFoldedPastTheBandStopHitTesting` — a folded-away row stops answering at its old position |
| ModuleLibraryScrollTests | 14 | Vertical scrolling for the library sidebar. The bar appears only once rows overflow the panel and hides again when they fit (including after `setAllSectionsCollapsed`, which also resets the offset to 0); `MaxScrollOffsetIsExactlyTheOverflow` — scrolled to the bottom the last row is fully reachable, no more and no less; the offset clamps on over/under-scroll and re-clamps when the panel grows; the bar sits below the pinned chrome (search field + COLLAPSE ALL) at the right edge. `MouseWheelScrollsTheRows` drives the real wheel path (component → `ScrollBar` → async listener → offset, so the helper pumps the message queue) and `MouseWheelDoesNothingWhenEverythingFits` guards the no-overflow case. `HitTestingFollowsTheScrollOffset` and `TheSameScreenYHitsDifferentRowsAtDifferentOffsets` guard the two coordinate spaces — `getEntryIndexAtComponentY()` applies the offset, `getEntryIndexAt()` stays content-space so existing `getRowCentreY()` callers are unaffected; `PinnedTopStripIsNeverARowHitWhileScrolled` — rows that scrolled under the search field or COLLAPSE ALL chrome do not steal those clicks |
| ModuleLibrarySearchTests | 23 | Library sidebar search field. `highlightSpansFor` reports each case-insensitive hit (and stays empty for a blank/whitespace query); a trimmed query hides non-matching modules and empty sections, keeps a header-match section's full child list (`"Time"` → Delay + Reverb), and draws matching sections open without rewriting `collapsedSections` or firing `onCollapseStateChanged` (clearing the field restores the fold); snippet names are searchable; the empty-snippets hint hides unless the Snippets header itself matched; no match yields zero rows; `getDraggableModuleNames()` is unaffected; filtering a short list hides the scrollbar and resets the scroll offset; paint is safe with matches and with none; the editor is pinned above COLLAPSE ALL |
| GraphEditorTest | 47 | Module drag-and-drop, port connection via beginConnectionDrag/endConnectionDrag, deletion, mod matrix visibility, **drag-to-knob modulation** — `DroppingACableOnAKnobCreatesAModRouting` drives the whole path (LFO output → Position knob → a routing on that parameter's CV channel) and asserts the drop-target highlight arms on hover and clears on release, `KnobDropIsIgnoredForACableDraggedFromAnInput` (a mod source drives a destination; the reverse would wire it backwards), snap-on-drop (position is grid-multiple of 8), overlap resolution (second drop at same coord produces non-overlapping bounding boxes); `AudioFileDroppedOnCanvasCreatesAPreloadedSampler` — a real wav dropped on empty canvas yields a Sampler already holding it; `NonAudioFileDragIsRejectedByTheCanvas`; **Macro bank resize** — growing 4→16 knobs keeps the bank's top-left fixed, pushes the module below past `kCollisionGap` and persists its new y to node properties; shrinking 16→4 drops the routing on a hidden jack (and its attenuverter) while leaving a still-visible jack's routing intact |
| ModuleComponentTest | 33 | Initialization, resizing, parameter attachment to UI sliders; bypass/mute/delete are DrawableButtons with correct header bounds; delete button triggers requestDeleteModule; `SamplerHasLoadButtonWaveformAndKnownHeight` — the Sampler gets a `SampleWaveformComponent`, a "Load Sample..." button and a "(no sample)" label, at 280×657; `BodyContentClearsEveryPortLabel` — every visible body child starts below the lowest jack (regression guard for the overlap the old duplicated layout formula caused); `EstimatedModuleSizesMatchTheRealComponents` — builds every library-offered type and fails if `GraphEditor::estimateModuleSize` drifts from the real card, so the drag ghost cannot lie; `KnobsAreLaidOutThreePerRow`; `NonSamplerModulesGetNoSamplerChrome`; audio-file drop — `SamplerAcceptsAudioFileDropAndLoadsIt` (highlight on enter, cleared on drop, sample actually loaded), `SamplerIgnoresNonAudioFileDrag`, `NonSamplerModuleRefusesFileDragSoItFallsThroughToTheCanvas`; `WavetableCardBuildsDisplayAndLoadButton` — a Wavetable card owns a `WavetableDisplayComponent` and a "Load Wavetable..." button, both laid out inside the card, with 8 combos / 15 sliders / 2 toggles at `kDoubleWidth`; `WavetableCardKeepsEveryInputJackOnTheLeft` — all 16 jacks land on distinct in-bounds points across exactly two columns, **both on the left half** (inputs-left / outputs-right is what makes signal flow read left to right), outputs keep the right edge, the split is column-major, and no knob overlaps the lowest jack (which is not necessarily the last one); `ModulationRingsSkipKnobsOnInactiveTabPages` — `getModRingSliderIndex` returns -1 for a knob whose page is hidden and a valid index for a pinned one, so a ring cannot paint over empty card after a page switch; `KnobsResolveToTheirCVJackAsModulationDropTargets` — a visible knob reports the input Port of its own CV channel, empty card reports nothing, and a knob on a hidden page must not swallow a drop; `ModulationDropTargetHighlightTracksTheHoveredKnob` — the highlight sets/clears, reports whether it actually changed (so the caller can skip a repaint), and paints without a themed LookAndFeel; `WavetableTabsSwitchContentWithoutResizingTheCard` — the card height is identical on every page (a resizing card would shove its canvas neighbours on each click), Position and Warp Amt stay pinned across all pages, each page shows a different set, everything visible stays inside the card, and **every knob is reachable from some page** — a control on no page is unusable; `WavetableCardBuildsFolderBrowserChrome` — the `Folder...` / `<` / `>` buttons exist, are laid out inside the card, and clicking next with no folder selected is a no-op rather than a crash; `WavetableCardAcceptsAudioFileDrag` — a Wavetable card claims audio-file drops itself (before #180 it returned false and the drop fell through to GraphEditor, which spawned an unrelated Sampler beside it); `WavetableCardPaintsAndTicksWithoutCrashing`; **Macro bank layout** — height equals `macroBankHeight(count)` for 1/4/8/16 knobs, each output jack sits on its own knob row and hit-tests to that macro's index, no input or MIDI jacks, knobs clear the output-label gutter and stay inside the module, rows above the count are hidden |
| MidiKeyboardModuleTest | 4 | Note on/off, key press handling, velocity |
| VisualBufferTest | 3 | Scope visualization buffer management, read/write, ringbuffer behavior |
| ModuleBaseTest | 4 | Parameter getters, port labels, bypass functionality |
| ModuleBypassTest | 5 | Default state, toggle, signal passing when bypassed |
| FXBypassTest | 31 | Per-FX bypass dry pass-through, CV-channel clearing, mute silencing |
| VisualSignalFlowTests | 8 | AttenuverterModule peak/mod value tracking, VisualBuffer RMS computation, AudioEngine::getModulationDisplayInfo() population |
| SettingsWindowTest | 11 | Tab structure (Audio / AI / Keyboard Shortcuts / Preferences / Appearance), tab persistence, audio device selector, AI settings persistence, resize safety, shortcuts reference, Preferences tab hosts behaviour controls |
| PreferencesSettingsTabTests | 50 | Preferences tab defaults (`NewAndUnwired`, double-click disconnect on) and persistence/push-to-`GraphEditor` for every toggle, including the T184 `mixerAutoCreateChannelOnConnect` toggle (default ON, persists `"0"`/`"1"`, pushes to a live `GraphEditor`); the Dual I/O per-module popup (`dualIOCapableModuleTypes()`-derived rows, override-vs-global consumption in `GraphEditor::applyDefaultDualIOForNewModule`); the live search filter (label/tooltip matching, grouped-row visibility, divider collapse, Esc-to-clear via `onEscapeKey`); the "Scroll up to zoom in" checkbox's boolean-key persistence (migration-free across its round 3 relabel, round 5 dropdown detour and round 6 revert) and its OS-derived `platformCommandKeyName()` modifier text; muted-hint-label layout (two-line-tall box, no horizontal squeeze); the tab's own `wantsKeyboardFocus` so its search field cannot steal initial focus; paint/resize smoke |
| ShortcutManagerTest | 11 | Default bindings, reverse lookup, conflict detection, persistence round-trip, reset to defaults, display strings. `CopyPasteDuplicateUseThePlatformStandardKeys` pins Cmd+C/V/D. Two table-wide invariants that stop a new action from shipping broken: `EveryDefaultBindingIsUnique` (a duplicate binding silently shadows one of the two in `getActionForKeyPress`) and `EveryActionIdHasABindingACommandAndADescription` (an action with no `AppCommands` mapping binds a key that does nothing) |

**Poly connection creation coverage.** `GraphEditorTest` covers poly fan-out on drag (dragging a cable between two poly jacks creates all N per-voice connections) and poly-toggle rewire (toggling a module's `poly` parameter re-anchors its existing cables via `rewireForPolyChange`). `Tests/Engine/LogicalPortTests.cpp` adds pure, headless coverage of jack-target resolution — `ModuleBase::getJackTargets` and `GraphEditor::resolvePolyLink`'s pairing/scoring rules — independent of the audio graph.

**`GraphEditorTest`/`SmartConnection*Test` file layout.** The suite is split by topic under `Tests/UI/Graph/GraphEditor/`, all sharing the `GraphEditorTest` fixture and drag/connection helpers in `Tests/UI/Graph/GraphEditor/GraphEditorTestHelpers.h` (header-only, not built on its own):

| File | Covers |
|------|--------|
| `GraphEditorTests.cpp` | Core: init/resize, mod-matrix visibility, module drag-and-drop (incl. Dual I/O defaults, split-block collapse), audio-file drop, drag-to-knob modulation, port-drag connections, Replace Module |
| `GraphEditorLayoutTests.cpp` | Grid-layout / anti-overlap, alignment-guide rendering, Macro Control bank runtime resize |
| `GraphEditorPolyLinkTests.cpp` | Pure `resolvePolyLink` pairing/scoring, plus Dual I/O toggle stereo-leg completeness |
| `GraphEditorDualIOTests.cpp` | Dual I/O split/collapse wiring (migrate-not-duplicate, mid-chain voice modules, one-undo-step) and render-identity checks |
| `GraphEditorIntegrationTests.cpp` | Issue #163 poly connection integration through a full graph |
| `GraphEditorViewportTests.cpp` | Minimap (issue #159) visibility/model, zoom-perf cable memoization and zoom-gesture raster freeze |
| `GraphEditorSmartConnectionTests.cpp` | Smart-connection eligibility (mode, stereo/mono fan, incompatible pairs) |
| `GraphEditorSmartConnectionInsertTests.cpp` | Occupied audio destinations: default parallel-add, Ctrl insert-in-series, stereo fan correctness |
| `GraphEditorSmartConnectionPreviewTests.cpp` | Drop preview, Ctrl gesture plumbing, round-5 regressions |
| `GraphEditorSmartConnectionMatrixTests.cpp` | The three `TestWithParam` matrices: every FX insertable at the gap, vertical aim, and the full gesture-matrix contract table |
| `GraphEditorNodeActionsTests.cpp` | Per-node actions/identity: module title rename, double-click port disconnect (issue #216), output-card identity, Locate Master (FRO45) |

**`MacroContainerTest`/`MacroCollapse`/etc. file layout.** Split by topic under `Tests/Macros/MacroContainer/`, sharing `addModuleAt`/`uuidOf`/`nodeIdForUuid`/`findComponent`/`makeCanvasMouseEvent` in `Tests/Macros/MacroContainer/MacroContainerTestHelpers.h` (header-only, not built on its own):

| File | Covers |
|------|--------|
| `MacroContainerTests.cpp` | Lifecycle: wrap/unwrap, persistence, snippets, delete, membership (incl. port-splice on a newly crossing cable) |
| `MacroContainerGeometryTests.cpp` | Geometry/canvas: collapse, group-or-toggle dispatch, hull/chip bounds, chip drag (incl. the real mouse path), rename-dialog guard, undo/redo, boundary-cable re-anchoring, the untrusted-patch trust boundary |
| `MacroContainerInteractionTests.cpp` | Chrome/interaction: collapse button, recolour (preview + one-undo-step commit), card double-click-to-rename vs. expand, member right-click context menu, card right-click membership menu |

**`AIChatComponentTest` file layout.** Split by topic under `Tests/UI/Assistant/AIChatComponent/`, sharing the mock `AIProvider`s, `findMessageList`/`findDescendantWithText`, and the `AIChatComponentTest` fixture in `Tests/UI/Assistant/AIChatComponent/AIChatComponentTestFixture.h` (header-only, not built on its own):

| File | Covers |
|------|--------|
| `AIChatComponentTests.cpp` | Core: init/resize, send-message + classifier, `refreshModels()`/provider-install ordering, hosted-mode notices, account row, quota-error upgrade button, thumbs feedback, response-time/timeout display |
| `AIChatComponentHistoryTests.cpp` | P6-8/P6-9 history: unified history UI, upsell/downgrade strips, per-plan backend selection, clear-history, restoring a saved conversation, local save-on-every-exchange, rating sync, wrapped-height UX-polish regressions |
| `AIChatComponentArrangeTests.cpp` | Arrange mode: selector gating on the timeline preference, explicit capability/prompt routing, validated/rejected timeline-card response flow |

**`MacroAutoPort*Test` file layout.** Split by topic under `Tests/Macros/MacroAutoPort/`, sharing the test module stand-ins and graph/mouse helpers in `Tests/Macros/MacroAutoPort/MacroAutoPortTestHelpers.h` (header-only, not built on its own):

| File | Covers |
|------|--------|
| `MacroAutoPortCreationTests.cpp` | Auto-creating ports on grouping: mono crossing, jack dedup, collapsed-stereo, dual-I/O stereo merge, poly-N, MIDI-as-separate-node, mod-routing-knob splice (incl. T144), grouping-is-one-undo-step |
| `MacroAutoPortUngroupTests.cpp` | Ungroup removes auto-created ports and splices cables back; presentation-count/tooltip rules; the auto-create-ports preference's modal-firing conditions |
| `MacroAutoPortDeleteTests.cpp` | T148/T154: auto-delete a macro port once its last cable is gone, via `disconnectCable`/`disconnectPort` and via whole-node deletion |

**PianoRoll file layout.** The piano-roll editor's suite (234 tests) is split by topic under `Tests/UI/PianoRoll/`, all sharing the `PianoRollFixture` (a `CountingRoll`-backed roll wired to a bare `TimelineDoc` + `AppUndoManager`, no `TimelinePanelComponent` needed) and mouse/keyboard-gesture helpers in `Tests/UI/PianoRoll/PianoRollTestHelpers.h` (header-only, not built on its own):

| File | Covers |
|------|--------|
| `NoteSelectionModelTests.cpp` | `synth::ui::NoteSelectionModel` — mirrors `ClipSelectionModelTests.cpp`'s coverage, keyed on `synth::NoteId` |
| `NoteHitTestMarqueeTests.cpp` | `synth::ui::noteHitTestMarquee` — mirrors `clipHitTestMarquee`'s coverage |
| `PianoRollComponentTests.cpp` | Core: open/close lifecycle, the gesture table (single click deselects, DOUBLE-click creates/deletes, drag moves/resizes, drag-from-empty marquees), note length following the snap division, clip-window clamping, the roll's own beat<->x mapping (first bar reachable, zoom around the cursor, gridline density), the local playhead's strip-confined repaint seam, the Q button, and a snapshot smoke test |
| `PianoRollEditToolsTests.cpp` | The edit tools (Split/Glue/Erase/Mute/Draw) — each one's single-click gesture and no-op cases; QUANTIZE — the pitch-quantize verb, its header chip and both Option+Q shortcuts |
| `PianoRollClipboardTests.cpp` | The note clipboard — copy/cut/paste-at-playhead/duplicate/repeat/select-all, including the cross-clip paste the member clipboard exists for |
| `PianoRollArrowKeyTests.cpp` | ARROW-key editing — grid nudge, semitone/octave transpose, group clamping, the fall-through contract when nothing is selected; Alt+Left/Right note NAVIGATION — walking the doc's canonical note order, collapsing a multi-selection, the ends of the run |
| `PianoRollShortcutsTests.cpp` | REBINDABLE surface keys resolved through a `ShortcutManager` instead of the hardcoded defaults, including the "an unbound action has no key" rule and the two keys (Escape, Delete) that stay fixed on purpose |
| `PianoRollZoomTests.cpp` | WHEEL/ZOOM policy — the macOS Shift axis swap, natural-vs-inverted scroll sign, the anchored zoom commands, the wheel-zoom DIRECTION convention; SNAP vs the drawn grid — the chosen division stays visible with snap off |
| `PianoRollPaintingTests.cpp` | KEY LABELS (paintKeysColumn's pure per-row decision); ROW MAPPING (yForPitch/pitchForY through visiblePitches_, scale-context filtering); NOTE COLOURING through `synth::ui::resolveNoteColour`; KEYS-COLUMN geometry (black-key inset seam + snapshot smoke); the TOOLBAR ROW's three-band vertical layout above the ruler |
| `PianoRollScaleAssistTests.cpp` | Scale assist — the header button + gutter shift, `quantisePitchesToScale`, `generateRandomNotesIntoClip`, per-clip memory, PropertiesFile persistence; the scale-panel SLIDE animation; SHOW ONLY SCALE NOTES as a header chip + Option+S and the scale-aware Up/Down transpose it enables; the `ScaleAssistPanel` component in isolation via its own accessors |
| `PianoRollHeaderChipsTests.cpp` | Header button chip affordance (six chips — hover wash + resting/active fill); GENERATE — add-to-existing vs replace, plus the six chips' distinct/non-overlapping bounds |
| `PianoRollAuditionTests.cpp` | NOTE AUDITION (`onAuditionNote`) — "clicking a note plays it" — through the roll's own callback and through the real `TimelinePanelComponent` -> `TrackHeaderHost` wiring; KEYS-COLUMN audition (the virtual keyboard down the left gutter) |
| `PianoRollMouseTests.cpp` | MARQUEE multi-select from empty grid; BEAT-ANCHORED drag math / EDGE AUTO-SCROLL / FOLLOW PLAYHEAD; MULTI-NOTE RESIZE (incl. the Cmd unquantized resize and the clip-overrun prompt); CMD+DRAG unsnapped MOVE + the velocity scrub's move to Option |

#### `createComponentSnapshot` smoke-test pattern

Several new tests use `Component::createComponentSnapshot(bounds)` to verify that a component renders without crashing and produces non-empty pixels, without requiring a real display or window. Example: `StatusBarTests::RendersNonEmptyImage` and `ThemeTests::StyledWidgetSmokeTest.*`. The pattern is:

```cpp
comp.setSize(width, height);
auto img = comp.createComponentSnapshot({0, 0, width, height});
EXPECT_GT(img.getWidth(), 0);
```

#### AppProperties isolation pattern for persistence tests

Tests that read/write `ApplicationProperties` use an isolated temporary directory to avoid contaminating the shared settings file across test runs. The `MainComponent` exposes `getAppPropertiesForTest()` so the test fixture can target a temp dir:

```cpp
// In test SetUp():
auto tmpDir = juce::File::createTempFile("").getParentDirectory()
                  .getChildFile(juce::Uuid().toDashedString());
tmpDir.createDirectory();
// Write a value then verify MainComponent reads it back:
comp.getAppPropertiesForTest().getUserSettings()->setValue("librarySidebarVisible", "0");
// TearDown(): reset to defaults, then tmpDir.deleteRecursively()
```

#### Shared-settings-file reset-guard pattern (an alternative to tmp-dir isolation)

A headless `MainComponent` (`newPatchForTest()`) has no `getAppPropertiesForTest()` seam — it
hits the real, shared on-disk "Agent Synth" `ApplicationProperties` file, so a persisted setting
leaks into every other such test. Fix (`ChannelFlowTestFixture.h`'s `ChannelFlowTest::resetKeys()`;
FRO11/P9-5's `MixerDockActiveTabResetGuard.h`): a local RAII guard per affected test that opens the
same `ApplicationProperties`/`Options` and `removeValue()`s only the key(s) that suite touches, in
both its constructor and destructor (safe regardless of test order or a prior crashed run).
`MixerDockActiveTabResetGuardMDT` resets `"bottomDockActiveTab"` so a PNG-snapshot test's Mixer-tab
switch can't leak into a later test's "Timeline is default" assumption.

### State Management Tests (~82 tests)

Test persistence, serialization, and state restoration.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| PresetManagerTest | 12 | Preset listing, load all presets, default preset validation, audio output connectivity, all 7 factory presets load with zero pairwise bounding-box overlaps at kCollisionGap=12; `AllPresetsPositionsOnGrid` (every baked x,y %8==0); estimateModuleSize mirror updated: Sequencer/PolySequencer/MidiKeyboard→560 (kDoubleWidth), Attenuverter excluded via `continue` |
| UndoRedoTest | 12 | Add/remove modules, connections, parameter changes, complex sequences, rapid operations, auto-arrange is a single undo step (one Cmd+Z restores all pre-arrange positions) |
| AIStateMapperTest | 40 | Graph JSON round-trip serialization, parameter validation, modulation serialization, merge mode, schema generation; `MergeAutoConnectsNewAudioNodesToOutputByDefault` / `MergeSkipsAutoConnectWhenTheCallerOptsOut` — locks the merge-mode convenience wiring (new audio node → Audio Output) that AI patches rely on and snippet insertion opts out of, so the two cannot drift into each other. **Golden-data regression suite:** `FactoryTypeNamesRoundTrip` (every factory key serializes under the name that rebuilds it, catching issue #196: Poly Sequencer → "Sequencer" downgrade); `PolySequencerSurvivesRoundTrip` regression lock; `ParamIdsGolden` / `AuthorableModuleTypesGolden` pinned golden tables; `GraphToJSONEmitsSchemaVersionAndNodeUuids` + `PatchWithoutSchemaVersionStillApplies`; `NodeUuidsAreStableAcrossTrustedRoundTrip` / `UntrustedApplyIgnoresIncomingNodeUuids` (trusted vs untrusted paths); `SchemaOmitsReservedFields` (schemaVersion, uuid, timeline absent from schema); `TimelineIsRefusedFromUntrustedPatchesOnly` (refused untrusted, accepted trusted); `MergeMode_TypeMismatchIsRejectedUntrusted` + `*StillCreatesNewNodeWhenTrusted` + `*SameIdSameTypeStillUpdatesInPlace` + `*RemovedIdMayBeReusedForAnotherType` (merge-mode collision handling) |
| PatchDocumentTests | 7 | `Source/PatchDocument.h` — stashing unknown top-level JSON keys across a save/load round-trip so an older build never destroys a newer build's data (e.g. a future "timeline" key). Per-loaded-file scope (newPatch() clears); only user preset save/load path (undo/redo/snippets/AI apply must not resurrect file-level keys). `RoundTripPreservesUnknownTopLevelKeys` (loadFromVar stashes, toVar re-merges); `PreservedKeysNeverOverwriteKnownKeys` (stashed key cannot shadow a known key); `ClearDropsTheStash`; `SchemaVersionIsNeverStashed`; `GraphEditorSaveLoadRoundTripsUnknownKeysAndNewPatchClears` (end-to-end GraphEditor integration); `PreservedKeysNeverReachSetExtraState` (verified vs ModuleBase inert coupling) |
| SnippetManagerTests | 43 | Module snippets / grouping (issue #156, `Source/SnippetManager.{h,cpp}` — headless). **Extraction:** only selected modules; graph I/O nodes excluded even when selected; only connections with *both* endpoints inside the selection; positions normalised to the selection's top-left with relative layout preserved; `selectionOrigin` returns that same top-left, ignores ineligible/stale ids, and `AgreesWithTheOriginExtractionNormalisesAgainst` — the contract the clipboard leans on to place a paste relative to its source. **Extra state:** `includeExtraState` is off by default so a hand-editable `.agsnip` can never carry a `state` key into the trusted apply (`applyExtraStateToProcessor` reads it as a filename for Sampler), and `prepareForInsert` strips one even if a file has it; on, a Sampler's loaded file survives — that is the in-memory clipboard's opt-in; modulation stored as a `modulations` entry with the Attenuverter never becoming a snippet node; modulation leaving the selection dropped; stale/absent ids ignored. **Ids:** `nextFreeIdBase` above every existing uid; `prepareForInsert` renumbers + offsets, never mutates its input (the library inserts one loaded snippet repeatedly), clamps away from `NodeID{0}`. **Insert:** merges without disturbing a pre-existing module of the same type (the merge-collision regression), places the group at the drop point, `PreservesParameterValuesThatLookNormalised` (a 0.5 Hz LFO rate on a 0.01–20 range survives — the strict-validate / trusted-apply split), rebuilds modulation chains, two inserts give two independent copies, malformed JSON rejected without partially applying, dangling connections dropped, `DoesNotSpliceTheInsertedGroupIntoTheSurroundingPatch` + `DoesNotAttachInsertedModulesToAnExistingMidiSource` (merge-mode auto-connect opt-out — no wire may cross the snippet boundary on insert). **Persistence:** save/load/list/delete round-trip, insert from disk, name rewritten to the sanitised form, empty snippet and unusable name refused, corrupt files skipped by `listSnippets`, path separators stripped so a name cannot escape the directory, length capped |

### Output Level Tests (18 tests)

`Tests/Engine/OutputLevelTests.cpp` — the shared opt-in output-level stage (`ModuleBase::addOutputLevelParameter` / `prepareOutputLevel` / `applyOutputLevel`) and the modules that adopt it. Headless. The related hidden-amplifier audit and mix-audibility suites (`Tests/Engine/GainStaging/`) are documented in [`testing_gain_staging.md`](testing_gain_staging.md).

| Suite | Tests | What it covers |
|-------|-------|----------------|
| OutputLevelHelper | 9 | Unity default is bit-exact pass-through; only the declared leading audio channels are scaled (CV channels untouched); level 0 silences; a 1.0→0.0 step ramps over 10 ms (max per-sample step < 0.01, reaches zero within the block) instead of clicking; **bypass passes dry audio at level 0**; mute clears at unity; a legacy state blob with no `outputLevel` property loads at unity (old presets sound identical); level survives a state round-trip. Uses an in-test `ModuleBase` subclass so the helper is exercised free of any module's DSP |
| OutputLevelModules | 9 | Across all 10 adopting modules (Delay, Reverb, Chorus, Phaser, Flanger, Distortion, Bitcrusher, Pitch Shifter, Filter, Ring Modulator): parameter present and defaulting to unity; **parameter added last in the list** (only `muted` may follow it); level 0.5 halves the output sample-for-sample; level 0 silences; bypass still passes dry audio at level 0; mute clears at unity. Plus: Delay's level stays outside the feedback path (repeats survive a spell at level 0); Filter scales all 8 voices in poly mode; **`AttenuverterKeepsAmountAtParameterIndexOne`** pins the positional-parameter landmine shut |

### Stereo Declaration Tests (8 tests)

`Tests/Modules/StereoVoiceModule/StereoVoiceModuleDeclarationTests.cpp` (suite `StereoDeclaration`) — the Dual I/O toggle is granted by `ModuleBase`'s constructor from a module's channel shape (`ModuleBase::StereoAudio`), so these sweep the whole module factory rather than checking modules one at a time. Headless.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| StereoDeclaration | 8 | Every factory module either matches the shape rule (**≥ 2 in, exactly 2 out** → toggle, collapsed by default) or appears in one of two documented exception tables (`Declared` → toggle, split by default; `None` → no toggle, with a written reason) — a new stereo FX passes with no edit, a new exception fails until it is listed; the tables have no stale or no-longer-matching entries; **a bare module with only the shape and no Dual I/O code at all inherits the parameter AND the collapsing output jack**; the Ring Modulator (whose explicit registration was deleted) still behaves identically; `AIStateMapper::dualIOCapableModuleTypes()` reports exactly the modules that have the parameter; mute clears every channel and the CV inputs stay cleared (normal and bypassed blocks) for every registry module in both jack states; **a patch authored before the toggle moved into the base loads with identical layout** — explicit `dualIO` values survive, an omitted one falls to the module's default, and the Ring Modulator (whose patch predates its toggle) opens collapsed — plus the same claim on the `getStateInformation` XML path, where a blob with the `dualIO` attribute stripped out loads at the module's own default (collapsed for `Auto`, split for `Declared`) rather than at zero |

### Module Adoption Tests (3 tests)

`Tests/Modules/ModuleAdoptionTests.cpp` — enforces the standing rule that **every module whose output carries audio has a level control**. Headless.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| ModuleAdoptionTests | 3 | A hand-maintained table classifies every module the factory can build into `SharedStage` (adopted `addOutputLevelParameter`), `OwnParameter` (has its own `level`/`gain`/`outputGain`/`inputGain`/`makeupGain`) or `NoLevelByDesign` (CV/gate/MIDI output, with a rationale string). `EveryAudioOutputModuleHasALevelControl` asserts each module matches its declared bucket — including that an `OwnParameter` module does **not** also adopt the shared stage (two level knobs on one panel). `EveryFactoryModuleIsClassified` is the tripwire: it harvests module names from `AIStateMapper::getModuleSchema()` and fails when a newly added module isn't classified, with a message naming the three buckets. `ClassificationTableHasNoStaleEntries` catches the reverse — a renamed/removed module leaving a dead row that silently stops enforcing anything |

### Layout Tests (~8 tests)

Pure/headless tests for the grid-layout and anti-overlap helpers in `Tests/UI/Layout/LayoutUtilTests.cpp`.
No JUCE GUI components required.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| LayoutUtilTest | 16 | `snap` round-trips (negative-safe, midpoint), `intersectsAny` gap enforcement + selfId exclusion, `findFreeSlot` returns desired when clear, `findFreeSlot` resolves dense cluster (returned slot overlaps none), `computeAutoArrange` signal-depth layering (x strictly increases per depth, Audio Output in last column, no result-box overlaps); **width-bucket mapping** (Sequencer/PolySequencer/MidiKeyboard→Double, Attenuverter→Narrow, all others→Single); **bucket constants on grid** (kNarrowWidth/kSingleWidth/kDoubleWidth all %8==0, kDoubleWidth==2×kSingleWidth); **column stride** (kSingleWidth + kLayerGapX == 360); **Macro bank geometry** (`macroBankHeight` grows one `kMacroRowH` per macro, `macroRowCentreY` evenly spaced and always inside the bank); **`resolveOverlapsAfterResize`** (no-op when clear, pushes the neighbour below past the new bottom edge and on-grid, never moves the resized module, cascades through a stack until nothing overlaps, shrinking moves nobody back) |

### Theme Tests (~30 tests)

Tests for the theme system — `Tests/UI/Theme/ThemeTests.cpp`. All headless.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| ThemeBuiltInsTest | 2 | Built-in theme registration (obsidian/neon/warm), WCAG AA contrast (≥4.5) for all built-ins |
| ThemeLoaderTest | 8 | JSON round-trip (exact colour/metric/typography/treatment equality), obsidian.gtheme.json vs makeObsidian(), required-key rejection, bad hex rejection, treatment float clamping, schema version rejection, style string round-trip |
| ThemeTest (fixture) | 4 | Persistence + restore via ApplicationProperties, broadcast on change / idempotency, unknown id rejection, user theme replace-by-id |
| ThemeLookAndFeelTest | 2 | All ColourId mappings from spec section 3, draw-helper smoke tests (fillThemedBackground / drawModulePanel / drawConnectionWire / drawModulationRing / drawRotarySlider into headless image) |
| ThemeLookAndFeelTest (extended) | 4 | `ApplyThemeSetsEveryColourId` extended to cover ListBox and TabbedButtonBar ColourIds; `RetintIconsCalledByApplyTheme` (getIcon non-null across 3 built-ins); `MetricsCodeOnlyFieldsHaveExpectedDefaults` (toolbarHeight==36, statusBarHeight==24, etc.); `MetricsCodeOnlyFieldsNotInJSON` (ThemeLoader output does not contain "toolbarHeight") |
| StyledWidgetSmokeTest | 9 | `drawComboBox` normal/pressed/disabled × 3 themes; `drawComboBoxTextWhenNothingSelected`; `drawPopupMenuItem` separator/highlighted/ticked/disabled/hasSubMenu; `getDefaultScrollbarWidth()==6`; `drawScrollbar` V/H × over/down; `drawScrollbarButton`; `drawTabbedButtonBarBackground` + `drawTabButton` active/inactive/hover with snapshot pixel check |

### Cable Colour Tests (22 tests)

Suite `Tests/UI/Graph/CableColourTests.cpp` covering `Source/UI/Graph/CableColour.h` and the cable
enumeration / hit-testing added to `GraphEditor`. Layered: the pure resolver needs no GUI at
all, the canvas fixture builds a real two-module patch headlessly.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| CableColourCategoryTest | 3 | `categoryFor` totality over all 23 `ModuleType` values, groupings match the ModuleLibrary sections, persisted signal/category ids are unique and stable (`envelopes`, `modcv` spot-checked — renaming breaks saved user colours) |
| CableColourResolveTest | 7 | Each `CableSignal` maps to its theme token; `midiWire` differs from `audioWire` in every built-in theme; `BySourceCategory` uses the palette and ignores signal; the 8 category colours are mutually distinct per built-in theme; override precedence is mode-scoped; bypass alpha applies to the winning colour but NOT to `resolveCableBaseColour` (what swatches render); clearing an override restores the theme colour |
| CableColourPersistenceTest | 2 | Mode round-trip through `PropertiesFile`; override round-trip, untouched entries stay unset, reset removes the key entirely |
| CableColourThemeTest | 3 | `midiWire` + `cableCategory` survive a `themeToJson` → `parseTheme` round-trip; a pre-#157 theme with neither key still loads on defaults; a partial `cableCategory` object overrides only its named keys |
| CableGeometryTest | 2 | `buildCablePath` starts/ends exactly on the ports; `distanceToCable` ≈ 0 on the wire and large away from it |
| CableCanvasTest (fixture) | 5 | Enumeration reports the audio cable with the right signal + source category; hit-test hits a point sampled from the drawn curve and misses far away; tolerance is respected; `disconnectCable` removes the graph edge; `colourForCable` follows the active mode and overrides |

### Icon Library Tests (~11 tests)

Suite `Tests/UI/Theme/IconLibraryTests.cpp` covering `Source/UI/Theme/IconLibrary.h/.cpp`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| IconLibraryTest | 11 | `AllIconEnumValuesHaveEntry` — every Icon < kCount returns non-null getDrawable when assets present; `NullFallbackWhenAssetsAbsent` — no-assets path returns nullptr without crash; `ClonedDrawableIsIndependent` — two getDrawable calls return distinct raw pointers; `TintColourChangeApplied` — setTintColour(icon, c1) then setTintColour(icon, c2) → result contains c2 not c1; `RetintMultipleSwitchesStable` — 100× alternating setTintColour loop, final colour matches last set value; `SvgBinaryDataNamingConvention` — raw BinaryData symbol non-null (guards CMake renames); `TransportPlayIsScaffolding` — Icon::TransportPlay loads without crash; no DrawableButton wired this phase; Phase 4 adds: `WaveformIconsLoad` — all four WaveformSine/Saw/Square/Triangle return non-null; `WaveformIconsTintedToTextPrimary` — tint colour matches textPrimary after retintIcons(); `WaveformIconKTableCount` — static_assert count == 26 enforced at compile time; `WaveformBinaryDataSymbolsPresent` — waveformsine/saw/square/triangle_svg symbols non-null |

### Status Bar Tests (~9 tests)

New suite `Tests/UI/Chrome/StatusBarTests.cpp` covering `Source/UI/Chrome/StatusBarComponent.h/.cpp` and AudioEngine additions.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| StatusBarTest | 9 | `ConstructsWithoutCrash`; `RendersNonEmptyImage` (createComponentSnapshot 400×24); `FormatCpu` — 0.0%, 75.6%, 100.0%; `FormatVoices` — 0→"0 voices", 1→"1 voice", 8→"8 voices"; `FormatPatch` — ""→"Untitled", named patch passes through; `GatedRepaintDoesNotFireOnUnchangedValues` — update() twice with same values triggers repaint only once; `AudioEngine_GetActiveVoiceInfo_ReturnsZeroWithoutPolyModules`; `AudioEngine_CountsPolyMidiVoices` — maxVoices==8 after adding PolyMidiModule; `MasterMute_ZeroesOutput` — setMasterMute(true) → output buffer all zeros post-processBlock |

### Frequency Response Tests (~50 tests)

`Tests/UI/ModuleViews/FrequencyResponseTests.cpp` covers `Source/UI/ModuleViews/FrequencyResponseComponent.h`; `Tests/UI/ModuleViews/EQCurveTests.cpp` covers the axis maths both frequency-domain views share (`Source/UI/ModuleViews/FrequencyGrid.h`), the Parametric EQ view and its interaction model (`Source/UI/ModuleViews/EQCurveComponent.h`), and the pop-out editor (`Source/UI/ModuleViews/EQWindow.h`).

| Suite | Tests | What it covers |
|-------|-------|----------------|
| FrequencyResponseTest | 17 | `PeakBinFindsMaximum`/`PeakBinFindsFirstMaxWhenTied`/`PeakBinHandlesEmpty`/`PeakBinSingleElement` — `findPeakBin` returns the max-magnitude bin (first index on ties, -1 for null/zero-length, 0 for single element); `FormatHzLabel_100Hz`/`_1kHz`/`_10kHz`/`_SubKiloHz`/`_FractionalKiloHz` — `formatHzLabel` yields "100Hz"/"1kHz"/"10kHz"/"440Hz"/"1.5…kHz"; `FreqMappingMonotonic`/`FreqMappingEndpoints` — `freqToXStatic` log map is monotonic and pins 20 Hz→x=0, 20 kHz→x=width; `DbMappingMonotonic` — `dbToYStatic` monotonic across +20/0/−20 dB; `PaintSmoke` — paints into a `juce::Image` with no crash, produces opaque pixels; `PlotDbCapsPeaksButLeavesDeepCuts` / `DbBelowWindowMapsPastBottom` — deep roll-off stays below `minDb` and maps past the view bottom (no floor stroke); `TimerRunsOnlyWhileVisible` — 30 Hz timer gated on visibility; `LowpassRollOffDoesNotStrokeBottomEdge` — LPF paint regression: bottom-right edge is not an opaque accent floor line |
| FrequencyGridTest | 14 | `FreqToXSpansTheFullWidth`/`FreqToXIsLogarithmic`/`FreqToXIsMonotonic` — equal frequency *ratios* map to equal pixel distances; `XToFreqInvertsFreqToX`/`XToFreqHandlesZeroWidth`; `IndexToFreqSpansTheAxisAndIsMonotonic`/`IndexToFreqHandlesDegenerateCounts`; `DbToYPutsMaxAtTopAndMinAtBottom`/`DbToYIsMonotonicDownwards`/`YToDbInvertsDbToY`/`YToDbHandlesZeroHeight`; `FormatHzLabel`/`FindPeakBin`; `FilterViewStaticsDelegateToTheSharedGrid` — regression lock that `FrequencyResponseComponent`'s public statics still agree with `FrequencyGrid` after the mapping code was hoisted out of it |
| EQCurveTest | 8 | `StaticsUseASymmetricThirtyDbWindow` — ±30 dB window puts 0 dB at the vertical centre; `SpectrumIsOnByDefaultSoTheCurveHasABackdrop`; `FreqAtXAndGainAtYInvertTheDrawingTransform`; `GainAtYClampsToTheBandGainRange` — the ±30 dB view is wider than the ±24 dB parameter, so the top clamps; `PaintSmokeEmptyShowsTheHint`/`PaintSmokeWithActiveBands`/`PaintSmokeWithAllFourBandsAndSpectrum` — paint into a `juce::Image` with no crash and opaque pixels, including the FFT overlay driven by an explicit `timerCallback()`; `SilentInputDoesNotLeaveTheSpectrumRunning` — the silence gate that makes a default-on analyser free on an idle patch; `PaintAtDegenerateSizesDoesNotCrash` |
| EQCurveInteraction | 9 | The Cubase-style gestures, driven through the same methods the mouse handlers call. `AddPointEnablesTheBandAtTheClickedPosition`/`AddPointPicksTheSlotMatchingTheClickedFrequency`/`AddPointReturnsMinusOneOnceAllSlotsAreUsed`; `RemoveBandDisablesItAndClearsSelection`; `HitTestFindsEnabledHandlesAndIgnoresDisabledOnes` — including that a removed handle stops being hit-testable; `DragMovesTheBandInBothFrequencyAndGain` — with clamping past the edges; `ScrollAdjustsQMultiplicatively` — one notch doubles/halves Q and it saturates at the parameter bounds; `EveryEditIsBracketedByExactlyOneGesture` — balanced undo brackets, and a rejected add opens none; `DragIsNotBracketedPerStepSoOneDragIsOneUndoStep`; `ComponentPicksUpBandChangesMadeOnTheModule` — the card and pop-out window converge via the timer |
| EQWindowTest | 4 | `HostsACurveOverTheSameModule` — sizes itself, lays the curve out, and edits land on the shared module; `SpectrumToggleTracksTheCurve`; `ForwardsGestureCallbacksToTheCurve` — so pop-out edits are undoable; `PaintSmoke` |

### Scope Tests (~10 tests)

New suite `Tests/UI/ModuleViews/ScopeTests.cpp` covering `Source/UI/ModuleViews/ScopeComponent.h`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| ScopeTest | 10 | `NoSignalThreshold_Zero`/`_BoundaryInclusive`/`_JustAbove`/`_FullAmplitude` — `isNoSignal` is true for peak ≤ 0.02f (boundary inclusive), false above; `AmplitudeMapping_TopNearBoundsTop`/`_BottomNearBoundsBottom`/`_ZeroIsVerticalCentre`/`_Symmetry` — `amplitudeToY` maps +1→above centre, -1→below centre, 0→exact vertical centre, symmetric about centre; `PaintSmokeNoSignal`/`PaintSmokeWithSignal` — paint the silent (No-Signal empty-state) and signal states into a `juce::Image` with no crash |

### Wavetable Oscillator Tests (76 tests)

Suite `Tests/Modules/WavetableOscillatorModule/` covering `Source/Modules/WavetableOscillatorModule/`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| WavetableOscillatorModuleTest | 51 | **Surface** — `FactoryInitialisation` (type/name/26 params/`kNumInputs`×`kNumOutputs` channels/16 visible in-jacks/2 out-jacks, and that the Audio R block clears the whole shared-CV block); `DeclaresEnoughOutputsForEveryCVInput` — guards the buffer-aliasing invariant (highest poly CV channel < `getTotalNumOutputChannels()`); `PortLabelsAndModulationTargets`; **`LegacyModCVChannelsKeepTheirIndices`** — Position/Octave/Coarse/Fine/Level keep their pre-#180 raw channels, so patches saved against the six-jack module still route to the same targets; `StereoOutputPortsMapToSeparateChannelBlocks` — both legs are 8-wide poly-bus heads, and a channel between them is never a head; `LogicalPortMappingMonoAndPoly`; `ZeroChannelsDoesNotCrash`. **Core audio** — `ProducesAudioOnChannelZero`; `DefaultTablePositionZeroIsASine` (>95% of energy at the fundamental); `ScanningPositionChangesTheSpectrum`; `EveryBuiltInTableProducesAudio`; `LoadedFileChoiceFallsBackWhenNothingIsLoaded`; `OutputStaysBounded`; `LowSampleRateWithExtremeTuningStaysFinite` — 8 kHz sample rate with octave +4 / coarse +12 (>1 cycle per sample), guarding the phase wrap; **`HighNotesDoNotAlias`** — square at MIDI 108, worst magnitude in 200 Hz–3.5 kHz is <5% of the fundamental (the mip-selection guard); `PositionCVScansTheTableInMonoMode`, `LevelCVAttenuatesInMonoMode`; `PolyModeRendersOneVoicePerPitchCVChannel`, `PolyModePositionCVScansAllVoices`, `OctaveParameterTransposesInPolyMode`. **Warp (#180)** — `WarpAmountZeroLeavesTheWaveAlone` (sample-for-sample against an unwarped module), `WarpChangesTheSpectrum`, `WarpAmountCVDrivesTheWarp`. **Phase control (#180)** — `RetriggerPhaseStartsTheWaveWhereAsked` (0°/90°/270° land on the zero crossing and both peaks of the note-on block), `RandomPhaseDecorrelatesRepeatedNotes`, `UnisonSpreadDecorrelatesTheStack` (8 correlated sines pile up; spreading their start phases makes them cancel). **Stereo & voicing (#180)** — `DefaultsKeepAudioLIdenticalToAudioR` (the mono-compatibility guard for the balance pan law), `UnisonWidthSeparatesTheStereoLegs`, `PanShiftsEnergyBetweenTheLegs`, `PolyModeWritesBothAudioBlocks` (both 8-wide blocks sound, silent voices stay silent, the shared-CV block does not leak), `SubOscillatorAddsASubOctavePartial` (−1 and −2 octave), `StackModesTransposeUnisonVoices` (power chord puts energy at root/fifth/octave), `BlendFadesTheStackAgainstTheCentreVoice`, `RingModMultipliesBySyncInput` (sidebands appear, carrier suppressed), `HardSyncResetsThePhaseOnTheMasterEdge`. **Import & interpolation (#180)** — `FixedImportSizeSplitsOnThatBoundary`, `SingleCycleImportMakesExactlyOneFrame`, `PitchDetectImportFindsTheSourcePeriod` (a 512-sample period yields 32 frames with no 2nd harmonic — the octave-error guard), `SpectralImportKeepsMagnitudesAndDropsPhase` (opposed frames cancel at the midpoint on a normal import, survive on a spectral one), `HermiteInterpolationTracksLinearButStaysBounded`. **Browser & state (#180)** — `FolderBrowserScansStepsAndWraps` (non-audio files excluded, scanning loads nothing, next/prev wrap, an empty folder stays inert), `StateRoundTripRestoresNewParametersAndFolder` (both the binary blob and the `getExtraState` path). **Loading** — `LoadWavetableFileSplitsFrames`, `LoadedFramesKeepTheirDistinctHarmonics`, `LoadWavetableFileRejectsMissingAndInvalidFiles`, `LoadWavetableFileCapsFrameCount`, `ReloadingWhileRenderingStaysStable` (exercises the pending/retired handoff); `StateRoundTripRestoresParametersAndWavetable`, `StateRoundTripSurvivesAMissingWavetableFile`; **`TrustedGraphJSONRestoresTheWavetable`** — round-trips through `graphToJSON`/`applyJSONToGraph`, the path presets and undo actually use; `UntrustedPatchCannotNameAWavetableFileToOpen` — model-authored JSON must not reach `setExtraState` |
| AllWarpModes/WavetableWarpAliasTest | 11 | **`EveryWarpModeStaysCleanBelowTheFundamental`** — parameterised over all 11 warp modes. Each is swept at full warp on MIDI 108 with the squarest frame selected; the loudest bin in 300 Hz–0.85·f0 must stay under 5% of the loudest bin in the legitimate band (0.95·f0–20 kHz). No mode has legitimate content below the fundamental — they all reweight or multiply harmonics of f0 — so anything down there is folded. **A warp mode that aliases is a regression, not a feature: extend this suite when adding one.** |
| AllWarpModes/WavetableWarpBoundsTest | 11 | `EveryWarpModeStaysBounded` — same 11 modes at full warp with 8-voice unison, 50-cent detune and the sub at full; both stereo legs must stay finite and under ±4.0 |
| WavetableMipGeometry | 1 | `LimitsDecreaseMonotonicallyToTheFundamental` — mip 0 holds 1023 harmonics, the coarsest holds 1, limits strictly decrease, and every mip's limit is within its own Nyquist with length ≥ 64 |
| MuteAndBypass/WavetableMuteBypassTest | 2 | `OutputIsSilentWhenMutedOrBypassed` — parametrized over mute and bypass; a pure source module clears on both (the documented `OscillatorModule` exception) |

### Wavetable Display Tests (8 tests)

New suite `Tests/UI/ModuleViews/WavetableDisplayTests.cpp` covering `Source/UI/ModuleViews/WavetableDisplayComponent.h`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| WavetableDisplayTest | 8 | `QuantisePositionEndpointsAndClamping`/`QuantisePositionIsMonotonicAndCollapsesTinyChanges` — `quantisePosition` pins 0→0 and 1→`steps`, clamps out-of-range input, is monotonic, and maps sub-bucket jitter to the same bucket (this is the repaint gate); `RepeatedTimerTicksOnAnUnchangedModuleAreIdempotent` — five ticks leave the trace bit-identical, a real position change is still picked up; `DisplayWaveformTracksScanPosition` — position 0 and 1 traces differ by >0.2 and both stay bounded; `DisplayWaveformHandlesTinyPointCounts` — 0 and 1 requested points still yield ≥2 samples; `PaintSmokeBuiltInTable`/`PaintSmokeAtEveryScanPosition`/`PaintSmokeAtDegenerateSizes` — paints into a `juce::Image` with no crash at 11 scan positions and at 0×0/1×1/4×80 bounds |

`Tests/UI/ModuleViews/CurveEditor/` (FRO111; not yet wired into a module card — see [`layout_visuals_animation.md` §1](layout_visuals_animation.md#curveeditorcomponent-sourceuimoduleviewscurveeditor)) covers the reusable breakpoint curve editor at `Source/UI/ModuleViews/CurveEditor/` across four suites, 65 tests, sharing an `EnvelopeShape`/`buildEnvelopeModel` fixture (`CurveEditorTestHelpers.h`) for the fixed origin/attack-peak/hold-end/sustain/release-end topology the envelope card will use: **CurveModelTest** (18) — node/segment bookkeeping, `valueAt` matching `synth::EnvelopeGenerator::shape` exactly, Fixed-mode ripple x-edits (`minSegment`/`maxSegment` clamping, `y` ignored when `!yMovable`, the origin never moving), Free-mode `addPoint`/`removePoint`/reorder; **CurveEditorGeometryTest** (20) — time/level↔pixel round trips, the zero-duration segment's exact 12px display plateau and absent bend handle, hit-testing (nearest wins, exact tie goes to the earlier index, a pinned node is never hit, a node beats a bend handle), the handle tracking the curve's actual midpoint as bend changes, playhead mapping, grid ticks/labels; **CurveEditorInteractionTest** (23) — the public drag/bend/add/remove primitives plus the REAL mouse path via synthesized `juce::MouseEvent`s (a plain click opens no gesture, a drag opens exactly one); a node drag freezes the visible range for its duration so repeated events at the same on-screen position don't runaway-grow/shrink the model's total duration, map back to the original duration when driven back to the start position, and re-fit once `mouseUp` clears the freeze; `setModel()` preserves a live drag across a same-topology model swap (the envelope card's parameter round trip) and cancels it — pairing `onGestureEnd` — on an actual topology change; **CurveEditorPaintTest** (4) — a themed paint smoke test (accent-coloured curve pixels vs. plain background) plus an opt-in `CURVE_EDITOR_SNAPSHOT` PNG dump (same pattern as `ADSR_CARD_PNG` in `ModuleComponentLayoutTests.cpp`).
### Minimap Tests (25 tests)

New suite `Tests/UI/Graph/MinimapComponentTests.cpp` covering `Source/UI/Graph/MinimapComponent.h/.cpp` (issue #159). See [`layout_selection_canvas.md` §4](layout_selection_canvas.md#4-minimap-overlay-issue-159) for the feature.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| MinimapComponentTest | 19 | `computeWorldBounds` — empty model falls back to a `kMinWorldSpan` square at the origin, nodes-only/viewport-only/both are contained, a single small node is clamped out to the min span while staying centred on it; `computeWorldToMap` — preserves aspect ratio in a non-square map area, maps inside and centres in the map area, zero-width world / zero-height map area stay NaN/Inf-free; `mapToWorld` round-trips several points through the forward transform; `setModel` reflects a differing model and no-ops on an equal one (observed via `getModel()`); `setViewport` updates only the viewport, leaving nodes/cables untouched; construction at a realistic size and a non-empty tooltip; paints a non-empty image after `setModel`; `mouseDown`/`mouseDrag` fire `onNavigate` with the point `mapToWorld` itself predicts; mouse and wheel handlers are safe no-ops with `onNavigate`/`onZoom` unset |
| MinimapModelTest | 6 | `MinimapModel::operator==`/`!=` — identical models are equal; differing viewport, node count, a moved node, a node differing only in `selected`, or a cable differing only in colour all make models unequal |

### E2E Workflow Tests (24 tests)

Full application workflow tests in `Tests/App/E2EWorkflowTests.cpp`. Each test constructs a complete `MainComponent` with a mock AI provider and exercises real UI interaction code paths.

| Area | Tests | What it covers |
|------|-------|----------------|
| App Initialization | 3 | Default patch has nodes/connections, panel toggle visibility, fresh undo state |
| Preset Management | 3 | Load preset updates graph, load all 7 presets without crash, load-modify-undo |
| Module Management | 4 | Drop module via `itemDropped()`, drop all 17 module types, delete module, replace module type |
| Connections | 4 | Connect ports via `beginConnectionDrag()`/`endConnectionDrag()` with `localPointToGlobal()` coordinate conversion, disconnect, MIDI connections, mod routing creates attenuverter |
| Mod Matrix | 4 | Add empty routing, configure source/dest, adjust CV amount, delete routing |
| Undo/Redo | 4 | Undo add-module, complex sequences, preset-load-then-modify, rapid 5-module sequence |
| Combined Workflows | 1 | Full preset-modify-connect-undo-redo workflow |
| Layout / Auto-Arrange | 1 | Load preset, call autoArrange(); all module comps non-overlapping AND connection/node counts unchanged |

#### Key patterns

- **Coordinate conversion**: Components are nested inside MainComponent, so connection tests use `localPointToGlobal()` to convert port positions to screen coordinates for `endConnectionDrag()`.
- **Relative counts**: The default patch creates ~14 nodes, so all tests use `initialCount + N` rather than absolute values.
- **Module lookup**: `findNewModule(name, initialNodeIDs)` finds only modules added after a snapshot, avoiding false matches with default patch modules.
- **Preset loading**: Must call `editor().detachAllModuleComponents()` before `PresetManager::loadPreset()` to avoid use-after-free.

### AI Patch Validation Tests (~19 tests)

`Tests/AI/AIPatchValidationTests.cpp` is table-driven: one deliberately malformed patch per
`PatchValidationError` value, each asserting the **exact** enumerator rather than merely "was
rejected". `EveryErrorValueIsCovered` walks the enum and fails if a newly added value has no case,
so the table cannot silently fall behind the enum.

The same file pins the `getPatchSchema()` contract — the module-type enum matches the factory,
choice parameters are enumerated, `additionalProperties` stays open for numeric parameters, and no
reference data is offered as an output field.

`Tests/AI/AIPatchRetryTests.cpp` covers `applyPatchWithRetry` with a scripted provider double that
answers synchronously (so no message loop is needed): that the validation message reaches the
model, that retries stop at `kMaxPatchRetries`, and that the mode repair fires only for a
rejected, mode-less patch and never in the destructive merge-to-replace direction.

### AI Patch Fixture Replay Tests (corpus-driven, offline)

`Tests/AI/AIPatchFixtureReplayTests.cpp` is a **characterisation test**: it replays real model output,
recorded once by `Tools/AIPatchHarness` against `gpt-oss:20b` (the confirmed production model)
and at least one other local model, through the exact production path —
`extractJsonFromResponse -> JSON::parse -> validatePatch -> applyPatchWithRetry` — with no model
and no network, in milliseconds. `AIPatchValidationTests.cpp` above uses hand-written patches,
which are cleaner than what a real model emits; this suite is what actually notices a refactor
breaking the retry loop against real-world model output, and it runs in every CI build (unlike the
harness itself, which needs a live Ollama and is opt-in only).

The corpus lives at `Tests/fixtures/ai-patches/*.json` — verbatim `--json` output from the harness,
committed as-is (format documented in `Tools/AIPatchHarness/README.md`). Each record carries the
raw model text for its first answer (`rawResponse`) and, if `applyPatchWithRetry` needed a
correction round-trip, the raw text of each retry answer too (`retryResponses`). The test replays
`rawResponse` through validation directly, then drives `applyPatchWithRetry` against a
`ReplayProvider` double that hands back `retryResponses` in order (mirroring
`AIPatchRetryTests.cpp`'s `ScriptedProvider`) instead of calling a real model, and asserts the
recorded outcome exactly: same parsed/valid/error, same final applied-after-retry result. Scenario
names are resolved against `Tools/AIPatchHarness/Scenarios.h` — the one shared table both the
harness and this test use, so a fixture can never silently drift from the prompt that produced it.
`CorpusIsPopulatedAndContainsARejection` guards against an empty or all-success corpus quietly
passing.

This is deliberately intolerant: a behaviour change in the retry/validation path **should** fail
it. A failure is either a real regression (fix the code) or an intended change (re-record — see
"Recording the fixture corpus" in `Tools/AIPatchHarness/README.md`); never loosen an assertion here
just to turn the suite green again.

## Measurement Harness (not a test)

`Tools/AIPatchHarness` measures how often real model output passes `validatePatch`, replaying a
fixed prompt set through the production path and tallying rejections by error value. It needs a
live Ollama, so it is opt-in and never built by CI:

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIPatchHarness
./build/Tools/AIPatchHarness/AIPatchHarness --model <tag> --runs 2
```

See `Tools/AIPatchHarness/README.md` — in particular that `format` enforcement is backend-dependent
and that `OllamaProvider` times out at 240 s (`kChatRequestTimeoutMs`), which shows up as a
provider error rather than a rejection. `--json` records include the raw model text (`rawResponse`,
`retryResponses`) behind each outcome — that's the offline corpus the fixture-replay tests above
are built from.

**Determinism and cost ceilings (P1-11).** Unlike `AIEvalHarness` below, this harness pins
`--temperature 0` and a fixed `--seed` by default — needed so repeat runs are comparable enough to
ratchet against (unpinned sampling swings ~8 points run to run). `--runs` is hard-capped at 10, and
`--max-requests` bounds total outbound requests for the whole invocation (required, with no
default, for `--provider remote`, since that path can reach a paid vendor). See
`Tools/AIPatchHarness/RequestBudget.h` — its `RequestBudget` class is covered directly by
`Tests/AI/RequestBudgetTests.cpp`, independent of any live model.

`.github/workflows/ai-eval-nightly.yml` runs this harness on a schedule, OFF by default (gated on
the `AI_EVAL_ENABLED` repository variable) — see "Nightly scheduled eval (P1-11)" in
`Tools/AIPatchHarness/README.md` for the full switch/runner/ratchet story. It never blocks a merge:
it isn't a `pull_request`/`push` workflow, and `scripts/ai-eval-ratchet.sh` (unit-tested by
`scripts/tests/ai-eval-ratchet.test.sh`, run by the Lint job) only fails *that job*, comparing
against a committed baseline that doesn't exist yet as of this writing — the script is inert
(reports, exits 0) until a human commits one.

`Tools/AIEvalHarness` scores a different thing: of the patches that pass validation and apply, are
they actually usable — has an output, that output is reachable from a real sound source, every
parameter in range? It replays 40 golden prompts and runs `Source/AI/PatchEval.h`'s checks against
the resulting graph. Same opt-in flag:

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIEvalHarness
./build/Tools/AIEvalHarness/AIEvalHarness --model <tag> --runs 2
```

The structural checks themselves (`evaluatePatch()`) have no model dependency and are covered by
`Tests/AI/PatchEvalTests.cpp` in the regular suite — only the golden-prompt replay needs Ollama.

**Reproducibility/investigation flags**, `--provider ollama` only (all unset by default —
this is for comparing corruption/rejection rates before and after a candidate fix, not a
production knob):

```bash
# pin sampling so a before/after comparison isn't confounded by run-to-run variance
./build/Tools/AIEvalHarness/AIEvalHarness --model gpt-oss:20b --think false --seed 42 --temperature 0 \
  --json eval-results/my-run.json

# exercise getPatchSchemaWithTimelineOps() instead of getPatchSchema() — same request path
# (OllamaProvider::processRequest -> `format` field), the extended schema. Works on every build.
./build/Tools/AIEvalHarness/AIEvalHarness --model gpt-oss:20b --mode timeline --json eval-results/my-timeline-run.json
```

`--mode timeline`'s summary reports `timelineOps present in response` and
`timelineOps corrupted/rejected` — the latter via
`AIIntegrationService::extractTimelineOps()` + `previewTimelineOps()` (validated, never applied),
the timeline-side equivalent of the `applyError` corruption proxy the plain patch mode already
surfaces (a corrupted choice value shows up there as `"Invalid value for choice parameter …"`).

Note that `evaluatePatch` is not only a scoring function: `AIIntegrationService::applyPatch` gates on
`sourceReachesOutput` and surfaces `detail` to the user via `getLastPatchError()`, so those strings are
a contract two `AIIntegrationServiceTest` cases assert verbatim. `SamplerAloneDoesNotCountAsAReachableSource`
/ `SamplerAlongsideAnOscillatorStillPasses` cover the one module that is a sound source in the library
but deliberately not one here (a Sampler is silent until a file is loaded, and a model-authored patch
cannot load one).

## Testing Cloud-Gated Features Locally (not a test)

Pro-gated features — cloud conversation history, `x-conversation-id` threading — only do anything
interesting against a real server from the private backend repo; the local `AIChatComponent` test suite fakes
the backend (`setHistorySourcesForTesting`), so exercising the real client/server contract needs an
actual server running.

**Fast path:** `scripts/run-local-cloud-dev.sh` does everything below in one command — starts a
disposable local Postgres (Docker), migrates it, starts the private backend repo's API server against it
(dev IdP, real Ollama inference, auto-picks a sane model if `llama3.1` isn't pulled), then launches
this repo's locally-built Debug app with the override already set:

```bash
scripts/run-local-cloud-dev.sh            # start everything + launch the app
scripts/run-local-cloud-dev.sh --down     # stop the API server and Postgres container
```

Requires a checkout of the private backend repo as a sibling directory by default (override with
`SYNTH_PLATFORM_DIR`) and a Debug `AgentSynth` build already present (`BUILD_DIR`, default `build`
— see `## Build` above). It prints the exact Settings/sign-in steps once the app launches. The rest
of this section explains what it's doing and why, for when something needs debugging by hand.

There are two hosts involved, and only one of them was previously configurable:

- **Chat / `patch.generate`** (`RemoteProvider`) — already user-configurable via the Settings
  "Host" field for that provider. No change needed here.
- **Auth / entitlement / cloud history** (`AccountService`/`AuthClient`, the `GET`/`DELETE`
  `/v1/conversations*` endpoints) — hardcoded to production (`synth::branding::kApiBaseUrl`) with
  no UI to override it. `synth::branding::resolveApiBaseUrl()` (`Source/Branding.h`) now reads a
  `AGENTSYNTH_LOCAL_API_URL` environment variable in Debug builds only, so this can be redirected
  without hand-editing `Branding.h` and rebuilding per URL change.

Run the private backend repo locally first (in-memory stores by default — no Postgres needed for
this flow). See that repo's own `docs/local-development.md` for the full setup; the short version:

```bash
pnpm --filter @platform/api dev   # serves http://localhost:8787
```

Then, to test a Pro-gated flow end-to-end:

1. Start the private backend repo locally as above.
2. In AgentSynth's Settings, set the chat provider's Host field to `http://localhost:8787`.
3. Launch the locally-built Debug app with the override set:

   ```bash
   AGENTSYNTH_LOCAL_API_URL=http://localhost:8787 "./build/AgentSynth_artefacts/Debug/Agent Synth.app/Contents/MacOS/Agent Synth"
   ```

`AGENTSYNTH_LOCAL_API_URL` is compiled out of Release builds entirely (`#ifndef NDEBUG`) — see the
comment on `resolveApiBaseUrl()` in `Source/Branding.h` for why.

Production conversation storage is the backend's conversation-history store — Neon-managed
Postgres, 180-day retention; local dev's in-memory store implements the identical
`ConversationStore` interface, so this flow still exercises real client/server contract behavior
even without a real Postgres in the loop.

## Adding Tests for New Modules

When adding a new audio module:

1. **Unit tests** in `Tests/<ModuleName>Tests.cpp` — test DSP output, parameter handling, edge cases
2. **E2E coverage** — add the module's name string to the `moduleTypes` array in `E2EWorkflowTest.DropAllModuleTypes_NoCrash`
3. **Add to `Tests/CMakeLists.txt`**

## Snapshot Testing

The `AudioRenderingTests` suite compares rendered audio against "golden" reference files stored in `Tests/reference/`.

- **To run**: `./build/Tests/Tests --gtest_filter="AudioRenderingTest.Snapshot*"`
- **To update references**: If you intentionally change DSP logic (e.g., a better filter algorithm) and want to update the baseline:
  ```bash
  bash scripts/update-reference.sh
  ```
- **To listen**: Use `scripts/play-reference.sh <filename>` (requires `ffplay` or `aplay`).

## Build

```bash
cmake -S . -B build
cmake --build build
```

`ENABLE_TESTS` defaults `OFF` — pass `-DENABLE_TESTS=ON` to generate the `Tests` target. `ENABLE_COVERAGE` is a separate opt-in flag used by the Ubuntu CI job and `scripts/coverage.sh`.

### Plugin target (`ENABLE_PLUGIN`)

`ENABLE_PLUGIN` defaults `ON`, so the plain `cmake --build build` above also builds `AgentSynthPlugin` — VST3 on every platform, plus AU on macOS — from the same `AudioEngine`/`MainComponent` code the standalone app uses (see [`docs/architecture.md`](architecture.md#plugin-layer)). Disable it for a faster app-only local loop:

```bash
cmake -S . -B build -DENABLE_PLUGIN=OFF
cmake --build build
```

PR CI (`.github/workflows/ci.yml`) does not override `ENABLE_PLUGIN`, so its default-`ON` build compiles the plugin as part of the normal `cmake --build build` step on all three platforms it runs (Ubuntu, macOS, Windows) — a shared-code change that silently breaks the plugin wrapper fails the same job as the app, with no separate opt-in required.

## Git Hooks

Install once per clone (hooks are **not** auto-installed):

```bash
bash scripts/install-hooks.sh
```

Two hooks are registered:

- **pre-commit** (`scripts/pre-commit-lint.sh`): runs `clang-format --dry-run --Werror` on staged `Source/` and `Tests/` C/C++ files (skipped entirely when no C++ is staged), then [the file-size guard](#file-size-cap-lint-job) against the whole tree, unconditionally, for any commit with staged changes. Fast; mirrors the CI Lint job. Also warns if the local `clang-format` version differs from the pin in `.clang-format-version`.
- **pre-push** (`scripts/ci-local.sh`): the full local CI reproduction — see [Local CI reproduction](#local-ci-reproduction) below. The first push configures the `build-ci-local/` directory; subsequent pushes are fast incremental rebuilds (ccache + Ninja are picked up automatically when installed).

If you installed the hooks before this change, re-run `bash scripts/install-hooks.sh` — the generated `pre-push` hook is a static file and still points at the old `scripts/pre-push-release-test.sh`, which no longer exists.

Bypass a single invocation with `--no-verify`:

```bash
git commit --no-verify
git push --no-verify
```

Run manually at any time:

```bash
bash scripts/pre-commit-lint.sh  # lint staged files
bash scripts/ci-local.sh         # everything the pre-push hook runs
```

**clang-format version note:** clang-format is pinned via the PyPI `clang-format` wheel to the version recorded in `.clang-format-version`. CI installs that exact version with `pip install "clang-format==$(cat .clang-format-version)"` (after `actions/setup-python`), so CI and the local hooks run the identical binary — eliminating "hook passes locally but CI fails" drift. Install or update locally with the same command:

```bash
pip install "clang-format==$(cat .clang-format-version)"
```

### Local CI reproduction

`scripts/ci-local.sh` is the single source of truth for "what CI will check, run locally" — it is what the pre-push hook runs, and what a developer can run by hand to get the same signal without waiting on a CI round-trip. It reproduces `.github/workflows/ci.yml`'s `Lint` job plus this machine's platform build-and-test job:

```bash
bash scripts/ci-local.sh                # run every check
bash scripts/ci-local.sh --open         # ...then `open` the built app bundle on macOS
bash scripts/ci-local.sh --skip-tests   # skip step 7 (the Tests suite) for a faster local loop
bash scripts/ci-local.sh --help         # usage
```

What it does, in order (fast checks first, so a lint failure doesn't wait on a full build):

1. `clang-format --dry-run --Werror` over `Source/` `Tests/` `Tools/` — the Lint job's "Check Formatting" step, exactly. **Check-only, never `-i`** — a violation fails loudly instead of being silently rewritten.
2. `bash scripts/utf8-literal-check.sh` against the real tree — the Lint job's "Check for un-decoded UTF-8 escapes" step, run directly rather than only via its unit test.
3. `bash scripts/check-file-sizes.sh` against the real tree — the Lint job's "Check file sizes" step ([File-size cap](#file-size-cap-lint-job) below), run directly rather than only via its unit test.
4. `bash scripts/check-function-sizes.sh` against the real tree — the Lint job's "Check function sizes" step ([Function-size cap](#function-size-cap-lint-job) below), run directly rather than only via its unit test.
5. Every `scripts/tests/*.test.sh` (globbed, so a newly added one is picked up automatically without editing this script) — as of this writing `ci-cache-check`, `ci-install-linux-deps`, `check-nonascii-literals`, `ai-eval-ratchet`, `utf8-literal-check`, `check-file-sizes`, `check-function-sizes`, `dev-sign-app`, `ci-local-deps-reuse`. `check-nonascii-literals.test.sh`'s last case scans the real `Source/` tree itself, so this also covers the Lint job's ASCII-literal gate on live code, not just the checker's fixtures.
6. Configure `build-ci-local/` with `-DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DENABLE_AI_HARNESS=ON` (matching the macOS/Windows build-and-test jobs) and build with a plain `cmake --build` — every target those jobs build (`Core`, `AppUI`, `AgentSynth`, `AgentSynthPlugin`, `Tests`), the same way a missing `CMakeLists.txt` entry shows up in CI. ccache and Ninja are picked up automatically when installed (see the `find_program(CCACHE_PROGRAM ccache)` block at the top of `CMakeLists.txt`), so a second run is an incremental rebuild, not a cold one. On the **first** configure only, and only when this checkout is a git worktree, it also reuses the main checkout's already-fetched dependency sources — see [Worktree dependency-source reuse](#worktree-dependency-source-reuse) below.
7. Dev-sign the built app bundle (macOS only): `bash scripts/dev-sign-app.sh "$APP_PATH"`. Not a CI check — it runs only locally, after the build, before the test suite so a signing failure surfaces early rather than after a multi-minute test run.
8. Run the full suite: `build-ci-local/Tests/Tests`. Skippable with `--skip-tests` (default off — the pre-push hook and CI both expect the full suite) for a faster local iteration loop; every earlier step, including dev-signing, still runs, so a `--skip-tests` rebuild keeps the same TCC identity for a live/manual app run.

On success it prints the path to the built `Agent Synth.app` bundle under `build-ci-local/` (found the same way `build-artifacts.yml` locates it for packaging) so a green terminal isn't the only thing you're left with — you can open and try the real app. `--open` does that automatically (macOS only; a no-op notice on other platforms, since the flag also needs to be safe to leave off in a headless/CI-like run).

#### Worktree dependency-source reuse

A freshly created `git worktree add` checkout starts with no `build-ci-local/` at all, so its first configure has nothing in `build-ci-local/_deps` and FetchContent re-downloads JUCE (~600 MB), GoogleTest and Sparkle from scratch — originally observed taking 15-20 minutes before a single line of C++ compiles, even though the main checkout sitting right next to it (under `agentsynth-suite/`) already has those exact sources on disk. Found 2026-09-15 running two worktrees in parallel for the mixer epic, where that repeated every time. Measured on the machine that fixed it: **4m 12.7s** for a cold configure (no ninja installed, so the default Makefiles generator) down to **32.0s** reusing the main checkout's sources — the gap depends heavily on network conditions to GitHub, which is why the two numbers differ so much.

`scripts/ci-local.sh` now avoids the re-fetch on the **first** configure of a worktree (once `build-ci-local/CMakeCache.txt` exists, later configures are already fast, so this never applies again). The logic lives in `scripts/lib/deps-reuse.sh` (sourced by `ci-local.sh`, unit-tested directly by `scripts/tests/ci-local-deps-reuse.test.sh` against fixture directory trees — no cmake, no compiler, no real worktree needed):

- Detects a worktree via `git rev-parse --git-common-dir` pointing outside the checkout itself (a plain checkout's common dir resolves back to its own `.git`).
- **Only** reuses when `cmake/DependencyVersions.cmake` is byte-identical between the worktree and the main checkout — a pin bump made in the worktree (not yet merged to `main`) must never silently build against the main checkout's OLD sources.
- Passes `-DFETCHCONTENT_SOURCE_DIR_JUCE` / `_GOOGLETEST` / `_SPARKLE` pointing at `<main checkout>/build-ci-local/_deps/{juce,googletest,sparkle}-src` for whichever of those the main checkout has already fetched (partial reuse is fine — a source CMake still fetches on either side is fetched normally).
- `CI_LOCAL_NO_DEPS_REUSE=1` opts out unconditionally, forcing a normal from-scratch fetch.
- Prints exactly one line saying what was reused, or why not (not a worktree, pins differ, nothing fetched yet in the main checkout, or the opt-out env is set).

**ccache settings for running worktrees in parallel:** the dependency-source reuse above only covers the FetchContent download; the compiler cache is a separate concern and needs its own configuration to actually share hits across sibling worktrees. The default `max_size` (5 GiB) is sized for one checkout, not several building concurrently — measured at 99.95% full with 5,901 cleanups and a 15.9% hit rate running two worktrees side by side, because evictions were racing the builds. `hash_dir=true` (ccache's default) additionally bakes the compiling directory's absolute path into the cache key, so two worktrees compiling the same source at different paths can never share a hit at all. Fix once, globally (not a repo setting — this is local machine config):

```bash
ccache --set-config max_size=60G
ccache --set-config base_dir=<projects root>       # e.g. ~/Documents/projects/agentsynth-suite
ccache --set-config hash_dir=false
```

`base_dir` plus `hash_dir=false` is what makes this safe: ccache rewrites any path under `base_dir` to a relative one before hashing, so object code that only differs by which worktree compiled it still hits, while paths outside `base_dir` (e.g. a stray absolute include from a system header) are left alone.

**Dev-signing (`scripts/dev-sign-app.sh`) — why the app re-signs itself on every local build:**

A local ad-hoc/linker-signed build's designated requirement pins to `cdhash H"..."`, which changes on every build (any changed byte mints a new hash). macOS TCC (the microphone/camera/etc. permission system) keys its grants on the designated requirement, so every fresh local build looks like a brand-new app to TCC — it re-prompts for microphone access, and that dialog is one automation cannot dismiss, stalling live app testing on a human click.

`ci-local.sh` fixes this by re-signing the built bundle with a stable **Apple Development** identity after every local build:

```bash
bash scripts/dev-sign-app.sh "$APP_PATH"
```

- On Darwin, it picks the first identity from `security find-identity -v -p codesigning` whose name starts with `Apple Development:`, then runs `codesign --force --deep --sign "$identity"` followed by `codesign --verify --deep --strict`. Signed with such an identity, the app's designated requirement becomes `identifier "com.agentsynth.app" and anchor apple generic and certificate leaf[subject.CN] = "Apple Development: <name> (<team>)" and ...` — identical across rebuilds (same identity, same requirement), so TCC keeps recognising the app. **The first launch after switching to a stable identity still asks once; every later rebuild signed with the same identity does not.**
- Override the identity (or opt out) with `AGENTSYNTH_DEV_SIGN_IDENTITY`: set it to an identity name to force that one, or to `-`/`none` to deliberately skip signing.
- If no Apple Development identity is installed (including every CI runner — see below), it prints a note and exits `0` rather than failing the build; the app just stays ad-hoc signed and TCC re-prompts as before.
- A signature that fails to apply or verify is treated as a real error (non-zero exit), unlike a missing identity.
- This is local dev tooling only: it never runs in CI (no Apple Development identity on CI runners, so it always takes the "no identity found" no-op branch there — `scripts/tests/dev-sign-app.test.sh` covers the identity-selection logic itself, with fake `uname`/`security`/`codesign` shims, on every CI platform including Linux), and it never touches the release packaging path — `build-artifacts.yml` keeps its own ad-hoc `codesign -s -` for distributed builds. No hardened runtime, no entitlements: this is a plain re-sign for local trust, not a distributable/notarizable signature.
- Why the prompt appears at all when audio input is opt-in: on a fresh launch the app requests **0** input channels and JUCE builds an output-only device (no input stream object, no `AudioIODeviceCombiner` — traced through `AudioDeviceManager::insertDefaultDeviceNames` and `CoreAudioIODeviceType::createDevice`). The prompt has been observed on a dev machine whose default output device is a combined-I/O USB interface that is also the default input device; starting that device for output appears to be enough for macOS to ask. So a mic prompt at launch on such a machine is not, by itself, a regression of the opt-in contract — check the default device (`system_profiler SPAudioDataType`) before debugging the app. Signing makes one Allow persist; it does not stop the first ask.

**Deliberately not reproduced**, all covered elsewhere: the Ubuntu coverage gate (`bash scripts/coverage.sh`, a separate opt-in — see [above](#build)), the label-gated ASAN job (opt-in per PR via the `run-asan` label, not something to run on every push), and actual cross-platform compilation (this only exercises the toolchain installed on the machine it runs on — Linux/Windows failures still need CI or a VM).

## CI Pipeline

CI runs via `.github/workflows/ci.yml` on pull requests to `main` **and on pushes to `main`**, path-filtered to changes under `Source/**`, `Tests/**`, `Tools/**`, `CMakeLists.txt`, `Tests/CMakeLists.txt`, `cmake/**`, `scripts/**`, or either workflow file (`ci.yml` / `build-artifacts.yml`).

The push-to-`main` trigger exists **to seed the build caches** — see [Build caching](#build-caching). Removing it silently doubles every PR's build time, so it is load-bearing, not redundant with the artifact workflow. `ci.yml` also has a `workflow_dispatch` trigger for exactly one purpose: re-seeding a cache by hand. PR runs restore only and never save (see below), so if main's cache for an OS is ever evicted or 7-day-GC'd, no PR can fix that itself — run "Agent Synth CI" via workflow_dispatch on `main` (Actions tab → select the workflow → Run workflow, branch `main`) to seed it without waiting for a real commit. It's build-only on any ref (`lint`/`Run Tests`/`Check Coverage` all gate on `pull_request`, and `CACHE_WARM_EXPECTED` reads false for `workflow_dispatch`, so a cold cache during the run itself is reported as expected, not a failure) — but it only *saves* when run on `main`, per the save-gate rule below.

**Push runs build only.** Linting, test execution and the coverage check are gated to `pull_request`: they already ran on the PR against this same tree, so repeating them on the merge result costs runner minutes without adding signal — the position `build-artifacts.yml` has always taken for the release build. This does not weaken the cache, because `cmake --build build` still compiles the `Tests` target; only *running* the binary is skipped, so every test translation unit still lands in ccache. It trims a push run from ~9.4 to ~6 runner-minutes; the macOS job was the worst offender, an 8-second cached build carrying 118 seconds of tests.

What this gives up: a post-merge run no longer catches two PRs that are each green alone but conflict once both land. The exposure is small — the next PR's CI runs against the merged result, and the release build still fails if the merge does not compile.

> **Do not rename the `Lint`, `Build, Test, and Coverage`, `Build and Test (macOS)` or `Build and Test (Windows)` jobs.** Those four strings are configured as required status checks in `main`'s branch protection; renaming one leaves a required check permanently pending and blocks every merge. Skipping a job on `push` is safe (protection only gates pull requests) — renaming it is not.

A `concurrency` group cancels superseded runs on the same PR (main is never cancelled, since those runs seed the cache).

There are **five jobs**:

| Job | Runner | Config | Notes |
|-----|--------|--------|-------|
| **Lint** | `ubuntu-latest` | — | Installs the pinned `clang-format` PyPI wheel (version from `.clang-format-version`) via `pip install "clang-format==$(cat .clang-format-version)"` after `actions/setup-python`; runs `--dry-run --Werror` over `Source/` and `Tests/`. Fast (~30 s) — gives formatting feedback without waiting for a full build. |
| **Build, Test, and Coverage** | `ubuntu-latest` | Debug + clang + `ENABLE_COVERAGE=ON` | Runs tests, then `bash scripts/coverage.sh --report-only` (skips re-build; only merges profdata and checks the 85% line-coverage threshold). |
| **Build and Test (ASAN)** | `ubuntu-latest` | `RelWithDebInfo` + `-fsanitize=address` | **Label-gated** — only runs when the PR carries the `run-asan` label. `ASAN_OPTIONS=detect_leaks=0`. |
| **Build and Test (macOS)** | `macos-latest` | Release | Catches UB/segfaults and cross-platform issues. |
| **Build and Test (Windows)** | `windows-latest` | Release | Catches UB/segfaults and cross-platform issues. |

### Optimizations

- **`JUCE_WEB_BROWSER=0`**: Drops unused WebBrowserComponent and removes WebKit/libsoup deps on Linux.
- **Separate lint job**: Instant formatting feedback without waiting for a full build.
- **apt package caching**: `awalsh128/cache-apt-pkgs-action` caches Ubuntu packages across runs.
- **`coverage.sh --report-only`**: In CI, skips redundant configure/build/test steps and only merges profdata + generates the report.

### Build caching

Two caches carry work between runs. Both are easy to break in ways that look exactly like a healthy build, just slower — which is why `scripts/ci-cache-check.sh` gates them (see below).

- **ccache** — compiler cache. `CMAKE_C/CXX_COMPILER_LAUNCHER=ccache` plus `CMAKE_OBJC/OBJCXX_COMPILER_LAUNCHER=ccache` on macOS (rule 5 below — the launcher is per language), `CCACHE_DIR` pinned explicitly to `${{ github.workspace }}/.ccache` on every platform. Max size is set per OS, measured from what a warm main build actually uses (2026-09-15): **2 GB on Linux** (the Debug+coverage+AI-harness job — a seeded `Linux-ccache-main` entry measured 1.96 GB, already saturating a smaller cap), **512 MB on macOS and Windows** (Release+tests jobs measured 117 MB / 160 MB respectively, so 512 MB is headroom, not the real requirement — every configured byte is a byte a save could write toward the 10 GB repo budget). Keyed `<os>-ccache-<ref>-<sha>`; the restore-keys fall back this ref → `main` → anything. Also sets `compiler_check=content` and `sloppiness=time_macros,include_file_mtime,include_file_ctime`, because `actions/checkout` rewrites every file's mtime each run and the compiler binary's mtime changes whenever GitHub rebuilds the runner image — under ccache's mtime-based defaults both produce false misses.
- **FetchContent** — `build/_deps` (JUCE + GoogleTest sources), keyed on `hashFiles('cmake/DependencyVersions.cmake')`.

#### Six rules, each learned from an outage

1. **`ci.yml` must keep its `push: main` trigger.** GitHub scopes a cache to the ref that wrote it; a PR run can read its own ref and the **base branch**, nothing else. From the introduction of ccache until Aug 2026 this workflow ran on `pull_request` alone, so it never wrote a cache into `main`'s scope and **no PR ever restored one** — every run compiled the entire tree cold for months. The logs said `Cache not found for input keys: …` and CI passed regardless.
2. **Key `build/_deps` on the dependency pins alone**, never on `CMakeLists.txt`. `build/_deps` holds nothing but fetched sources, so adding a module must not invalidate it. The old `hashFiles('CMakeLists.txt', 'Tests/CMakeLists.txt')` key minted a fresh ~350 MB entry per platform per workflow on every module PR; with 10 `CMakeLists.txt` edits in the first nine days of Aug 2026 that pushed the repo past **GitHub's 10 GB per-repo cache limit**, which LRU-evicted the ccache entries too. All pins therefore live in `cmake/DependencyVersions.cmake` and the `FetchContent_Declare()` calls read them from there, so key and pin cannot drift.
3. **`CCACHE_DIR` must be set explicitly.** ccache's default directory varies by platform *and* version. The Linux job cached `~/.ccache` while ccache 4.x on `ubuntu-24.04` writes to `~/.cache/ccache`, so the Linux ccache was never even saved — no `Linux-ccache-*` entry ever existed. In `build-artifacts.yml` the path is a **matrix value**, since one job spans three runners and a job-level override is unavailable.
4. **Cache steps use `actions/cache/restore` + `actions/cache/save`, not the combined `actions/cache`.** The combined action declares exactly one output, `cache-hit`, true only on an *exact* primary-key match — and the ccache primary key embeds `github.sha`, so it never matches exactly even when a restore-key fallback works perfectly. Only `actions/cache/restore` exposes **`cache-matched-key`**, the one value that reports a fallback hit. Reading it off the combined action yields an empty string forever: run `31301691346` restored every cache and compiled at a **100% ccache hit rate** while the check reported `MISS` on all three platforms; enforcement would have failed every build. The split also lets the save step run `if: always()`, so a successful compile is not discarded because a later test failed — every save step also adds `&& github.ref == 'refs/heads/main'` on top of that (see rule 6).
5. **The compiler launcher is per language — `C`/`CXX` do not cover `OBJC`/`OBJCXX`.** CMake treats Objective-C++ as its own language, so until Aug 2026 every `.mm` compile bypassed ccache: **103 translation units per macOS build**, because JUCE ships each of its modules as a single ObjC++ unity file and each target compiles its own copy (Core, AppUI, AgentSynth, the plugin, `Tests`, both AI harnesses). Those units never change, and they were still rebuilt cold on every run — roughly 12 minutes of a 12 min 45 s macOS job, ending in a near-serial tail of 20–35 s `juce_gui_basics` / `juce_audio_processors` compiles while the rest of the build had finished. It survived four earlier cache fixes and the health check itself because it does not look like a cache failure from the outside: the caches restored, and the hit rate read a plausible-looking **54%**, since a compile that never reaches ccache is counted as neither a hit nor a miss. Both `CMakeLists.txt` (for local builds) and `ci.yml`'s macOS job set all four launchers; the audit below is what keeps a sixth language from repeating it.
6. **PR runs restore only; main saves and prunes its previous ccache generation.** Every PR push used to both restore AND save — each attempt minted its own `refs/pull/<n>/merge`-scoped deps entry (if main's didn't hash-match yet) and ccache entry (always, since the ccache primary key is per-commit and never exact-matches on restore), and those entries were never cleaned up until GitHub's 7-day/10 GB-budget GC got to them. On 2026-09-14 that reached **9.86 GB of the 10 GB repo limit**, with PR-scoped entries alone measured at **6.3 GB** (the Linux ccache family alone 5.49 GB) — enough to LRU-evict main's seeded macOS/Windows deps caches, so the next PR restored nothing and `scripts/ci-cache-check.sh` failed it by design; only a rerun (which itself saved a PR-scoped cache) passed. Every `Save FetchContent Dependencies` / `Save ccache` step's `if:` now adds `github.ref == 'refs/heads/main'`; the label-gated ASAN job (which never runs on `push: main`) was switched from the combined `actions/cache` action to `actions/cache/restore` outright, since it can only ever restore. Saving to main doesn't itself solve accumulation, since a ccache key is per-commit and cache entries are **immutable** — every merge mints a new `<os>-ccache-main-<sha>` generation instead of updating one in place, which would re-blow the 10 GB budget in 3-4 merges on its own. So each `Save ccache` step is followed by a **`Prune superseded main cache generations`** step (`gh cache list`/`gh cache delete`, needs job-level `permissions: actions: write`) that runs only after a successful save (never `if: always()` — that would delete every generation including main's only good one on a failed/skipped save), keeps the 2 most recent generations per OS family regardless, and only ever deletes entries strictly older than the one this run just saved — never "everything but mine" — so a run working off a stale cache listing can't delete a sibling run's fresher save. `build-artifacts.yml`'s own `<os>-release-ccache-<sha>` family gets the same treatment. `ci.yml`'s `concurrency` group queues (never cancels) main pushes, so two prune runs for the same OS family never race in practice; if one ever were cancelled as superseded before its save step, that run simply never saves or prunes, and the next queued run's save (against the now-current tree) covers it. `build/_deps` is left unpruned by design here beyond the one-line sweep folded into the same step (keyed on the dependency-pins hash alone, not per-commit, so it only grows when a pin changes) — and the two workflows' deps caches are deliberately **not** merged into one key despite fetching from the same `cmake/DependencyVersions.cmake` pins: `ci.yml` builds with `ENABLE_TESTS=ON` (populates GoogleTest) and `build-artifacts.yml` never sets it, so sharing a key risks whichever job's `Build` step finishes first permanently winning the cache-populate race for a new hash — if that's the no-tests release job, `ci.yml` would silently lose GoogleTest from its restored cache and re-fetch it on every run with no test failure and nothing for the health check to catch.

#### The cache health check

`scripts/ci-cache-check.sh` runs after the build on Linux, macOS, and Windows. It reads each cache step's `cache-matched-key` output plus `ccache --show-stats`, writes a summary table to the job summary, and:

- **fails** when a cache that should have restored did not;
- **fails** when any `C`/`CXX`/`OBJC`/`OBJCXX` compile generated under `build/` does not invoke ccache (the launcher audit). It reads **both** `build/CMakeFiles/rules.ninja` and `build/build.ninja`, because CMake splits the launcher across them: the rule's command holds a `${LAUNCHER}` placeholder — the word `ccache` never appears in it — while the real path is a per-build-statement `LAUNCHER =` variable. The audited unit is therefore the build **statement**, one per translation unit, which is also the number worth reporting (103 `.mm` files, not the ~10 ObjC++ rules they share). A launcher inlined into the rule command, as older CMake did, still counts as wired. Two earlier versions of this audit passed against simplified fixtures and then failed in CI, so the fixtures now reproduce the real two-file layout verbatim; It parses the generator's own output rather than the workflow, so it catches the fault however it arrives — a new language, a dropped `-D` flag, a CMake upgrade. Link rules, the Windows resource compiler and the C++20 module-scan rules are out of scope: ccache does not handle them. If `build.ninja` exists but no compile rule matches the expected `rule <LANG>_COMPILER__<target>` naming, that **warns** — an audit that silently matches nothing is the same blind spot it was added to close;
- **warns** when the ccache hit rate is under `CACHE_MIN_HIT_RATE` (default 25%), which drops legitimately whenever a PR touches a widely-included header. Note what the rate cannot tell you: it is a ratio over the calls that *reached* ccache, so it never detects an unwired language — that is the audit's job.

`CACHE_WARM_EXPECTED` decides which runs are held to that standard. It is true **only for pull requests from a branch in this repository**. Two cases are legitimately cold and are reported as a notice instead:

- the **push-to-`main`** run, which is what seeds the cache in the first place;
- a **pull request from a fork** — GitHub gives forks an isolated cache scope with no read access to the base repository's entries, so a miss is guaranteed and is nothing the contributor can fix. Without this exemption, enforcement would fail every outside contribution on its first run.

Enforcement is controlled by the repository variable **`CI_CACHE_CHECK_ENFORCE`** (Settings → Secrets and variables → Actions → Variables), **currently `true`** — a cold cache fails the build. It is a variable rather than a hard-coded value so enforcement can be switched off without a code change if a runner-image or `actions/cache` change ever starts producing false alarms.

`build-artifacts.yml` runs the same check but pins it to report-only: losing a release over a cache miss would be a worse outcome than a slow release.

The check also **fails safe**: an empty `cache-matched-key` contradicted by a high ccache hit rate (≥ `CACHE_SELFCHECK_HIT_RATE`, default 50%) is reported as *a misconfigured check*, not a cold cache, and does not fail the build — a cold build cannot hit 100%, since its only hits are files compiled into two targets in the same run (~15%). A genuinely cold cache still fails. The guard keys on the **ccache** contradiction alone: a high hit rate proves the ccache restored and both keys share the same plumbing, but it proves nothing about `build/_deps`, so a deps-only miss with a populated ccache key is still a real failure.

The script has its own tests (`scripts/tests/ci-cache-check.test.sh`, 22 cases, run by the Lint job): if the thing that detects a broken cache breaks silently, we are back to shipping cold builds unnoticed. Those tests scrub every input variable before each case — `ci.yml` exports `CACHE_CHECK_ENFORCE` and `CACHE_WARM_EXPECTED` workflow-wide, the Lint job inherits them, and a non-hermetic harness silently inherited CI's values and passed cases it should have failed.

To inspect cache state directly:

```bash
gh api "repos/:owner/:repo/actions/caches?per_page=100" \
  -q '.actions_caches[] | "\(.size_in_bytes/1048576|floor)MB\t\(.ref)\t\(.key)"'
# Total size — evictions start once this approaches GitHub's 10 GB limit:
gh api "repos/:owner/:repo/actions/caches?per_page=100" -q '[.actions_caches[].size_in_bytes]|add/1073741824'
```

#### Dependency install: the apt mirror is not reliable

The Linux job in `ci.yml`, and the Linux leg of `build-artifacts.yml`'s build matrix, both install their build dependencies through `scripts/ci-install-linux-deps.sh`, not a bare `apt-get`, because GitHub's ubuntu runners resolve the archive through `/etc/apt/apt-mirrors.txt` — `azure.archive.ubuntu.com` first, `archive.ubuntu.com` as fallback — and when the Azure mirror is degraded **apt does not fail fast**. Its default `Acquire` timeout is 120 s with retries, applied per index and per package, so ~30 packages become a multi-minute or multi-hour stall that still ends in a green build. The log signature is a run of `Ign: http://azure.archive.ubuntu.com/… InRelease` lines followed by a `Hit:` on `archive.ubuntu.com`: apt burning its whole retry budget before failing over to the mirror that works.

`build-artifacts.yml` installed its Linux packages through `awalsh128/cache-apt-pkgs-action` instead, until 2026-08-25: that action resolves and pins the *exact* currently-available package versions up front (for its cache key), then installs those exact `.deb`s with no retry. When the Azure mirror's index had already rotated past one pinned version (a routine point-release bump), the fetch 404'd and the whole release build failed outright — the same class of mirror flakiness the script above exists to survive, just with no failover at all instead of a slow one. Both workflows now share this one script.

On 2026-08-19 this step degraded across one morning — 19 s, then 2m12s, 3m36s, 4m38s, 18 min+ — and on 2026-08-18 a single job sat in it from 15:53 to 21:54. **Six hours, in a step that normally takes 20 seconds**, and it was invisible because a slow success looks like a healthy build.

The script therefore: caps each apt call (15 s per attempt, 2 retries, instead of apt's 120 s default, and 60 s total for `update` / 300 s for `install`); on a stall, rewrites the mirror list to `https://archive.ubuntu.com` and retries — note the scheme, since the runner lists Azure over cleartext while the fallback already serves TLS; and treats a second failed `update` as non-fatal, letting the *install* decide the exit status, since the image ships usable indexes. The healthy path is unchanged: a working Azure mirror is genuinely faster (same datacenter), so this switches on failure rather than hard-coding the fallback.

Those caps come from measurement, not taste. Healthy: `update` 5-15 s, `install` ~40 s. During the live outage (run `32232448856`) the first `update` burned its whole cap, the rewrite took milliseconds, and the retry fetched 10.7 MB in 2 s with `install` fetching 31.4 MB in 2 s — so on a sick mirror the cap *is* the cost, and it wants to be the smallest value that cannot fire on a slow-but-alive mirror. The step recovered in 3m19s at the 180 s originally shipped, which is why it is 60 s now.

`timeout-minutes: 15` on the step is the backstop, not the mechanism — every package-manager step has one now (Linux apt, macOS brew, the ASAN job's cached-apt action), because a package manager with no ceiling stalls until the 6-hour job limit instead of failing. Failover is covered by `scripts/tests/ci-install-linux-deps.test.sh` (13 cases, Lint job) against a fake `apt-get`: a path that only runs during an outage otherwise gets tested by the outage. One of those cases exists because `sed -i` takes a mandatory backup suffix on BSD sed and none on GNU sed, so the original rewrite edited the file in CI and silently did nothing on macOS.

### String literals are ASCII-only (Lint job)

`scripts/tests/check-nonascii-literals.test.sh` (15 cases, ~1 s, no compiler) fails the Lint job when a `Source/**.{cpp,h}` line puts a non-ASCII byte inside a double-quoted literal. The reason is a JUCE contract that nothing else enforces: `juce::String`'s `const char*` constructor decodes its bytes with `CharPointer_ASCII` — **Latin-1, not UTF-8** — so `"Rename…"` reaches the UI as `"Renameâ€¦"`, one mojibake glyph per byte. A hex escape (`"Rename\xe2\x80\xa6"`) is the *identical* three bytes and fails the same way; that spelling is how the bug shipped a second time after the first "fix", which is why the checker flags `\x`/`\u` escapes above `0x7F` as well as raw bytes. Write plain ASCII, or declare the encoding with `juce::CharPointer_UTF8` / `juce::String::fromUTF8` — a line mentioning either is exempt.

Scope and limits, all deliberate: **comments are exempt** (this codebase writes prose em dashes throughout them and they never reach `juce::String`); **`Tests/` is out of scope** (its non-ASCII lives in gtest `<<` streams, which go to a `std::ostream` and render fine); a line opening a **raw string literal** (`R"(...)"`) is skipped rather than parsed, since it has its own quoting rules; and the scanner is a byte-level state machine (`scripts/nonascii-literals.py`) that tracks string/char/line-comment/block-comment state, not a C++ parser — it does not model octal escapes or line continuations inside a literal.

### File-size cap (Lint job)

`scripts/tests/check-file-sizes.test.sh` (24 cases, ~1-2 s, no compiler; the last-but-one case runs the harness itself under a hook-style `GIT_DIR` and requires the hook's repository to come back untouched — every harness that builds fixture repos, and `ci-local.sh` which the pre-push hook runs, strips git's inherited `GIT_DIR`/`GIT_WORK_TREE`/`GIT_INDEX_FILE` first, because a hook-run harness once staged every real file as deleted) and `scripts/check-file-sizes.sh` itself (Lint job's "Check file sizes" step, also run directly from `ci-local.sh` and the pre-commit hook) enforce a hard **1000-line cap** (`FILE_SIZE_CAP`) on every git-tracked source/test/docs/script/config file — one cap for everything, since a 6,000-line test file is exactly as unreviewable as a 6,000-line source file; a per-directory cap would only move the goalposts. The reason it exists: `Source/UI/GraphEditor.cpp` crossed 9,000 lines and several test files passed 6,000 before this guard did — a file that size turns every change into a scroll through unrelated concerns, inflates review diffs with untouched context lines, and makes merge conflicts far likelier between two people editing different features that happen to share a file.

**Strict ratchet baseline** (`scripts/file-size-baseline.txt`) grandfathers files already over the cap at their EXACT current line count, so the cap doesn't force a freeze-and-split-everything-today migration. It only ever tightens:

- A baselined file may never grow past its entry — do that and the check fails, naming the exact grow (`grew from N to M lines`) and pointing at a `<Class><Concern>.cpp` split instead.
- Shrink one and its entry must tighten to match: run `bash scripts/check-file-sizes.sh --update`, which rewrites the baseline from the current tree and prints what changed (a too-loose entry fails the check on its own, naming `--update` as the fix).
- Get a file back under the cap and its entry must be removed entirely — `--update` does that too.
- No file may join the baseline as new; a file crossing the cap for the first time fails outright, naming the split it needs.
- `--update` never raises an entry or adds a new one either (FRO83) — it refuses (exit 1, baseline left untouched) rather than launder a grown or newly-added file through, and a same-size `git mv` is recognized and let through automatically; `--allow-growth` is the deliberate, reviewed exception, printing a `::warning::` per raised/added entry.

`bash scripts/check-file-sizes.sh --list [N]` prints the N largest scanned files (default 25), largest first, regardless of cap or baseline — for picking what to split next.

**How to split an over-cap file** — full rules live in the root `CLAUDE.md`'s "Code structure" section; in short, a class that outgrows one file gets its own directory named after the class holding the header and every unit (e.g. `Source/UI/Graph/GraphEditor/GraphEditor.h` + `GraphEditor<Concern>.cpp` units, never `_Part1`, + shared private helpers in `GraphEditorInternal.h`), never flat siblings dropped next to unrelated files. Tests mirror it (`Tests/UI/Graph/GraphEditor/GraphEditor<Topic>Tests.cpp` with shared fixtures in `GraphEditorTestFixture.h`). A directory itself gets split by area once it passes roughly 30 files — `Source/UI/` now holds only area directories (`Graph/`, `Timeline/`, `PianoRoll/`, `Library/`, `Macros/`, `ModuleViews/`, `Settings/`, `Assistant/`, `Chrome/`, `Layout/`, `Theme/`), each class directory nested in its area (FRO80). `Tests/` mirrors the code by area — `Modules/`, `FX/`, `Engine/`, `Plugin/`, `Timeline/`, `Mixer/`, `Macros/`, `AI/`, `Account/`, `Project/`, `App/` (MainComponent-level and E2E), and `UI/<area>/` matching `Source/UI/`; only shared helpers (`TestMain.cpp`, `TestAudioHelpers.h`, `FakeAudioIODevice.h`, `StubPluginInstance.h`) and the `fixtures/`/`reference/` data stay at the root. Data next to the test tree is resolved from the `TESTS_ROOT_DIR` compile definition, never from a test file's own `__FILE__` depth.

Largest legacy files at the time of writing (see `scripts/file-size-baseline.txt` for the full, current list):

| File | Lines |
|------|-------|
| `Source/UI/GraphEditor.cpp` | 9125 |
| `Tests/GraphEditorTests.cpp` | 6196 |
| `Source/MainComponent.cpp` | 5379 |

Scope and exclusions, all deliberate (see the script's own header comment for the full reasoning): `assets/`, `mockups/`, any local `build*` directory, `.claude/`, and recorded JSON fixture corpora (`Tests/fixtures/`, `Tools/TimelineOpsHarness/Fixtures/`) never count toward the cap — their size reflects recorded data, not hand-authored structure. The guard's own baseline file is excluded from itself.

**Hook-environment gotcha (FRO81):** a git hook process inherits `GIT_DIR` (and sometimes `GIT_WORK_TREE`/`GIT_INDEX_FILE`) from git itself, which broke `ROOT` resolution and made the whole guard pass vacuously (every file read as 0 lines, "0 over the cap") instead of scanning anything — the script now `unset`s those variables up front, fails loudly if `git rev-parse --show-toplevel` still can't resolve `ROOT`, and carries an awk-level fail-safe that refuses to report a clean pass if every scanned file comes back as 0 lines.

**Docs-only PRs are not gated by this in CI.** `ci.yml`'s `paths:` filter (see [CI Pipeline](#ci-pipeline) above) deliberately does not include `docs/**` or `*.md` — adding them would trigger the full build matrix for a docs-only change, which is what the filter exists to avoid. A docs-only PR is instead checked locally, by the pre-commit hook and `scripts/ci-local.sh`, both of which run the guard unconditionally against the whole tree. A mixed code+docs PR is gated normally, since `Source/**` / `scripts/**` / etc. changing triggers the Lint job the guard rides in either way.

### Function-size cap (Lint job)

`scripts/tests/check-function-sizes.test.sh` (26 cases, ~1 s for the fixtures plus the real-tree case) and `scripts/check-function-sizes.sh` itself (Lint job's "Check function sizes" step, also run directly from `ci-local.sh` and the pre-commit hook) enforce a hard **200-line cap** (`FUNCTION_SIZE_CAP`) per function over every git-tracked `*.cpp`/`*.h`/`*.mm` file under `Source/`, `Tests/`, `Tools/` — narrower scope than the file-size guard above (which scans the whole tree; a function only exists in code), same strict-ratchet mechanism otherwise. The reason it exists: root `CLAUDE.md`'s "a new function must never be 200+ lines" rule had nothing enforcing it — a function is where a file-size violation actually starts (one function that grew, not evenly distributed bulk), and it is the more direct signal that a piece of code is doing more than one thing.

**Tool choice: an awk brace-depth scanner, not `clang-tidy`'s `readability-function-size`.** The Lint job never configures CMake — `readability-function-size` needs a `compile_commands.json`, i.e. the full JUCE `FetchContent` + configure + parsing every JUCE-heavy translation unit, in the one job meant to give fast feedback before a real build. A scanner in the same style as `check-file-sizes.sh` (no compiler, no network) runs identically in the Lint job, `ci-local.sh`, and the pre-commit hook. The scanner itself lives in `scripts/function-size-scan.awk` (a separate file from `check-function-sizes.sh`, unlike the file-size guard's inline awk block) because real brace/paren-depth parsing needs several helper functions, and its operator-name handling needs literal `'` characters that would terminate a bash single-quoted script.

**How a function's size is measured:** brace depth, tracked per file. On every `{` not already inside a function, the text since the previous `;`/`{`/`}` at that level (the "header") is classified: `namespace .../extern "C"` and `class`/`struct`/`union`/`enum` (when not itself a function-like macro call) are **transparent** — a member function or a nested type inside either is still classified normally; a `TEST`/`TEST_F`/`TEST_P` macro body is a function named after the whole macro call (`TEST_F(Suite, Case)`); a header with a top-level `(...)` whose close is followed only by whitespace, `const`, `noexcept[(...)]`, `override`, `final`, a trailing `-> type`, or a constructor initializer list (`: ...`) is a real function; anything else (a brace initializer, a bare block) is **not** tracked. Once inside a function, every nested `{` — an `if`/`for`/`while` body, a lambda, a local block — counts toward that ONE enclosing function and is never reclassified, per the spec this guard implements: "lambdas and local blocks count toward the enclosing function." A function's size is `<last line (the closing brace)> - <first line of its signature> + 1`; a multi-line signature's start line is the signature's own first line, not the line the `{` lands on. The function's **name** is the qualified identifier (or operator name — `operator==`, `operator()`, `MainComponent::operator[]`) immediately before its argument list; a name repeated within one file gets `#2`, `#3` suffixes in the order it appears.

Comments, string/char literals and preprocessor lines never contribute real code structure: `//`/`/* */` comments and preprocessor directives are dropped entirely before scanning, and a string/char literal keeps its text (needed so `extern "C"` is still recognized) but has `{`, `}`, `(`, `)`, `;` inside it blanked — several `Tests/*.cpp` files embed multi-line `R"(...)"` JSON fixtures full of braces, and the bare-delimiter raw-string form is tracked across lines the same way. One access-specifier quirk worth knowing: `public:`/`private:`/`protected:` on their own line reset the header buffer exactly like a `;` would (otherwise the NEXT member's header — and its reported start line — would glue onto the specifier's own line); every other use of `:` (inheritance, a constructor initializer list, a ternary) is left alone, since only a header buffer that is EXACTLY one of those three keywords triggers it. See `function-size-scan.awk`'s own header comment for the full method and its "KNOWN LIMITATIONS" section (an explicit-delimiter raw string, `R"DELIM(...)DELIM"`, isn't specially handled — the codebase only uses the bare-delimiter form; code inside a disabled `#if 0`/`#ifdef` block is still scanned as if compiled, since only the preprocessor LINES themselves are dropped, not what they exclude).

**Strict ratchet baseline** (`scripts/function-size-baseline.txt`, `<lines> <path>::<name>` per entry) works exactly like the file-size baseline: grandfathers functions already over the cap at their EXACT current size, only ever tightens (grow past the entry and the check fails naming the function, its `path:line`, and its size; shrink it and `--update` must tighten the entry; get it under the cap and `--update` removes the entry; no function may join the baseline as new — `--update` refuses growth/additions without `--allow-growth`, which prints a `::warning::` per exception). A same-name, same-size function whose FILE git confirms was renamed is accepted by `--update` without `--allow-growth`, the same as a file-size baseline entry surviving a `git mv`.

`bash scripts/check-function-sizes.sh --list [N]` prints the N largest scanned functions (default 25), largest first, regardless of cap or baseline.

Largest legacy functions at the time of writing (see `scripts/function-size-baseline.txt` for the full, current 22-entry list):

| Function | Lines |
|------|-------|
| `SmartConnectionEngine::refreshSmartSuggestions` | 550 |
| `AIStateMapper::applyJSONToGraph` | 409 |
| `MainComponent::commandTable` | 390 |

### What didn't work

- **Unity builds** (`CMAKE_UNITY_BUILD`): Incompatible with JUCE — Obj-C++ `.mm` files cannot be merged into C++ unity translation units.
- **Precompiled headers**: JUCE module `.cpp` files have guards against being pre-included; on macOS, `.mm` files also require Obj-C++ mode which conflicts with a C++ PCH.

### Post-merge artifact builds

After a merge to `main`, `.github/workflows/build-artifacts.yml` triggers automatically. A `version` job runs first: it dry-runs the same tag computation (`mathieudutour/github-tag-action`) the release step below will actually tag with, so the marketing version is known before anything builds. The build matrix then builds and packages the app on Ubuntu, macOS, and Windows (no tests — CI already ran them on the PR) using **Ninja on all three platforms** (Windows via the MSVC dev environment), so the build path matches the PR CI exactly, configuring each leg with that computed version (`SYNTH_MARKETING_VERSION`) so it's baked into the app/plugin/installer. Finally the `release` job tags with that exact same version (via `custom_tag`) and creates a GitHub release with all three platform artifacts — the published tag is therefore guaranteed to equal the version every artifact was built with.

It uses ccache as well, under a distinct `<os>-release-ccache-` key — the Release/no-tests configuration produces different objects from `ci.yml`'s, so the two must not share a cache scope. Until Aug 2026 this workflow had no compiler cache at all and recompiled the whole tree from cold on every merge, even though PR CI had just built the identical commit. `max_size` is a per-OS `matrix.ccache_max_size` value (512 MB on all three legs — a measured `Windows-release-ccache` entry was 42 MB on 2026-09-15, so this is headroom, not a real cap), the same right-sizing as `ci.yml`'s macOS/Windows jobs — see [Build caching rule 6](#build-caching) above. This workflow's `Save FetchContent Dependencies`/`Save ccache` steps carry the same `github.ref == 'refs/heads/main'` gate as `ci.yml`'s (a `workflow_dispatch` dry-run from a branch must not write a cache under that branch's scope either), and `Save ccache` is followed by the same `Prune superseded main cache generations` step pruning the `<os>-release-ccache-` family — its `build/_deps` family is left alone, both because it's hash-keyed (not per-commit) and because it is deliberately kept in its own `release-deps-ninja3` key namespace rather than merged with `ci.yml`'s `deps3`, for the GoogleTest race reason in rule 6. The tag-and-release step runs **only on `push` to `main`** — a manual `workflow_dispatch` run is a build-only dry-run, useful for validating the matrix (including the Windows build) before merging. Docs-only / non-code merges are skipped via a `paths-ignore` filter: pushes that touch only `**/*.md`, `docs/**`, `LICENSE`, `.gitignore`, `.clang-format`, `.clang-format-version`, `.claude/**`, or `mockups/**` produce no build, no version bump, and no release; mixed code+docs merges still release as normal.

The Windows leg's `Install NSIS` step installs via `choco install nsis -y` and then explicitly resolves `makensis.exe`'s directory and appends it to `$GITHUB_PATH`. This is not optional plumbing: `choco install` only updates the *machine registry* PATH, and every later step in a GitHub Actions job is a fresh process that inherited its environment at job start — it never re-reads the registry mid-job. From PR #241 (2026-08-20) until this was fixed on 2026-08-25, that meant `makensis` was invisible to the very next step, `Build Windows installer`, and **every single release build failed** ("makensis: The term ... is not recognized"). `$GITHUB_PATH` is the one channel GitHub Actions does re-read before each step, which is why the fix publishes through that instead of `$env:Path`.

Fixing the PATH issue unmasked a second, independent bug in `installer/windows/AgentSynth.nsi` that had never once actually run in CI: its `OutFile` directive was `"installer\windows\AgentSynthSetup.exe"`, on the (wrong) assumption that NSIS resolves a relative `OutFile` against makensis's invocation cwd. NSIS actually resolves it against **the `.nsi` script's own directory** — already `installer\windows\` — so the real path doubled to `installer\windows\installer\windows\AgentSynthSetup.exe` and makensis failed with "Can't open output file." `OutFile` is now the bare filename `"AgentSynthSetup.exe"`, which resolves to the same `installer\windows\AgentSynthSetup.exe` that `Package Windows Artifact` and the manual verification steps above both already expect.
