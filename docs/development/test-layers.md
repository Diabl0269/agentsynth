# Test Layers

What each suite in the GoogleTest binary covers, grouped by layer. Every suite is headless — no
audio device, no GUI window. `./build/Tests/Tests` reports the authoritative suite and case counts;
this doc names suites and their coverage, never counts, so it cannot go stale by arithmetic.

How to run the suite, and the conventions every test here follows, are in
[`testing.md`](testing.md) and [`test-patterns.md`](test-patterns.md).

## Audio rendering

Headless DSP tests that render audio through individual modules and verify output characteristics —
RMS levels, silence detection, frequency response, waveform accuracy.

| Suite | What it covers |
|-------|----------------|
| OscillatorTest | Waveform generation (sine, saw, square, triangle), MIDI response, tuning, frequency accuracy |
| FilterTest | Low-pass/high-pass filtering, cutoff/resonance parameters, frequency response across 7 filter types |
| EnvelopeGeneratorTest | `Tests/Modules/Envelope/EnvelopeGeneratorTests.cpp` — the shared `synth::EnvelopeGenerator` engine ADSRModule is built on, independent of the module wrapper: zero-duration-stage cascade (a 0 ms stage costs no samples), exact stage timing across a curve sweep (`CurveSweep/EnvelopeGeneratorStageDurationTest`, parameterised over attack/decay/release x 3 curve amounts), `shape()` endpoint/monotonicity, release from mid-attack/mid-decay starting at the current level, retrigger-from-current-level (never 0, never instant), a mid-ramp time change with no level discontinuity, sustain 0/1 edge cases, and a gapless note sequence at sustain 0 |
| ADSRTest | `Tests/Modules/ADSR/` — split by concern into `ADSRStageTests.cpp` (attack/hold/decay/sustain/release shapes, retriggering, zero-sustain and sustain==1 edge cases, parameter changes during playback), `ADSRGateTests.cpp` (**mono Gate CV**: rising edge starts, falling edge releases, MIDI-only still works, MIDI+Gate OR so note-off does not release while CV is high), `ADSRPolyTests.cpp` (poly mode independent envelopes and gate edge detection), `ADSRThresholdTests.cpp` (**Threshold**: default 0.5, raised threshold rejects a sub-threshold gate, Threshold CV, hysteresis, port layout). A regression group covers the mono held-note bitset (a MIDI note-on re-articulates even when a note-off and note-on land on the same sample, and releasing one of two held notes keeps the envelope up) and the zero-sustain/sustain==1 release amputation (`EnvelopeGenerator`'s progress-based stages have no rate that can compute to 0 and self-amputate) |
| EnvelopeFollowerModuleTest | Peak/RMS detection accuracy (unit sine → 1/√2 in RMS), attack/release time-constant ordering, unipolar clamped output, Attack/Release/Sensitivity CV modulation, CV channels cleared on output, bypass/mute emitting no CV, port roles (Audio in / ModCV out), state round-trip, zero-channel/zero-sample/zero-sample-rate robustness |
| LFOModuleTest | LFO waveform output, rate modulation, sync behavior |
| VCAModuleTest | Gain application, envelope following, silence detection |
| AttenuverterModuleTest | CV signal attenuation, bipolar control, CV modulation |
| SampleHoldModuleTest | Port topology and modulation targets, Sample-mode rising-edge latch and hold across blocks, Track-mode follow/freeze, internal free-running clock and Rate CV, random source range, Level/Offset/Slew shaping and their CV inputs, **Schmitt-trigger threshold** (low-amplitude and negative-threshold gates, hysteresis rejects dither and in-gap dips, full release re-arms, Threshold CV), **trigger meter telemetry** (signed block peak, live on internal clock, armed state, capture count, reset on bypass/mute), `ThresholdControlComponent` static helpers (`valueToX` clamping, unipolar / dB mapping, `needsRepaint` idle-gate, preferred height) plus a paint smoke test, trigger/CV channel hygiene, bypass dry pass-through and mute silence, zero-length/zero-channel/single-sample buffers, zero sample rate, state round-trip |
| ComparatorModuleTest | Port topology (Signal / Threshold in, Gate / Inverse out), gate high above threshold and low below, inverse complements gate, hysteresis, Threshold CV, bypass/mute silence both outputs, meter peak, trigger count, default threshold 0.5 |
| SchmittTriggerTest | Shared rising-arm / falling-rearm helper: starts low, arms above threshold, holds inside the 0.05 gap, falling edge, reset, equal-to-threshold does not arm |
| MacroControlModuleTest | Port model (16 channels, 8 visible by default, no MIDI, ModCV role), per-macro CV output, channels above the `Knobs` count silent, hidden knobs keep their values when the bank is re-grown, bipolar mapping, 20 ms smoothing, bypass/mute silence, zero-channel buffer; factory creation, `macroCount` plus knob JSON round-trip, one macro fanned out to two destinations |
| FX module tests | Delay (passthrough, feedback), Distortion (clipping, drive), Reverb (room size), Chorus, Phaser, Compressor, Flanger, Limiter, Bitcrusher (downsampling, quantization, CV), Ring Modulator (diode-ring, oversampling aliasing) |
| GateModuleTest | `Tests/FX/GateModuleTests.cpp`. Hysteresis Schmitt trigger (opens at Threshold, closes only below Threshold - hysteresis, a level sitting in the gap keeps whichever state the gate was already in), Attack/Hold/Release timing measured in samples against the analytic linear-ramp model, Range floor is the parameterised gain (e.g. -20 dB -> 0.1 amplitude) not silence, both stereo legs gated identically by the linked max(\|L\|,\|R\|) detector, port labels/counts, module type/category. Bypass dry pass-through and mute silence live in `FXBypassTest` alongside every other FX module |
| PitchShifterModuleTest | Pitch mode transposition ratios (spectral peak), Frequency mode SSB offset and sideband suppression, CV routing, feedback stability, state round-trip |
| SamplerModuleTest | Registration plus port/parameter surface; WAV load (success, missing file, unreadable file, failed load keeps the previous sample, clear); Sample mode playback verified sample-exact against a ramp file at unity rate, at `pitch = +12` (2×), via MIDI note transpose, and with `start = 0.5`; monotonic anti-click fade-in; one-shot falls silent at the last frame vs loop keeps going; Granular mode produces bounded finite audio and stays silent with no sample loaded, including at max density × max grain size; gate precedence (free-run with nothing patched, trigger-CV latch silences a low gate, retrigger, a gate rising mid-block is not mistaken for an unpatched jack); FRO246 sample-accurate MIDI retrigger — two Note-Ons with no Note-Off between them across separate blocks and within one block both retrigger at their own pitch, a Note-Off immediately followed by a Note-On in the same block (adjacent samples, and the exact-boundary same-sample case a transport loop restart produces) still cuts and restarts; bypass/mute clear; CV channels do not leak to the output; level CV sums with the parameter; zero-channel buffer is safe; `getExtraState`/`setExtraState` round trip, restored through `graphToJSON` → `applyJSONToGraph` on the trusted path and **dropped** on the untrusted path |
| SampleWaveformPeaks | `SampleWaveformComponent::computePeaks` — empty inputs, columns span the buffer and track min/max extremes, channels averaged (opposite phase cancels), more columns than frames |
| SampleWaveformPaint | Paints the empty state ("No sample loaded") and a loaded sample with a live playhead into a `juce::Image`; repeat `timerCallback()` with nothing changed is a no-op; zero-width component is safe |
| SamplerFormats | `getSupportedFormatWildcard()` is non-empty and includes `*.wav`; `isSupportedAudioFile` accepts wav/WAV/aiff and rejects .json/.txt/extensionless/directories (extension-only check, so drag-hover stays cheap) |
| Parametric EQ tests | `Tests/FX/ParametricEQ/` (`ParametricEQModuleTests.cpp`, CV in `ParametricEQCVTests.cpp`). `ParametricEQModuleTest` — identity, 15-in/2-out channel layout, port labels, mod targets, logical-port roles, slot types and row labels, **all four bands start disabled**, enable round-trip and count, a disabled band with a big gain still contributing nothing, setters clamping to range, bypass dry pass-through / CV clearing / mute. `ParametricEQPointPlacement` — `findBandForNewPoint` picks the nearest free slot on a log axis, skips slots in use, returns -1 when full, and resolves out-of-range frequencies. `ParametricEQResponse` — `bandMagnitudeDb` anchor points (bell hits its gain at centre and 0 dB two decades out; cut is the exact mirror of boost; higher Q narrows the bell; a shelf sits at half its gain at the corner; zero-gain bands are flat everywhere; degenerate inputs return unity, not NaN) and `responseDb` skipping disabled bands plus adding the output trim. `ParametricEQCoefficients` — the RBJ digital biquad agrees with the analog prototype it came from within 0.6 dB, zero gain yields a literal pass-through biquad (`b == a`), and centres past Nyquist / a zero sample rate stay finite. `ParametricEQAudio` — real-audio level measurements (all-off is a straight wire; a configured-but-disabled band does not touch audio; enabling applies and disabling restores unity; ±12 dB bells move their band by ±12 dB; a narrow boost leaves distant tones alone; shelves only touch their own end; output gain scales everything; stereo channels come out identical) plus `MeasuredResponseTracksTheAnalyticCurve`, which locks the drawn curve to the measured DSP within 1 dB. `ParametricEQCV` — freq CV is exponential over the full range, gain CV maps onto ±24 dB and clamps, near-silent CV is gated to exactly the unmodulated value while CV above the gate gets through, the original bell jacks (ch2-5) provably leave the shelves alone, and the normalised jacks on ch6-14 move the shelves, every Q and Output, match no-CV at zero and never leak into the audio outputs. `ParametricEQEdgeCases` — zero-length/zero-channel/mono buffers, processing without `prepareToPlay`, re-preparing at a new sample rate, band response independent of sample rate, state round-trip preserving enable flags, and every band exposing the full parameter set |
| AntiClickTest | ADSR's 1 ms attack/release defaults do not click and an explicit 0 ms is honoured (no clamp), smooth parameter transitions |
| AutomationZipperTest / AutomationZipperCoverage | `Tests/Timeline/AutomationZipperTests.cpp` — the zipper net for timeline automation. `EveryAutomatableFloatParam/AutomationZipperTest` is parameterised over **(factory module × float parameter), generated at runtime from `AIStateMapper::moduleFactoryTypeNames()` and each module's live parameter list**, so a new module or a new float parameter is swept without touching the test. Each case renders 62 blocks at 48 kHz: 50 walking the parameter's full range through the applier's own write path (`param->setValue(param->convertTo0to1(v))`, once per block), then a square wave between the two extremes — four instantaneous full-range jumps, which is the case smoothing exists for. Asserts every output sample is finite (universal, never relaxed) and that the worst inter-sample delta stays under 0.6, with per-module/per-parameter overrides carrying a written justification (Noise and Sample & Hold are staircases by construction; Bitcrusher is a quantiser; the EQ's `bandNGain` cannot absorb an instant ±24 dB coefficient swap). The `AutomationZipperCoverage` cases are the enforcement half, mirroring `WavetableWarpAliasTest`: a factory module with float parameters that is neither configured nor excluded fails the build, a config entry naming a module with no float parameters is rejected as stale, every configured name must still resolve through the factory, and every exclusion / raised bound must carry a reason |
| EdgeCaseTests | Zero-length buffers, extreme parameters, single-sample buffers, rapid parameter changes, large buffers |
| AudioRenderingTests | Snapshot-based tests comparing bit-perfect output against reference files; covers full chains (Osc->Filter->VCA), modulation accuracy, and External MIDI input |

## Transport and offline render

The timeline's clock and the headless render harness built on it. No audio device, no sleeps, fully
deterministic: engines here are always `HostMode::Hosted` (a Standalone engine must never have
`initialise()` called on it in a test — that opens real hardware) and the graph is clocked through
`prepareForHost()` / `processHostBlock()`, which funnel into the same `renderNextBlock()` the
standalone device callback uses.

| Suite | What it covers |
|-------|----------------|
| TransportServiceTest | `Source/Transport/TransportService.*` in isolation — command FIFO ordering and drop-when-full, play/stop/locate/loop/BPM/time-signature, `BlockTimeInfo` per block, loop wrap (including the exact-boundary and tiny-loop cases), beats-are-canonical behaviour across BPM and sample-rate changes, `getPositionSnapshot()` / `juce::AudioPlayHead` reads staying consistent, including under concurrent readers |
| AudioEngineTransportTest | `Tests/Engine/AudioEngineTransportTests.cpp` — the transport's wiring inside `AudioEngine`: exactly one `tick()` per rendered block in both host modes, a stopped transport freezing the position, `prepareForHost` handing the host sample rate to the transport, the playhead installed once on the graph and re-applied by JUCE to every node (including nodes re-created by an undo restore), a node reading block-start position through `getPlayHead()`, latency reported not compensated |
| OfflineTransportDriverTest | `Tests/Engine/OfflineTransportDriverTests.cpp` — `synth::OfflineTransportDriver`: `renderBlocks(n)` returns exactly n×blockSize samples and advances the transport by one block per block; `renderToBeat` renders whole blocks until `endPpq` reaches the target (from beat 0 that is exactly `ceil(sampleFromBeat(beat) / blockSize) × blockSize` samples, overshooting by < 1 block) and returns an empty buffer without spinning when the transport is stopped or the target is behind the playhead; the per-block callback sees consecutive `BlockTimeInfo` (block start samples 0, 512, 1024 …); rendered audio is finite and in range; a free-running oscillator patched to Audio Output is audible **whether or not the transport is playing** — the transport is a conductor, not the engine. `renderBlocks`/`renderToBeat` delegate to the non-accumulating `streamBlocks`/`streamToBeat`, so every assertion here covers both shapes; the streaming pair's own consumer is `BounceExporterTest`; the optional pre-block `BlockGate` ends either stream loop **without rendering the refused block** (the transport's position proves it never ran) and no gate means every requested block |
| BounceExporterTest | `Tests/Engine/BounceExporter/` — the offline bounce. Everything asserts on the **file**, opened with JUCE's own `WavAudioFormat` reader, using the same RMS windows and guard bands `TimelineE2ETest` uses on the in-memory render (the patch is duplicated from it, plus an optional fully-wet Delay for the tail): a `[0,8]`-beat bounce is exactly `ceil(sampleFromBeat(8) / blockSize) × blockSize` = 375 × 512 samples with the requested rate/depth/channels, and `bitDepth 32` really is an IEEE-float WAV; energy sits exactly inside each note window and each gap is silent; a 1 s tail is still ringing where the range ended and decays across it (RMS of the first 100 ms > the last 100 ms) while `tailSeconds = 0` stops dead at the range with the same Delay still ringing; two fixtures bounced once each are **byte-identical**; a bounce taken from a playing transport at beat 3 leaves the playhead at beat 3, **stopped**, the engine back on its previous prepare format and Track In still audible on the next live render; cancelling from the progress callback leaves neither the target nor a stray temp file and still restores the transport; a nonexistent destination directory fails with a message, writes nothing, creates no directories and leaves the engine renderable; invalid options (backwards range, 20-bit depth, zero sample rate) are rejected before anything is written; progress is non-decreasing, ≤ 1 and lands on exactly 1.0 over a range plus tail render. Three more cover the parts a synth-only patch cannot reach: a bounced **Track Audio** clip is bit-exact against its 32-bit float asset from its **first frame** with `streamDropouts == 0` (with the prefetch thread paused, so the bounce's own `waitUntilPrimed` call is the only thing that ever fills a ring), the same holds for a range starting two beats *into* a clip with the **real** prefetch thread running, and cancelling on the first tail block writes exactly range plus one blocks |
| StemExportTest | `Tests/Engine/StemExportTests.cpp` — offline stem export ([`../mixer/stem-export.md`](../mixer/stem-export.md)): N strips produce N files named `"NN - <name>.<ext>"`, all the same length; THE correctness check — `sum(stems)` reproduces the pre-Master mix, proven against a **non-unity** Master gain (+6 dB) against a normal `BounceExporterTest`-style bounce of the same patch, since a post-Master tap would fail by exactly that gain factor, and separately with a non-zero tail so `StemSession::stepTail` is exercised too (every stem grows by the tail's block count and the sum property holds through it); non-default strip gain/pan land in each stem exactly (proving the tap is post-fader); a muted strip's file is silent while the sum property still holds; a soloed strip during export still writes every strip (non-soloed ones silent) and leaves solo/mute state unchanged; a no-strips patch fails with a clear message before touching the destination; cancellation and an unwritable/blocked destination leave no stem files, never touch a pre-existing file, and disarm every `ChannelStripModule` tap; two module-level tests pin the tap's exact post-fader/muted/solo-gated output directly |
| TimelineE2ETest | `Tests/Timeline/TimelineE2ETests.cpp` — the clip → Track In → Poly MIDI → Oscillator/VCA → Audio Output regression net every later scheduling change must keep tripping: rendered RMS is high exactly inside each timeline note's window and silent exactly inside each gap (guard bands ±2400/4800 samples absorb the ADSR-free chain's only shaping, PolyMidi's 5 ms gate smoothing); a stopped transport, a muted track and a bypassed Track In all render full silence; a looped range re-fires the same note every pass; the note's audio frequency (not just its gate) measures 440 Hz end to end |
| AutomationApplierTest | `Tests/Timeline/AutomationApplierTests.cpp` — the engine-level net for a `TimelineDoc` lane reaching a live `juce::RangedAudioParameter`: a ramp tracked block by block against the transport's beat position; a stopped transport leaving the knob untouched; deleting the bound node mid-render staying safe (the binding's refcounted `Node::Ptr`) and dropping out on the next `publishTimeline`; unresolvable lanes (unknown uuid, unknown paramID) producing no binding without poisoning the resolvable ones; a lane authored against a wider range clamping at the parameter's endpoints; 100 publishes interleaved with render passes swapping tables cleanly and reclaiming retirees. Plus three record-mode branches driven against a hand-built binding table in `Tests/Timeline/AutomationRecordTests.cpp` (no engine needed): an `Off` lane never writing, a `Write` lane reading like `Read` until global record is armed and going silent once it is, and a `Touch` lane yielding to a claimed parameter and resuming the block after the claim is released |
| AutomationRecordTest | `Tests/Timeline/AutomationRecordTests.cpp` — the capture half. Mostly headless (bare `TransportService` plus a standalone `FilterModule` for a real parameter): a `Touch` gesture committing as **exactly one** undo step whose undo restores the lane byte for byte; the programmatic-write guard (`setValueNotifyingHost` with no gesture captures nothing, on an armed and rolling `Touch` lane) and `ScopedProgrammaticApply` suppressing even a fully gestured write without latching; RDP thinning collapsing 100 collinear points to 2 and keeping a triangle's corner at its **exact** captured value; `Write` overwriting its span, closing with a terminal anchor and auto-dropping to `Touch` on stop *without* undo re-arming it; `Latch` still capturing after the gesture ends; an empty `Touch` gesture being a total no-op vs. an empty `Write` span writing flat anchors; ring overflow raising the flag and still committing; `recordMode` round-tripping through `toVar`/`fromVar` (absent ⇒ `Read`, out of range ⇒ rejected) and reaching `TimelineSnapshot::LaneInfo`. Two hosted-engine tests: `ApplierRespectsClaims` and `RecorderNeverHearsTheApplier` (200 blocks of playback into an armed lane capture nothing) |
| AutomationSlicingTest | `Tests/Timeline/AutomationSlicingTests.cpp` — the control-rate slicing flag. The flag defaults off; with it on, automation leads the unsliced value by exactly `slope × (blockSize − 64) / samplesPerBeat` at the end of every block (it really does slice); a per-sample chain (Osc → Filter → VCA, DC from a Macro bank into the VCA's CV) renders **bit-identical** audio either way; a Chorus with an LFO on its block-rate Rate CV does **not** — that test asserts only finiteness/audibility and *reports* the measured difference, which is the documented reason the flag ships off; plus a cost tripwire over a 49-node patch that fails only on a >4× blow-up |

## Audio input and device state

`Tests/Engine/AudioInputTests.cpp` — the first tests in the repo that drive `AudioEngine`'s
**device-callback** half. The fake device itself lives in `Tests/FakeAudioIODevice.h` so the
following suite drives the same one; see
[`test-patterns.md`](test-patterns.md#the-fakeaudioiodevice-pattern) for how to extend it.

| Case | What it covers |
|-------|-------|
| `InputReachesTheGraph` | device input ch0 (a ramp) arrives at an `AudioGraphIOProcessor(audioInputNode)` and passes through to Audio Output unchanged, while an unconnected output channel renders **silent** rather than leaking the input channel that aliases it |
| `InputCountZeroBehavesAsToday` | a null input array plus 0 channels renders the same patch **sample-for-sample** as the hosted `processHostBlock` reference, and dereferences nothing |
| `MoreInputsThanOutputs` | 2 in / 1 out: input ch1 survives the scratch round trip (it is the only thing connected to the single output, so the assertion cannot pass without it) |
| `CallbackNeverAllocates…` | the channel-pointer array and scratch are sized in `audioDeviceAboutToStart` — on `max(in, out)`, for a whole device block — via `getDeviceScratchInfo()` |
| `CallbackBeforePrepareStillSilencesTheOutput` | the belt-and-braces path: with no scratch to render through, the callback silences the device's output rather than replaying what was in the block |
| `DeviceStateChangeReachesTheOwnerCallback` | a device-manager change notification reaches `onDeviceStateChanged` exactly once; another broadcaster does not; a Hosted engine never persists |
| `SavedDeviceStateSelectsTheRestorePath` | through the `initialiseDevices` seam: no saved state ⇒ legacy defaults path, saved state ⇒ restore path, Hosted ⇒ no device acquisition at all, and the seam leaves the engine unattached |
| `InputLatencyAccessor` | the fake's 64 samples after `audioDeviceAboutToStart`, back to 0 after `audioDeviceStopped`, 0 in Hosted mode |
| `MainComponentDeviceStateTest` | the owner half: the persist callback writes `"audioDeviceState"` (and a null payload writes nothing), and a stored string is parsed and handed to the engine before `initialise()`. Uses the AppProperties isolation pattern, snapshotting and restoring that one key so a developer's real device choice survives a test run |

## Audio Input module

`Tests/Modules/AudioInputModuleTests.cpp` — the module that replaced the graph's raw
`audioInputNode`. Two layers: **module-level** tests drive an `AudioInputModule` directly with a
bare `synth::TransportService` on its playhead (exactly what the engine does per block, minus the
engine — one of them, `NoTransportRendersSilence`, is the no-playhead caveat described in
[`../architecture/audio-engine.md`](../architecture/audio-engine.md#audioengine)), and
**engine-level** tests drive the whole path through a real `AudioEngine`, exercising the playhead
the engine itself installs.

| Case | What it covers |
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
| `DeviceShrinkDropsHiddenRoutings` | 8-in device ⇒ 8 jacks and a cable on ch3 survives; switch to a 2-in device plus `refreshIoModulesAfterDeviceChange()` ⇒ ch3's routing is dropped, ch0/ch1 keep theirs, and the card gets shorter |

## Rec Tap and audio recording

`Tests/Timeline/RecordTapTests.cpp`. Three layers. **Module-level** tests drive a `RecordTapModule`
directly (no engine, no device, no graph), exactly the way `MidiRecorderTests.cpp` drives
`synth::MidiRecorder`. **Registration** tests are the internal-only checklist Track In established.
**Flow** tests drive `MainComponent` with the device callback suspended, the transport ticked by
hand and the tap's `processBlock` called directly. The last two groups exercise the factory entry
and the record wiring, both unconditional, always-compiled code.

| Case | What it covers |
|-------|-------|
| `PassThroughAlways` | armed or not, the output is the input **bit-exactly** — the tap is transparent |
| `CaptureWritesExactWav` | 8 × 512 frames of an exactly-representable ramp: the WAV parses with JUCE's own reader at 48 kHz / 32-bit / IEEE-float / 2 ch, is exactly 4096 frames, and compares **equal sample for sample**; the `.agpk` sidecar's magic/version/bucketSize/channels parse, the bucket count is `ceil(4096/256)`, and every bucket's per-channel min/max is the ramp's first/last frame (ch1 is ch0 negated, so a channel mix-up cannot pass) |
| `PartialFinalPeakBucketCoversOnlyTheFramesThatExist` | a 300-frame take is 2 buckets, the second covering `[256, 300)` — short, never padded |
| `OverrunSetsFlagAndKeepsAudioThreadClean` | a 64-frame ring against 512-frame blocks (deterministic, not a race): the flag is set, the audio still passes through untouched, the take is SHORT but finalised, and `lengthSamples` equals the WAV's real length |
| `BypassPassesDryAndStopsCapturePushes` | bypass passes the dry signal (this module has a dry path — it does **not** clear) and records nothing; un-bypassing resumes on the very next block |
| `StopWithoutStartIsSafe` / `DoubleStartRejected` | stop with no take, a double stop, and processing while disarmed are all inert; a second `startCapture` is refused and leaves the other file untouched; a bad rate/channel count arms nothing |
| `DestructorFinalisesAnInFlightTake` | destroying the module mid-take closes the WAV and writes the sidecar rather than abandoning a half-written file |
| `RegisteredButInternalOnly` / `AbsentFromTheLibraryWithAPinnedSizeEstimate` | factory key, module type, channel counts, `getFactoryTypeName`, non-authorable, Utility cable colour; absent from the library, with `estimateModuleSize("Rec Tap")` measured against the real card |
| `RecordFlowCreatesAudioClip` | bundle-saved project plus armed Audio track: Record-on splices exactly one tap between Reverb and Audio Output (both channels re-routed, the direct connections gone) as **one** undo step; capture starts on the poll, not the click; rolling 8 blocks and Record-off produces **one** clip with `assetRef == "Audio/take-1.wav"`, `startBeat` at the punch and a length matching `lengthSamples × bpm / (60 × rate)`; the WAV and `.agpk` exist inside the bundle; a second take re-uses the tap and gets `take-2`; undo removes the clip and **leaves the file** |
| `MidiArmedPathUnchanged` | an armed MIDI track records exactly as the recorder left it — no tap is created at the click or by the poll, and the committed clip has notes and an empty `assetRef` |
| `AudioRecordWithoutAnAudioOutputIsRefused` | no master bus ⇒ refused with a status message, no tap, no clip, and the transport is **not** started |

## Mixer channels

`Tests/Mixer/ChannelStripTests.cpp` and `Tests/Mixer/MixerSoloTests.cpp` — the `Channel Strip` /
`Master` nodes and the solo gate ([`../mixer/mixer.md#solo-is-a-render-time-gate`](../mixer/mixer.md#solo-is-a-render-time-gate)). Headless. **Module-level** tests
(`ChannelStripTest`, `MasterModuleTest`) drive the processors directly, with a bare
`synth::TransportService` on the playhead when the gate matters. **Engine-level** `MixerSoloTest`
cases build a hosted `AudioEngine` rig (two strips into Master's Mix, one constant source into
Direct) and render through it, so the per-block gate is exercised exactly as the engine publishes
it. The `MasterSplice*` cases give their bare graph `setPlayConfigDetails(2, 2, ...)` first —
without it Audio Output has zero channels and every connection into it is silently refused. Channel
macro bypass is covered by
`MacroBypassMute.ChannelMacroBypassSkipsSourceAndStripButMuteIncludesTheStrip`.
`ChannelStripTest`/`MasterModuleTest` also cover `takeMeterPeak`/`PeakMeterLatch` (per-reader
consuming reads, the missed-overs regression); the meter UI/theme/ballistics suites that grew from
it are in [`../mixer/meters.md#test-coverage`](../mixer/meters.md#test-coverage).

| Case | What it covers |
|-------|-------|
| `ChannelStripTest.*Pan*` / `Gain*` | balance-law pan (unity centre, mono feeds both legs, only the far leg attenuates); gain in dB with the -60 dB floor as true silence |
| `ReservedChannelsAreClearedEveryBlock` | ch1..3 between the legs never leak |
| `Bypass*` / `MuteClears` | bypass is dry (a bypassed mono strip still feeds both legs), mute clears — two branches |
| `MeterReportsTheLastBlocksPostFaderPeakWithoutConsumingIt` | the meter is a plain per-block store, readable twice |
| `ShapeIsFixedOnceWritten` / `ShapeLocksOnceTheStripIsLive` / `ExtraStateRoundTripsShapeAndSolo` / `JackMap*` | mono/stereo is fixed for the strip's lifetime; shape plus solo round-trip through trusted extra state; the right leg sits on `kRightBase` |
| `MasterModuleTest.*` | Direct summed into Mix before the fader; bypass is a unity sum; four labelled inputs and no Dual I/O toggle |
| `MixerSoloTest` gate cases | a non-soloed strip (bypassed too) and Master's Direct go silent while any strip is soloed; no transport means no gating; solo never writes a mute parameter |
| `DeletingASoloedStripReleasesTheGateAtPublishTimeline` / `UndoRedoAcrossAGraphRebuildSettlesTheGate` | the count is recounted from the graph, so the mix is never stuck silent |
| `MasterSplice*` / `MasterIsASingleton` / `MasterGoesInFrontOfAnExistingRecTap` / `MasterSpliceNeedsAnOutput` | one undo step; strips re-route to Mix, everything else to Direct; at most one Master; spliced ahead of the Rec Tap; refused with no output |

## Channel creation flows

`Tests/Mixer/ChannelFlow/` — `Source/Mixer/ChannelFlows/ChannelFlows.h`/`.cpp`'s builders and the
three "+ Track"/drag flows that call them ([`../mixer/mixer.md#channels-follow-audio-not-tracks`](../mixer/mixer.md#channels-follow-audio-not-tracks)). Headless except the
`ChannelFlowTest` fixture, which drives a real `MainComponent` off-screen.
`ChannelFlowAutoChannelCore` cases build a bare `AudioEngine`/graph directly
(`graph.setPlayConfigDetails(0, 2, 44100.0, 512)` before adding an "Audio Output" node, mirroring
`MixerSoloTests.cpp`'s rig, since `AudioEngine`'s default constructor has no IO yet);
`ChannelFlowTest` cases go through `MainComponent::newPatchForTest()` for a real,
empty-but-seeded graph and drive `ModuleComponent::mouseDown/mouseDrag/mouseUp` with synthesized
`juce::MouseEvent`s for the drag gestures (see
[`test-patterns.md`](test-patterns.md#test-the-real-mouse-path)) rather than `GraphEditor`'s own
drag API. The "Make channel" cases ([`../mixer/mixer.md#make-channel-and-shared-modules`](../mixer/mixer.md#make-channel-and-shared-modules)) add a render-identity rig —
`HostedPatchCFT`, a `HostMode::Hosted` engine rendered offline through `processHostBlock`, built
twice from the same legacy patch so a standalone `GraphEditor` can convert one and the two renders
be compared sample for sample (`ChannelFlowMakeChannelCore`) — and drive the track header, canvas
and module-card right-click menus through their
`setShowContextMenuHookForTest`/`setShowCanvasContextMenuHookForTest` seams (`ChannelFlowTest`).

| Case | What it covers |
|-------|-------|
| `buildDefaultAudioChannel`/`buildChannelForFeeds` | EQ(bypassed) -> Compressor(bypassed) -> Strip(Stereo) -> Master chain wiring; a poly instrument gets a Voice Mixer ahead of the strip (`PolyInstrumentGetsVoiceMixerAheadOfStripAndFeedsTheChannel`); latency stays zero after `prepareToPlay` (`BypassedEQAndCompressorReportZeroLatencyAfterPrepare` — a gate, not a tautology: neither module calls `setLatencySamples`) |
| `findUnchanneledOutputFeeds` | a straight instrument-to-output path is 2 exits (`OnInstrumentToOutputFindsTwoExits`); an already-`ChannelStrip`-ed path is 0 (`OnStripChanneledInstrumentFindsZero`); a dead-end path reaching nothing is 0 (`NotReachingOutputFindsZero`); a hidden `AttenuverterModule` mod/CV leg is neither traversed nor counted, and does not disturb an unrelated instrument's own path (`IgnoresModulationBranchAndLeavesUnrelatedPathUntouched`) |
| `buildChannelForFeeds` exit handling | exit edges are removed before the chain is built, and wired through to a freshly spliced Master (`RemovesExitEdgesAndWiresThroughToANewMaster`) or an existing one, clearing prior Direct feeds (`ReusesAnExistingMasterAndClearsDirectFeeds`) |
| `ChannelFlowTest.AutoChannelOnConnect_*` (real mouse drag) | toggle ON builds exactly one channel as one undo step (`ToggleOnBuildsOneChannelAsOneUndoStep`); toggle OFF only makes the connection, byte-identical to the pre-feature behaviour (`ToggleOffOnlyConnectsNoChannel`); an instrument that already has a channel gets nothing new when a second Track In connects (`AlreadyChanneledInstrumentGetsNoNewStrip`); the new EQ/Compressor/Strip join the instrument's existing macro when boxing applies (`NewChainNodesJoinTheInstrumentsExistingMacro`) |

The suite is split by topic, all sharing the `ChannelFlowTest` fixture, `MockProviderCFT`, the plugin-scan stub backend and the render-identity rig helpers in
`Tests/Mixer/ChannelFlow/ChannelFlowTestFixture.h` (header-only, not built on its own):

| File | Covers |
|------|--------|
| `ChannelFlowTests.cpp` | "+ Track -> Audio Track" builds a whole mixer channel in one undo step |
| `ChannelFlowInstrumentTests.cpp` | "+ Track -> Instrument -> {Oscillator/Wavetable/Sampler}", the MIDI-track mirror of the audio-track flow |
| `ChannelFlowPluginInstrumentTests.cpp` | A hosted plugin as the instrument, plus menu snapshot resolution, format-label disambiguation and self-exclusion |
| `ChannelFlowAutoChannelTests.cpp` | Auto-create channel on MIDI connect — core-level `findUnchanneledOutputFeeds`/`buildChannelForFeeds`, then real-mouse-gesture coverage through `GraphEditor::endConnectionDrag` |
| `ChannelFlowCreateChannelsTests.cpp` | "Create Channels" for existing projects, wrapping every channel-less track's chain as one undo step |
| `ChannelFlowMakeChannelCoreTests.cpp` | "Make channel" core behaviour through a standalone `GraphEditor` (render-identity comparisons), plus the adversarial no-double-drive / three-way-merge / chained-merge probes |
| `ChannelFlowMakeChannelAppTests.cpp` | The real app wiring — track header, canvas selection and module right-click menus, each as one undo step |

## Mixer panel

`Tests/Mixer/MixerModel/` — headless column-ordering logic ([`../mixer/panel.md#what-the-mixer-shows`](../mixer/panel.md#what-the-mixer-shows)),
pure `Source/Mixer/MixerModel/` code. `Tests/UI/Mixer/` — the panel UI, on a real off-screen
`MainComponent` (`newPatchForTest()` plus `simulateAddAudioTrackClick()`, the `ChannelFlowTest`
rig's style):

| File | Covers |
|------|--------|
| `BottomDockComponentTests.cpp` | tab strip switches without closing the dock; `toggleMixerPanel` (Cmd+Alt+M) opens on Mixer then closes on a second press; actionId round-trips to `AppCommands::toggleMixerPanel`; active tab persists across an `ApplicationProperties` reload |
| `BottomDockResizeTests.cpp` | FRO231: the dock-wide resize handle on each of the Timeline / Mixer / MIDI Remote tabs (parameterized) — bounds and cursor, hit-test priority over the tab buttons, hover, total-height drag, live vs commit, stray click; plus `MainComponent`'s ownership of the height (default, persisted, clamp, smaller-window reclamp, hide/show, detached Timeline) |
| `MixerPanelComponentTests.cpp` | one column per strip plus Direct plus Master; themed PNG render smoke test (Obsidian plus Daylight, see the [`createComponentSnapshot` pattern](test-patterns.md#component-snapshot-smoke-tests)); clicking a column selects its owning macro (the clip-readout PNG inspection test is in [`../mixer/meters.md#test-coverage`](../mixer/meters.md#test-coverage)) |
| `MixerFaderTests.cpp` | the fader's `SliderParameterAttachment` binding and dB readout, plus a regression test for a `MixerFader::parameterValueChanged` use-after-free (a `callAsync` lambda captured raw `this`; fixed with `SafePointer`) |

Tests calling `dock.setActiveTab(...)` use `BottomDockActiveTabResetGuardMDT` (see the
[reset-guard pattern](test-patterns.md#shared-settings-file-reset-guard)). Every
`Tests/UI/Timeline/TimelinePanel/*Tests.cpp` case, plus `PanelAnimationAndLoadingTests.cpp` and
`TimelinePlayheadTests.cpp`, asserts the timeline panel's bounds and visibility through
`TimelinePanelTestFixture.h`'s `timelinePanelBoundsInMainComponent(mc)` (`getLocalArea`, bounds are
dock-relative) and `timelinePanelIsOpen(mc)` (`isVisible() && bottomDock.isVisible()`; `isShowing()`
needs a real Desktop peer, unavailable headless), because `TimelinePanelComponent` nests inside
`BottomDockComponent` rather than being `MainComponent`'s direct child.

## MIDI Remote panel

`Tests/UI/MidiRemote/` (FRO131, [`../control/midi-remote-ui.md#the-midi-remote-panel`](../control/midi-remote-ui.md#the-midi-remote-panel))
— the dock's third tab, same real off-screen `MainComponent` style as the Mixer panel tests above.
The engine/model/learn-controller test suite this panel sits on top of lives in `Tests/MidiRemote/`
(FRO124/FRO130/FRO133/FRO253) and is not covered here.

| File | Covers |
|------|--------|
| `MidiRemotePanelTests.cpp` | pre-`configure()` null-safety; post-`configure()` via a real `MainComponent`; dock tab-switch shows the panel and hides the others |
| `ControllersListTests.cpp` | present/absent/orphan row states; right-click Rename/Export/Delete via `synth::ui::test_hooks`' free-function seams (the header is locked for this ticket — see the file's own comment) |
| `ControllerSurfaceTests.cpp` | grid layout from `col`/`row`; activity decode onto the display-only widgets; drag-to-move reporting; a themed PNG render smoke test gated on `MIDI_SURFACE_PNG` |
| `ControlInspectorTests.cpp` | Project vs Global assignment rows; takeover/range editing gated to a parameter target (`isTakeoverEditable()`) |

Tests using `BottomDockComponent::Tab::MidiRemote` share the same
`BottomDockActiveTabResetGuardMDT` reset guard as the Mixer panel tests above (`bottomDockActiveTab`
is one shared on-disk settings key across all three tabs).

## Audio clip playback

`Tests/Timeline/AudioClipPlaybackTests.cpp`. Five layers. Playback tests render through
`synth::OfflineTransportDriver` exactly the way `TimelineE2ETests.cpp` does, but assert **bit-exact
sample content** rather than RMS windows, which two things make possible: the test WAV is 32-bit
IEEE float carrying exactly-representable values (`n / 65536`), and the streamer's prefetch thread
is **paused** (`setPrefetchPausedForTest`) and driven by `pumpForTest()` from the render loop's
per-block callback. There is no sleep and no "eventually the ring fills" wait anywhere in the file.

| Case | What it covers |
|-------|-------|
| `AudioClipSnapshotTest.*` | an Audio track flattens an `audioClips` run and **no** notes (and a MIDI track the reverse); runs sorted by `startBeat`; `gainDb` converted to linear once; a 400-char ref truncated but still NUL-terminated; a soloed **audio** track sets `anySoloed` |
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

## Hosted plugins

`Tests/Plugin/HostedPluginTests.cpp`. Not gated: hosting is independent of the timeline. **No
third-party binary is involved.** `Tests/StubPluginInstance.h` supplies a
`juce::AudioPluginInstance` subclass (constructor-set channel counts, a ×0.5 gain marker, a
round-trippable state blob, a settable latency, and a record of the thread its destructor ran on)
plus a `StubBackend` that resolves any identity to it. The stub matches the real formats' threading
— `MessageManager::callAsync`, never re-entrant — so every test pumps the message loop with the
bounded-poll idiom from `AccountServiceTests`.

| Case | What it covers |
|-------|-------|
| `PassThroughUntilReady` / `BareModuleShowsOneJackASide` | with no instance the output **is** the input, on all 16 channels; bypass takes the same dry branch (it must not clear) and mute does clear; a bare module shows one jack a side over 16 real channels |
| `AsyncLoadPublishesAndProcesses` | the callback does not fire re-entrantly; after pumping, the ×0.5 marker is on channels 0–1, the visible ports are the instance's **real** 2/2 while `getTotalNumOutputChannels()` stays 16, the instance was prepared to the host's rate/block, and every hidden channel is silent |
| `InstrumentWithNoInputsGetsItsOutputChannelsCleared` | a 0-in/2-out instrument reports 0 input jacks, and its output-only channels arrive cleared so upstream audio cannot leak through as if bypassed |
| `OverMaxRefusedWithMessage` | a 32-channel instance is **refused, not truncated**: no instance, a status message naming both 32 and 16, and the dry path still intact |
| `UnresolvedIdentityStaysAPlaceholderThatRemembersItsPlugin` | "not installed on this machine": the identity survives, the message names the plugin, and `getExtraState` still serializes it so re-saving does not destroy it (the placeholder's foundation) |
| `StateRoundTrip` | load → set a blob → `graphToJSON` → **trusted** `applyJSONToGraph` into a fresh graph with a `ScopedDefault` stub backend → the instance is restored with the same blob. Also asserts the serialized patch contains **no path** — the stub deliberately reports a `fileOrIdentifier`, so this tests something |
| `UntrustedCannotAuthorIt` / `UntrustedApplyNeverReachesSetExtraStateOnAnExistingNode` | the pin: `validatePatch(trusted=false)` rejects the type with `InternalModuleNotAllowed`, with or without a state blob attached, and apply refuses the whole patch; a merge patch aimed at a **live** hosted node cannot repoint it either |
| `AudioThreadNeverFrees` | two instance swaps and an unload while a second thread renders continuously: retired instances are reaped, every destructor ran on the message thread, and none ran on the render thread |
| `PrepareToPlayPropagates` / `LatencyIsPublishedToTheGraph` | a rate/block change re-prepares the **live** instance in place without retiring it, and a plugin loaded afterwards gets the new format; the instance's latency becomes the module's, and unload returns it to 0 |
| `RegisteredButInternalOnly` / `AbsentFromTheLibraryWithAPinnedSizeEstimate` | the internal-only checklist: factory key, module type, **16/16** channels, `getFactoryTypeName`, non-authorable, Utility cable colour, void `getExtraState` when bare; absent from the library, with `estimateModuleSize("Hosted Plugin")` measured against the real card |
| `IdentitySerializationCarriesNoPath` | `PluginIdentity` round-trips through `var`, carries no path, and matches uid-first — a rename still matches, a different format does not |

Three sibling files share `StubPluginInstance.h` and the same message-pump idiom:
`HostedPluginEditorWindowTests.cpp` (editor windows, headless, state not pixels),
`HostedPluginLaneTests.cpp` (a hosted parameter as an automation lane), and
`HostedPluginLatencyTests.cpp`. The last one's acceptance test splits an impulse down a **dry** path
and a **hosted-plugin** path into Audio Output on a real `AudioEngine` graph, and asserts both
copies land on the same output sample — off-by-zero, at two different latencies, across a
`rebuild()`. That only tests anything because the stub genuinely delays its audio by the latency it
reports; `WithoutARebuildTheParallelPathsDrift` is the negative control that fails if the rebuild is
ever removed.

## Integration

Module interactions within the audio graph, and cross-system integrations.

| Suite | What it covers |
|-------|----------------|
| IntegrationTest | Signal chain routing (Osc->Filter->VCA), LFO->Filter modulation, preset loading with graph structure validation |
| ModMatrixTest | Add/remove mod routings, amount scaling, channel mapping, modulation chains; `RowHeightIs48` (`kRowHeight == 48`); `ZebraAlternates` (`isZebraRow` false for even, true for odd indices); `HoverStateUpdates` (`setHoveredRow`/`getHoveredRow` round-trip, default -1, redundant-set no-op, reset to -1); `ModMatrixComponentPaintSmokeTest` (paint with two routings, no crash); `HoverResetsAfterRowRemoval` (hover clears to -1 when a routing is removed, the componentsChanged path) |
| OllamaProviderTest | AI LLM HTTP requests, streaming responses, model management, non-blocking discovery; `SendPromptWithNoModelFailsWithoutHittingNetwork` — fail-fast with no network call when `currentModel` is empty; `SendPromptIncludesSelectedModelInRequestBody` — captures the POST body and asserts `"model"` matches `setModel()` (the regression lock for empty-model 400s); `QueuedRequestDuringThreadShutdownStillCompletes` — a request enqueued as the worker winds down still gets a callback (request-loss race); `PendingRequestsAreFailedOnDestruction` — requests still queued at destruction are failed *before* `~OllamaProvider()` returns. Both use bounded `condition_variable` waits and fail on timeout rather than sleeping. **Never call `stopThread(0)` in a test** — it force-kills via `pthread_cancel`, which aborts under glibc |
| AIIntegrationServiceTest | Module suggestions, parameter recommendations, graph state mapping |

## Component workflows

UI component interactions using in-process construction (no window, no display).

| Suite | What it covers |
|-------|----------------|
| MainComponentTest | AI panel visibility toggle, mod matrix toggle, minimap toggle plus button tooltip, default configuration, command manager registration (`CommandManagerHasCommands` is pinned to `ShortcutManager::getActionIds()`, so a bindable action with no command cannot ship), redo shortcut, `CopyPasteDuplicateCommandsReachTheCanvas` plus `PasteCommandIsInertUntilSomethingHasBeenCopied` (Paste carries `isDisabled` until the clipboard is filled, and `tryToInvoke` refuses an inactive command — that flag, not `perform()`, is what stops a stray Cmd+V); toolbar narrow/wide mode at 480/1600 px, library sidebar toggle plus persistence, AI panel persistence, status bar bounds, canvas non-zero at minimum size, timer gating (5 Hz), patch name default plus update on preset load, DrawableButton header buttons; `AiProviderGetsModelSelectedOnStartup` — the regression lock that a model is selected on startup via `aiChatComponent.refreshModels()` called AFTER `setProvider()` |
| AIChatComponentTest | Initialization/resizing, send-message updates UI plus history via mock provider; `RefreshModelsSelectsModelWhenProviderInstalledAfterConstruction` — reproduces MainComponent's member-init ordering (chat component constructed before a provider is installed), asserting `getCurrentModel()` stays empty until a post-construction `setProvider()` plus `refreshModels()` |
| SelectionModelTests | Multi-select primitives (`Source/UI/Graph/SelectionModel.h` — pure, no GUI). `SelectionModel` add/remove/toggle/setSelection/clear; `NodeID{0}` rejected as the graph's invalid sentinel; `getSelected()` ordered by uid regardless of insertion order (snippet node order must not depend on click order); `retainOnly` staleness pruning. `marqueeRectFrom` normalises a drag in all four directions. `hitTestMarquee` uses intersection not containment (a clipped edge selects), degenerate band selects nothing, invalid box ids skipped. `unionSelection` for the additive marquee |
| MultiSelectTests | GraphEditor-level multi-select. Selection API (replace vs additive toggle, select-all, clear, prune after a node is removed behind the editor's back). Marquee: touches-to-select, replace vs add semantics, shrinking the band deselects, band over empty canvas deselects, update-without-begin is a no-op. Group drag: followers move by the initiator's delta, `finalizeSelectionDrag` preserves relative layout and snaps the *group* box to the grid, single selection does not engage group drag, `cancelSelectionDrag` leaves off-grid positions untouched, follower positions reach node properties. `deleteSelection` is ONE undo step for the whole group. Snippet drop through `itemDropped` with a `snippet:` payload; plain module drops still work; canvas Delete/Backspace/Escape keys return `false` when nothing is selected so they fall through |
| ClipboardTests | Copy / paste / duplicate for a multi-module selection (`Source/UI/Graph/ModuleClipboard.h` plus the `GraphEditor` actions). **Clipboard state (pure):** empty by default, a payload with zero `nodes` still counts as empty (so Paste cannot be offered for a selection of nothing but graph I/O), `kOffsetStep` is a multiple of the layout grid, each `nextPastePosition()` steps one offset further, a new copy restarts the cascade and `anchorAt` re-aims it. **Copy:** refused with nothing selected; an ineligible-only selection (Attenuverters) is refused *without* wiping what was already on the clipboard; the payload is a snapshot, so it still pastes after the originals are deleted; parameter values survive (a 0.5 Hz LFO rate) and so does non-parameter state (`…SoADuplicatedSamplerKeepsItsSample`). **Paste — wiring, the point of the feature:** `RewiresInternalConnectionsBetweenTheCopiesNotBackToTheOriginals` (copy↔copy wired, original↔original untouched, no wire crossing between them), `DropsConnectionsThatLeftTheCopiedSelection` (a half-selected wire is not recreated — the copy arrives with no wires at all), `DoesNotSpliceTheCopiesIntoTheSurroundingPatch` (merge-mode auto-connect stays off, so a pasted module never wires itself to Audio Output), `RebuildsModulationChainsBetweenTheCopies` (attenuverter count 1→2; the attenuverter never becomes a clipboard node). **Paste — placement:** no-op on an empty clipboard, copies left selected and originals not, relative layout preserved, lands one offset from the original rather than on top of it, repeated pastes cascade by exactly one step each, `pasteClipboardAt` snaps an off-grid point and re-anchors the cascade, ONE undo step for the whole group. **Duplicate:** no-op with nothing selected, offset copy with its own internal wiring, clipboard left untouched, works with no prior copy and does not implicitly fill the clipboard, repeats from the *new* selection so a chain walks across the canvas, ONE undo step |
| ModuleLibraryCollapseTests | Collapsible sidebar sections plus the Snippets section. `DraggableModuleNamesExcludeSnippetsAndThePlaceholder` — `getDraggableModuleNames()` feeds callers that instantiate each name via the module factory, so it filters on `RowKind::Module`; snippet rows and the "No snippets yet" placeholder are visible but are not module types. `HitTestingAgreesWithTheRowLayout` — every row's centre maps back to its own entry (paint and hit-testing share `buildRows()`); rows never overlap; collapse hides a section's rows but keeps its header and shrinks `getTotalContentHeight()`; header click and top-strip click toggle; `toggleAllSections` folds from a partial state rather than unfolding; `onCollapseStateChanged` fires only for user-driven changes, never for the `setCollapsedSections` restore path; blank persisted state expands everything; Snippets section shows an empty hint with no snippets, becomes draggable rows when populated, and sits ahead of the module catalogue |
| ModuleLibraryCollapseAnimationTests | The 150 ms accordion fold. The tween is VBlank-driven and cannot tick headlessly, so these drive `setSectionProgress()` — the same value the animator writes each frame — and separately assert the snap paths: `CollapsingWhileHiddenSnapsInsteadOfHanging` and `RestoringPersistedStateSnaps` (a component that is not `isShowing()` must land on its final layout, or an off-screen sidebar would freeze mid-fold and every other headless test would see a half-open list). Endpoint parity — progress 1 reproduces the collapsed layout exactly, progress 0 truncates nothing. Band geometry — a half fold shows fewer rows, pulls the next header up by about half the section height, and shrinks `getTotalContentHeight()` monotonically across 0/0.25/0.5/0.75/1. `TheRowStraddlingTheBandEdgeIsTruncatedNotSquashed` — at most one row is clipped and none is emitted at zero height; `RowsFoldedPastTheBandStopHitTesting` — a folded-away row stops answering at its old position |
| ModuleLibraryScrollTests | Vertical scrolling for the library sidebar. The bar appears only once rows overflow the panel and hides again when they fit (including after `setAllSectionsCollapsed`, which also resets the offset to 0); `MaxScrollOffsetIsExactlyTheOverflow` — scrolled to the bottom the last row is fully reachable, no more and no less; the offset clamps on over/under-scroll and re-clamps when the panel grows; the bar sits below the pinned chrome (search field plus COLLAPSE ALL) at the right edge. `MouseWheelScrollsTheRows` drives the real wheel path (component → `ScrollBar` → async listener → offset, so the helper pumps the message queue) and `MouseWheelDoesNothingWhenEverythingFits` guards the no-overflow case. `HitTestingFollowsTheScrollOffset` and `TheSameScreenYHitsDifferentRowsAtDifferentOffsets` guard the two coordinate spaces — `getEntryIndexAtComponentY()` applies the offset, `getEntryIndexAt()` stays content-space so existing `getRowCentreY()` callers are unaffected; `PinnedTopStripIsNeverARowHitWhileScrolled` — rows that scrolled under the search field or COLLAPSE ALL chrome do not steal those clicks |
| ModuleLibrarySearchTests | Library sidebar search field. `highlightSpansFor` reports each case-insensitive hit (and stays empty for a blank/whitespace query); a trimmed query hides non-matching modules and empty sections, keeps a header-match section's full child list (`"Time"` → Delay plus Reverb), and draws matching sections open without rewriting `collapsedSections` or firing `onCollapseStateChanged` (clearing the field restores the fold); snippet names are searchable; the empty-snippets hint hides unless the Snippets header itself matched; no match yields zero rows; `getDraggableModuleNames()` is unaffected; filtering a short list hides the scrollbar and resets the scroll offset; paint is safe with matches and with none; the editor is pinned above COLLAPSE ALL |
| GraphEditorTest | Module drag-and-drop, port connection via beginConnectionDrag/endConnectionDrag, deletion, mod matrix visibility, **drag-to-knob modulation** — `DroppingACableOnAKnobCreatesAModRouting` drives the whole path (LFO output → Position knob → a routing on that parameter's CV channel) and asserts the drop-target highlight arms on hover and clears on release, `KnobDropIsIgnoredForACableDraggedFromAnInput` (a mod source drives a destination; the reverse would wire it backwards), snap-on-drop (position is grid-multiple of 8), overlap resolution (second drop at same coord produces non-overlapping bounding boxes); `AudioFileDroppedOnCanvasCreatesAPreloadedSampler` — a real wav dropped on empty canvas yields a Sampler already holding it; `NonAudioFileDragIsRejectedByTheCanvas`; **Macro bank resize** — growing 4→16 knobs keeps the bank's top-left fixed, pushes the module below past `kCollisionGap` and persists its new y to node properties; shrinking 16→4 drops the routing on a hidden jack (and its attenuverter) while leaving a still-visible jack's routing intact |
| ModuleComponentTest | Initialization, resizing, parameter attachment to UI sliders; bypass/mute/delete are DrawableButtons with correct header bounds; delete button triggers requestDeleteModule; `SamplerHasLoadButtonWaveformAndKnownHeight` — the Sampler gets a `SampleWaveformComponent`, a "Load Sample..." button and a "(no sample)" label, at 280×657; `BodyContentClearsEveryPortLabel` — every visible body child starts below the lowest jack (the regression guard for the overlap the old duplicated layout formula caused); `EstimatedModuleSizesMatchTheRealComponents` — builds every library-offered type and fails if `GraphEditor::estimateModuleSize` drifts from the real card, so the drag ghost cannot lie; `KnobsAreLaidOutThreePerRow`; `NonSamplerModulesGetNoSamplerChrome`; audio-file drop — `SamplerAcceptsAudioFileDropAndLoadsIt` (highlight on enter, cleared on drop, sample actually loaded), `SamplerIgnoresNonAudioFileDrag`, `NonSamplerModuleRefusesFileDragSoItFallsThroughToTheCanvas`; `WavetableCardBuildsDisplayAndLoadButton` — a Wavetable card owns a `WavetableDisplayComponent` and a "Load Wavetable..." button, both laid out inside the card, with 8 combos / 15 sliders / 2 toggles at `kDoubleWidth`; `WavetableCardKeepsEveryInputJackOnTheLeft` — all 16 jacks land on distinct in-bounds points across exactly two columns, **both on the left half** (inputs-left / outputs-right is what makes signal flow read left to right), outputs keep the right edge, the split is column-major, and no knob overlaps the lowest jack (which is not necessarily the last one); `ModulationRingsSkipKnobsOnInactiveTabPages` — `getModRingSliderIndex` returns -1 for a knob whose page is hidden and a valid index for a pinned one, so a ring cannot paint over empty card after a page switch; `KnobsResolveToTheirCVJackAsModulationDropTargets` — a visible knob reports the input Port of its own CV channel, empty card reports nothing, and a knob on a hidden page must not swallow a drop; `ModulationDropTargetHighlightTracksTheHoveredKnob` — the highlight sets/clears, reports whether it actually changed (so the caller can skip a repaint), and paints without a themed LookAndFeel; `WavetableTabsSwitchContentWithoutResizingTheCard` — the card height is identical on every page (a resizing card would shove its canvas neighbours on each click), Position and Warp Amt stay pinned across all pages, each page shows a different set, everything visible stays inside the card, and **every knob is reachable from some page** — a control on no page is unusable; `WavetableCardBuildsFolderBrowserChrome` — the `Folder...` / `<` / `>` buttons exist, are laid out inside the card, and clicking next with no folder selected is a no-op rather than a crash; `WavetableCardAcceptsAudioFileDrag` — a Wavetable card claims audio-file drops itself (it once returned false and the drop fell through to GraphEditor, which spawned an unrelated Sampler beside it); `WavetableCardPaintsAndTicksWithoutCrashing`; **Macro bank layout** — height equals `macroBankHeight(count)` for 1/4/8/16 knobs, each output jack sits on its own knob row and hit-tests to that macro's index, no input or MIDI jacks, knobs clear the output-label gutter and stay inside the module, rows above the count are hidden |
| MidiKeyboardModuleTest | Note on/off, key press handling, velocity |
| VisualBufferTest | Scope visualization buffer management, read/write, ringbuffer behavior |
| ModuleBaseTest | Parameter getters, port labels, bypass functionality |
| ModuleBypassTest | Default state, toggle, signal passing when bypassed |
| FXBypassTest | Per-FX bypass dry pass-through, CV-channel clearing, mute silencing |
| VisualSignalFlowTests | AttenuverterModule peak/mod value tracking, VisualBuffer RMS computation, `AudioEngine::getModulationDisplayInfo()` population |
| SettingsWindowTest | Tab structure (Audio / AI / Keyboard Shortcuts / Preferences / Appearance), tab persistence, audio device selector, AI settings persistence, resize safety, shortcuts reference, Preferences tab hosts behaviour controls |
| PreferencesSettingsTabTests | Preferences tab defaults (`NewAndUnwired`, double-click disconnect on) and persistence/push-to-`GraphEditor` for every toggle, including the `mixerAutoCreateChannelOnConnect` toggle (default ON, persists `"0"`/`"1"`, pushes to a live `GraphEditor`); the Dual I/O per-module popup (`dualIOCapableModuleTypes()`-derived rows, override-vs-global consumption in `GraphEditor::applyDefaultDualIOForNewModule`); the live search filter (label/tooltip matching, grouped-row visibility, divider collapse, Esc-to-clear via `onEscapeKey`); the "Scroll up to zoom in" checkbox's boolean-key persistence and its OS-derived `platformCommandKeyName()` modifier text; muted-hint-label layout (two-line-tall box, no horizontal squeeze); the tab's own `wantsKeyboardFocus` so its search field cannot steal initial focus; paint/resize smoke |
| ShortcutManagerTest | Default bindings, reverse lookup, conflict detection, persistence round-trip, reset to defaults, display strings. `CopyPasteDuplicateUseThePlatformStandardKeys` pins Cmd+C/V/D. Two table-wide invariants that stop a new action from shipping broken: `EveryDefaultBindingIsUnique` (a duplicate binding silently shadows one of the two in `getActionForKeyPress`) and `EveryActionIdHasABindingACommandAndADescription` (an action with no `AppCommands` mapping binds a key that does nothing) |

**Poly connection creation coverage.** `GraphEditorTest` covers poly fan-out on drag (dragging a
cable between two poly jacks creates all N per-voice connections) and poly-toggle rewire (toggling a
module's `poly` parameter re-anchors its existing cables via `rewireForPolyChange`).
`Tests/Engine/LogicalPortTests.cpp` adds pure, headless coverage of jack-target resolution —
`ModuleBase::getJackTargets` and `GraphEditor::resolvePolyLink`'s pairing/scoring rules —
independent of the audio graph.

`GraphEditorTest`/`SmartConnection*Test` are split by topic under `Tests/UI/Graph/GraphEditor/`, all
sharing the `GraphEditorTest` fixture and drag/connection helpers in
`Tests/UI/Graph/GraphEditor/GraphEditorTestHelpers.h` (header-only, not built on its own):

| File | Covers |
|------|--------|
| `GraphEditorTests.cpp` | Core: init/resize, mod-matrix visibility, module drag-and-drop (incl. Dual I/O defaults, split-block collapse), audio-file drop, drag-to-knob modulation, port-drag connections, Replace Module |
| `GraphEditorLayoutTests.cpp` | Grid-layout / anti-overlap, alignment-guide rendering, Macro Control bank runtime resize |
| `GraphEditorPolyLinkTests.cpp` | Pure `resolvePolyLink` pairing/scoring, plus Dual I/O toggle stereo-leg completeness |
| `GraphEditorDualIOTests.cpp` | Dual I/O split/collapse wiring (migrate-not-duplicate, mid-chain voice modules, one-undo-step) and render-identity checks |
| `GraphEditorIntegrationTests.cpp` | Poly connection integration through a full graph |
| `GraphEditorViewportTests.cpp` | Minimap visibility/model, zoom-perf cable memoization and zoom-gesture raster freeze |
| `GraphEditorSmartConnectionTests.cpp` | Smart-connection eligibility (mode, stereo/mono fan, incompatible pairs) |
| `GraphEditorSmartConnectionInsertTests.cpp` | Occupied audio destinations: default parallel-add, Ctrl insert-in-series, stereo fan correctness |
| `GraphEditorSmartConnectionPreviewTests.cpp` | Drop preview and Ctrl gesture plumbing |
| `GraphEditorSmartConnectionMatrixTests.cpp` | The three `TestWithParam` matrices: every FX insertable at the gap, vertical aim, and the full gesture-matrix contract table |
| `GraphEditorNodeActionsTests.cpp` | Per-node actions/identity: module title rename, double-click port disconnect, output-card identity, Locate Master |

`MacroContainerTest`/`MacroCollapse` and friends are split by topic under
`Tests/Macros/MacroContainer/`, sharing `addModuleAt`/`uuidOf`/`nodeIdForUuid`/`findComponent`/
`makeCanvasMouseEvent` in `Tests/Macros/MacroContainer/MacroContainerTestHelpers.h` (header-only,
not built on its own):

| File | Covers |
|------|--------|
| `MacroContainerTests.cpp` | Lifecycle: wrap/unwrap, persistence, snippets, delete, membership (incl. port-splice on a newly crossing cable) |
| `MacroContainerGeometryTests.cpp` | Geometry/canvas: collapse, group-or-toggle dispatch, hull/chip bounds, chip drag (incl. the real mouse path), rename-dialog guard, undo/redo, boundary-cable re-anchoring, the untrusted-patch trust boundary |
| `MacroContainerInteractionTests.cpp` | Chrome/interaction: collapse button, recolour (preview plus one-undo-step commit), card double-click-to-rename vs. expand, member right-click context menu, card right-click membership menu |

`AIChatComponentTest` is split by topic under `Tests/UI/Assistant/AIChatComponent/`, sharing the
mock `AIProvider`s, `findMessageList`/`findDescendantWithText`, and the `AIChatComponentTest`
fixture in `Tests/UI/Assistant/AIChatComponent/AIChatComponentTestFixture.h` (header-only, not built
on its own):

| File | Covers |
|------|--------|
| `AIChatComponentTests.cpp` | Core: init/resize, send-message plus classifier, `refreshModels()`/provider-install ordering, hosted-mode notices, account row, quota-error upgrade button, thumbs feedback, response-time/timeout display |
| `AIChatComponentHistoryTests.cpp` | History: unified history UI, upsell/downgrade strips, per-plan backend selection, clear-history, restoring a saved conversation, local save-on-every-exchange, rating sync, wrapped-height regressions |
| `AIChatComponentArrangeTests.cpp` | Arrange mode: selector gating on the timeline preference, explicit capability/prompt routing, validated/rejected timeline-card response flow |

`MacroAutoPort*Test` is split by topic under `Tests/Macros/MacroAutoPort/`, sharing the test module
stand-ins and graph/mouse helpers in `Tests/Macros/MacroAutoPort/MacroAutoPortTestHelpers.h`
(header-only, not built on its own):

| File | Covers |
|------|--------|
| `MacroAutoPortCreationTests.cpp` | Auto-creating ports on grouping: mono crossing, jack dedup, collapsed-stereo, dual-I/O stereo merge, poly-N, MIDI-as-separate-node, mod-routing-knob splice, grouping-is-one-undo-step |
| `MacroAutoPortUngroupTests.cpp` | Ungroup removes auto-created ports and splices cables back; presentation-count/tooltip rules; the auto-create-ports preference's modal-firing conditions |
| `MacroAutoPortDeleteTests.cpp` | Auto-delete a macro port once its last cable is gone, via `disconnectCable`/`disconnectPort` and via whole-node deletion |

The piano-roll editor's suite is split by topic under `Tests/UI/PianoRoll/`, all sharing the
`PianoRollFixture` (a `CountingRoll`-backed roll wired to a bare `TimelineDoc` plus
`AppUndoManager`, no `TimelinePanelComponent` needed) and mouse/keyboard-gesture helpers in
`Tests/UI/PianoRoll/PianoRollTestHelpers.h` (header-only, not built on its own):

| File | Covers |
|------|--------|
| `NoteSelectionModelTests.cpp` | `synth::ui::NoteSelectionModel` — mirrors `ClipSelectionModelTests.cpp`'s coverage, keyed on `synth::NoteId` |
| `NoteHitTestMarqueeTests.cpp` | `synth::ui::noteHitTestMarquee` — mirrors `clipHitTestMarquee`'s coverage |
| `PianoRollComponentTests.cpp` | Core: open/close lifecycle, the gesture table (single click deselects, DOUBLE-click creates/deletes, drag moves/resizes, drag-from-empty marquees), note length following the snap division, clip-window clamping, the roll's own beat↔x mapping (first bar reachable, zoom around the cursor, gridline density), the local playhead's strip-confined repaint seam, the Q button, and a snapshot smoke test |
| `PianoRollEditToolsTests.cpp` | The edit tools (Split/Glue/Erase/Mute/Draw) — each one's single-click gesture and no-op cases; QUANTIZE — the pitch-quantize verb, its header chip and both Option+Q shortcuts |
| `PianoRollClipboardTests.cpp` | The note clipboard — copy/cut/paste-at-playhead/duplicate/repeat/select-all, including the cross-clip paste the member clipboard exists for |
| `PianoRollArrowKeyTests.cpp` | ARROW-key editing — grid nudge, semitone/octave transpose, group clamping, the fall-through contract when nothing is selected; Alt+Left/Right note NAVIGATION — walking the doc's canonical note order, collapsing a multi-selection, the ends of the run |
| `PianoRollShortcutsTests.cpp` | REBINDABLE surface keys resolved through a `ShortcutManager` instead of the hardcoded defaults, including the "an unbound action has no key" rule and the two keys (Escape, Delete) that stay fixed on purpose |
| `PianoRollZoomTests.cpp` | WHEEL/ZOOM policy — the macOS Shift axis swap, natural-vs-inverted scroll sign, the anchored zoom commands, the wheel-zoom DIRECTION convention; SNAP vs the drawn grid — the chosen division stays visible with snap off |
| `PianoRollPaintingTests.cpp` | KEY LABELS (`paintKeysColumn`'s pure per-row decision); ROW MAPPING (`yForPitch`/`pitchForY` through `visiblePitches_`, scale-context filtering); NOTE COLOURING through `synth::ui::resolveNoteColour`; KEYS-COLUMN geometry (black-key inset seam plus snapshot smoke); the TOOLBAR ROW's three-band vertical layout above the ruler |
| `PianoRollScaleAssistTests.cpp` | Scale assist — the header button plus gutter shift, `quantisePitchesToScale`, `generateRandomNotesIntoClip`, per-clip memory, PropertiesFile persistence; the scale-panel SLIDE animation; SHOW ONLY SCALE NOTES as a header chip plus Option+S and the scale-aware Up/Down transpose it enables; the `ScaleAssistPanel` component in isolation via its own accessors |
| `PianoRollHeaderChipsTests.cpp` | Header button chip affordance (six chips — hover wash plus resting/active fill); GENERATE — add-to-existing vs replace, plus the six chips' distinct/non-overlapping bounds |
| `PianoRollAuditionTests.cpp` | NOTE AUDITION (`onAuditionNote`) — "clicking a note plays it" — through the roll's own callback and through the real `TimelinePanelComponent` → `TrackHeaderHost` wiring; KEYS-COLUMN audition (the virtual keyboard down the left gutter) |
| `PianoRollMouseTests.cpp` | MARQUEE multi-select from empty grid; BEAT-ANCHORED drag math / EDGE AUTO-SCROLL / FOLLOW PLAYHEAD; MULTI-NOTE RESIZE (incl. the Cmd unquantized resize and the clip-overrun prompt); CMD+DRAG unsnapped MOVE plus the velocity scrub's move to Option |

## State management

Persistence, serialization, and state restoration.

| Suite | What it covers |
|-------|----------------|
| PresetManagerTest | Preset listing, load all presets, default preset validation, audio output connectivity, all 7 factory presets load with zero pairwise bounding-box overlaps at `kCollisionGap=12`; `AllPresetsPositionsOnGrid` (every baked x,y %8==0); `estimateModuleSize` mirror (Sequencer/PolySequencer/MidiKeyboard→560 = `kDoubleWidth`, Attenuverter excluded) |
| UndoRedoTest | Add/remove modules, connections, parameter changes, complex sequences, rapid operations, auto-arrange is a single undo step (one Cmd+Z restores all pre-arrange positions) |
| AIStateMapperTest | Graph JSON round-trip serialization, parameter validation, modulation serialization, merge mode, schema generation; `MergeAutoConnectsNewAudioNodesToOutputByDefault` / `MergeSkipsAutoConnectWhenTheCallerOptsOut` — locks the merge-mode convenience wiring (new audio node → Audio Output) that AI patches rely on and snippet insertion opts out of, so the two cannot drift into each other. **Golden-data regression suite:** `FactoryTypeNamesRoundTrip` (every factory key serializes under the name that rebuilds it, catching a Poly Sequencer → "Sequencer" downgrade); `PolySequencerSurvivesRoundTrip`; `ParamIdsGolden` / `AuthorableModuleTypesGolden` pinned golden tables; `GraphToJSONEmitsSchemaVersionAndNodeUuids` plus `PatchWithoutSchemaVersionStillApplies`; `NodeUuidsAreStableAcrossTrustedRoundTrip` / `UntrustedApplyIgnoresIncomingNodeUuids` (trusted vs untrusted paths); `SchemaOmitsReservedFields` (schemaVersion, uuid, timeline absent from schema); `TimelineIsRefusedFromUntrustedPatchesOnly` (refused untrusted, accepted trusted); `MergeMode_TypeMismatchIsRejectedUntrusted` plus `*StillCreatesNewNodeWhenTrusted` plus `*SameIdSameTypeStillUpdatesInPlace` plus `*RemovedIdMayBeReusedForAnotherType` (merge-mode collision handling) |
| PatchDocumentTests | `Source/PatchDocument.h` — stashing unknown top-level JSON keys across a save/load round-trip so an older build never destroys a newer build's data (e.g. a future "timeline" key). Per-loaded-file scope (`newPatch()` clears); only the user preset save/load path (undo/redo/snippets/AI apply must not resurrect file-level keys). `RoundTripPreservesUnknownTopLevelKeys` (`loadFromVar` stashes, `toVar` re-merges); `PreservedKeysNeverOverwriteKnownKeys` (a stashed key cannot shadow a known key); `ClearDropsTheStash`; `SchemaVersionIsNeverStashed`; `GraphEditorSaveLoadRoundTripsUnknownKeysAndNewPatchClears` (end-to-end GraphEditor integration); `PreservedKeysNeverReachSetExtraState` |
| SnippetManagerTests | Module snippets / grouping (`Source/SnippetManager.{h,cpp}` — headless). **Extraction:** only selected modules; graph I/O nodes excluded even when selected; only connections with *both* endpoints inside the selection; positions normalised to the selection's top-left with relative layout preserved; `selectionOrigin` returns that same top-left, ignores ineligible/stale ids, and `AgreesWithTheOriginExtractionNormalisesAgainst` — the contract the clipboard leans on to place a paste relative to its source. **Extra state:** `includeExtraState` is off by default so a hand-editable `.agsnip` can never carry a `state` key into the trusted apply (`applyExtraStateToProcessor` reads it as a filename for Sampler), and `prepareForInsert` strips one even if a file has it; on, a Sampler's loaded file survives — that is the in-memory clipboard's opt-in; modulation stored as a `modulations` entry with the Attenuverter never becoming a snippet node; modulation leaving the selection dropped; stale/absent ids ignored. **Ids:** `nextFreeIdBase` above every existing uid; `prepareForInsert` renumbers plus offsets, never mutates its input (the library inserts one loaded snippet repeatedly), clamps away from `NodeID{0}`. **Insert:** merges without disturbing a pre-existing module of the same type (the merge-collision regression), places the group at the drop point, `PreservesParameterValuesThatLookNormalised` (a 0.5 Hz LFO rate on a 0.01–20 range survives — the strict-validate / trusted-apply split), rebuilds modulation chains, two inserts give two independent copies, malformed JSON rejected without partially applying, dangling connections dropped, `DoesNotSpliceTheInsertedGroupIntoTheSurroundingPatch` plus `DoesNotAttachInsertedModulesToAnExistingMidiSource` (merge-mode auto-connect opt-out — no wire may cross the snippet boundary on insert). **Persistence:** save/load/list/delete round-trip, insert from disk, name rewritten to the sanitised form, empty snippet and unusable name refused, corrupt files skipped by `listSnippets`, path separators stripped so a name cannot escape the directory, length capped |

## Output level

`Tests/Engine/OutputLevelTests.cpp` — the shared opt-in output-level stage
(`ModuleBase::addOutputLevelParameter` / `prepareOutputLevel` / `applyOutputLevel`) and the modules
that adopt it. Headless. The related hidden-amplifier audit and mix-audibility suites
(`Tests/Engine/GainStaging/`) are documented in [`gain-staging.md`](gain-staging.md).

| Suite | What it covers |
|-------|----------------|
| OutputLevelHelper | Unity default is bit-exact pass-through; only the declared leading audio channels are scaled (CV channels untouched); level 0 silences; a 1.0→0.0 step ramps over 10 ms (max per-sample step < 0.01, reaches zero within the block) instead of clicking; **bypass passes dry audio at level 0**; mute clears at unity; a legacy state blob with no `outputLevel` property loads at unity (old presets sound identical); level survives a state round-trip. Uses an in-test `ModuleBase` subclass so the helper is exercised free of any module's DSP |
| OutputLevelModules | Across all 10 adopting modules (Delay, Reverb, Chorus, Phaser, Flanger, Distortion, Bitcrusher, Pitch Shifter, Filter, Ring Modulator): parameter present and defaulting to unity; **parameter added last in the list** (only `muted` may follow it); level 0.5 halves the output sample-for-sample; level 0 silences; bypass still passes dry audio at level 0; mute clears at unity. Plus: Delay's level stays outside the feedback path (repeats survive a spell at level 0); Filter scales all 8 voices in poly mode; **`AttenuverterKeepsAmountAtParameterIndexOne`** pins the positional-parameter landmine shut |

## Stereo declaration

`Tests/Modules/StereoVoiceModule/StereoVoiceModuleDeclarationTests.cpp` (suite
`StereoDeclaration`) — the Dual I/O toggle is granted by `ModuleBase`'s constructor from a module's
channel shape (`ModuleBase::StereoAudio`), so these sweep the whole module factory rather than
checking modules one at a time. Headless.

Every factory module either matches the shape rule (**≥ 2 in, exactly 2 out** → toggle, collapsed by
default) or appears in one of two documented exception tables (`Declared` → toggle, split by
default; `None` → no toggle, with a written reason) — a new stereo FX passes with no edit, a new
exception fails until it is listed; the tables have no stale or no-longer-matching entries; **a bare
module with only the shape and no Dual I/O code at all inherits the parameter AND the collapsing
output jack**; the Ring Modulator (whose explicit registration was deleted) still behaves
identically; `AIStateMapper::dualIOCapableModuleTypes()` reports exactly the modules that have the
parameter; mute clears every channel and the CV inputs stay cleared (normal and bypassed blocks) for
every registry module in both jack states; **a patch authored before the toggle moved into the base
loads with identical layout** — explicit `dualIO` values survive, an omitted one falls to the
module's default, and the Ring Modulator (whose patch predates its toggle) opens collapsed — plus
the same claim on the `getStateInformation` XML path, where a blob with the `dualIO` attribute
stripped out loads at the module's own default (collapsed for `Auto`, split for `Declared`) rather
than at zero.

## Module adoption

`Tests/Modules/ModuleAdoptionTests.cpp` — enforces the standing rule that **every module whose
output carries audio has a level control**. Headless.

A hand-maintained table classifies every module the factory can build into `SharedStage` (adopted
`addOutputLevelParameter`), `OwnParameter` (has its own `level`/`gain`/`outputGain`/`inputGain`/
`makeupGain`) or `NoLevelByDesign` (CV/gate/MIDI output, with a rationale string).
`EveryAudioOutputModuleHasALevelControl` asserts each module matches its declared bucket —
including that an `OwnParameter` module does **not** also adopt the shared stage (two level knobs on
one panel). `EveryFactoryModuleIsClassified` is the tripwire: it harvests module names from
`AIStateMapper::getModuleSchema()` and fails when a newly added module is not classified, with a
message naming the three buckets. `ClassificationTableHasNoStaleEntries` catches the reverse — a
renamed/removed module leaving a dead row that silently stops enforcing anything.

## Layout

Pure/headless tests for the grid-layout and anti-overlap helpers in
`Tests/UI/Layout/LayoutUtilTests.cpp`. No JUCE GUI components required.

`LayoutUtilTest` covers `snap` round-trips (negative-safe, midpoint), `intersectsAny` gap
enforcement plus selfId exclusion, `findFreeSlot` returning the desired slot when clear and
resolving a dense cluster (the returned slot overlaps none), `computeAutoArrange` signal-depth
layering (x strictly increases per depth, Audio Output in the last column, no result-box overlaps);
**width-bucket mapping** (Sequencer/PolySequencer/MidiKeyboard→Double, Attenuverter→Narrow, all
others→Single); **bucket constants on grid** (`kNarrowWidth`/`kSingleWidth`/`kDoubleWidth` all
%8==0, `kDoubleWidth == 2 × kSingleWidth`); **column stride** (`kSingleWidth + kLayerGapX == 360`);
**Macro bank geometry** (`macroBankHeight` grows one `kMacroRowH` per macro, `macroRowCentreY`
evenly spaced and always inside the bank); **`resolveOverlapsAfterResize`** (no-op when clear,
pushes the neighbour below past the new bottom edge and on-grid, never moves the resized module,
cascades through a stack until nothing overlaps, shrinking moves nobody back).

## Theme system

`Tests/UI/Theme/ThemeTests.cpp`. All headless.

| Suite | What it covers |
|-------|----------------|
| ThemeBuiltInsTest | Built-in theme registration (obsidian/neon/warm), WCAG AA contrast (≥4.5) for all built-ins |
| ThemeLoaderTest | JSON round-trip (exact colour/metric/typography/treatment equality), `obsidian.gtheme.json` vs `makeObsidian()`, required-key rejection, bad hex rejection, treatment float clamping, schema version rejection, style string round-trip |
| ThemeTest (fixture) | Persistence plus restore via ApplicationProperties, broadcast on change / idempotency, unknown id rejection, user theme replace-by-id |
| ThemeLookAndFeelTest | All ColourId mappings, draw-helper smoke tests (`fillThemedBackground` / `drawModulePanel` / `drawConnectionWire` / `drawModulationRing` / `drawRotarySlider` into a headless image); `ApplyThemeSetsEveryColourId` extended to ListBox and TabbedButtonBar ColourIds; `RetintIconsCalledByApplyTheme` (`getIcon` non-null across 3 built-ins); `MetricsCodeOnlyFieldsHaveExpectedDefaults` (`toolbarHeight == 36`, `statusBarHeight == 24`, etc.); `MetricsCodeOnlyFieldsNotInJSON` (ThemeLoader output does not contain "toolbarHeight") |
| StyledWidgetSmokeTest | `drawComboBox` normal/pressed/disabled × 3 themes; `drawComboBoxTextWhenNothingSelected`; `drawPopupMenuItem` separator/highlighted/ticked/disabled/hasSubMenu; `getDefaultScrollbarWidth() == 6`; `drawScrollbar` V/H × over/down; `drawScrollbarButton`; `drawTabbedButtonBarBackground` plus `drawTabButton` active/inactive/hover with a snapshot pixel check |

## Cable colour

`Tests/UI/Graph/CableColourTests.cpp` covers `Source/UI/Graph/CableColour.h` and the cable
enumeration / hit-testing on `GraphEditor`. Layered: the pure resolver needs no GUI at all, the
canvas fixture builds a real two-module patch headlessly.

| Suite | What it covers |
|-------|----------------|
| CableColourCategoryTest | `categoryFor` totality over all 23 `ModuleType` values, groupings match the ModuleLibrary sections, persisted signal/category ids are unique and stable (`envelopes`, `modcv` spot-checked — renaming breaks saved user colours) |
| CableColourResolveTest | Each `CableSignal` maps to its theme token; `midiWire` differs from `audioWire` in every built-in theme; `BySourceCategory` uses the palette and ignores signal; the 8 category colours are mutually distinct per built-in theme; override precedence is mode-scoped; bypass alpha applies to the winning colour but NOT to `resolveCableBaseColour` (what swatches render); clearing an override restores the theme colour |
| CableColourPersistenceTest | Mode round-trip through `PropertiesFile`; override round-trip, untouched entries stay unset, reset removes the key entirely |
| CableColourThemeTest | `midiWire` plus `cableCategory` survive a `themeToJson` → `parseTheme` round-trip; an older theme with neither key still loads on defaults; a partial `cableCategory` object overrides only its named keys |
| CableGeometryTest | `buildCablePath` starts/ends exactly on the ports; `distanceToCable` ≈ 0 on the wire and large away from it |
| CableCanvasTest (fixture) | Enumeration reports the audio cable with the right signal plus source category; hit-test hits a point sampled from the drawn curve and misses far away; tolerance is respected; `disconnectCable` removes the graph edge; `colourForCable` follows the active mode and overrides |

## Icon library

`Tests/UI/Theme/IconLibraryTests.cpp` covers `Source/UI/Theme/IconLibrary.h/.cpp`.

`IconLibraryTest` covers `AllIconEnumValuesHaveEntry` (every `Icon < kCount` returns a non-null
`getDrawable` when assets are present); `NullFallbackWhenAssetsAbsent` (the no-assets path returns
nullptr without crashing); `ClonedDrawableIsIndependent` (two `getDrawable` calls return distinct
raw pointers); `TintColourChangeApplied`; `RetintMultipleSwitchesStable` (100× alternating
`setTintColour` loop, final colour matches the last set value); `SvgBinaryDataNamingConvention` (raw
BinaryData symbol non-null, which guards CMake renames); `TransportPlayIsScaffolding`;
`WaveformIconsLoad` (all four of WaveformSine/Saw/Square/Triangle non-null);
`WaveformIconsTintedToTextPrimary` (tint matches `textPrimary` after `retintIcons()`);
`WaveformIconKTableCount` (a compile-time `static_assert` on the table size);
`WaveformBinaryDataSymbolsPresent`.

## Status bar

`Tests/UI/Chrome/StatusBarTests.cpp` covers `Source/UI/Chrome/StatusBarComponent.h/.cpp` and the
`AudioEngine` accessors it reads: `ConstructsWithoutCrash`; `RendersNonEmptyImage`
(`createComponentSnapshot` at 400×24); `FormatCpu` (0.0%, 75.6%, 100.0%); `FormatVoices` (0→"0
voices", 1→"1 voice", 8→"8 voices"); `FormatPatch` (""→"Untitled", a named patch passes through);
`GatedRepaintDoesNotFireOnUnchangedValues` (`update()` twice with the same values repaints once);
`AudioEngine_GetActiveVoiceInfo_ReturnsZeroWithoutPolyModules`; `AudioEngine_CountsPolyMidiVoices`
(`maxVoices == 8` after adding a PolyMidiModule); `MasterMute_ZeroesOutput` (`setMasterMute(true)` →
the output buffer is all zeros post-`processBlock`).

## Frequency-domain views

`Tests/UI/ModuleViews/FrequencyResponseTests.cpp` covers
`Source/UI/ModuleViews/FrequencyResponseComponent.h`;
`Tests/UI/ModuleViews/EQCurveTests.cpp` covers the axis maths both frequency-domain views share
(`Source/UI/ModuleViews/FrequencyGrid.h`), the Parametric EQ view and its interaction model
(`Source/UI/ModuleViews/EQCurveComponent.h`), and the pop-out editor
(`Source/UI/ModuleViews/EQWindow.h`).

| Suite | What it covers |
|-------|----------------|
| FrequencyResponseTest | `findPeakBin` returns the max-magnitude bin (first index on ties, -1 for null/zero-length, 0 for a single element); `formatHzLabel` yields "100Hz"/"1kHz"/"10kHz"/"440Hz"/"1.5…kHz"; `freqToXStatic`'s log map is monotonic and pins 20 Hz→x=0, 20 kHz→x=width; `dbToYStatic` is monotonic across +20/0/−20 dB; `PaintSmoke` paints into a `juce::Image` with no crash and opaque pixels; `PlotDbCapsPeaksButLeavesDeepCuts` / `DbBelowWindowMapsPastBottom` — deep roll-off stays below `minDb` and maps past the view bottom (no floor stroke); `TimerRunsOnlyWhileVisible` — the 30 Hz timer is gated on visibility; `LowpassRollOffDoesNotStrokeBottomEdge` — the bottom-right edge is not an opaque accent floor line |
| FrequencyGridTest | `FreqToXSpansTheFullWidth`/`FreqToXIsLogarithmic`/`FreqToXIsMonotonic` — equal frequency *ratios* map to equal pixel distances; `XToFreqInvertsFreqToX`/`XToFreqHandlesZeroWidth`; `IndexToFreqSpansTheAxisAndIsMonotonic`/`IndexToFreqHandlesDegenerateCounts`; `DbToYPutsMaxAtTopAndMinAtBottom`/`DbToYIsMonotonicDownwards`/`YToDbInvertsDbToY`/`YToDbHandlesZeroHeight`; `FormatHzLabel`/`FindPeakBin`; `FilterViewStaticsDelegateToTheSharedGrid` — the regression lock that `FrequencyResponseComponent`'s public statics still agree with `FrequencyGrid` after the mapping code was hoisted out of it |
| EQCurveTest | `StaticsUseASymmetricThirtyDbWindow` — the ±30 dB window puts 0 dB at the vertical centre; `SpectrumIsOnByDefaultSoTheCurveHasABackdrop`; `FreqAtXAndGainAtYInvertTheDrawingTransform`; `GainAtYClampsToTheBandGainRange` — the ±30 dB view is wider than the ±24 dB parameter, so the top clamps; `PaintSmokeEmptyShowsTheHint`/`PaintSmokeWithActiveBands`/`PaintSmokeWithAllFourBandsAndSpectrum` — paint into a `juce::Image` with no crash and opaque pixels, including the FFT overlay driven by an explicit `timerCallback()`; `SilentInputDoesNotLeaveTheSpectrumRunning` — the silence gate that makes a default-on analyser free on an idle patch; `PaintAtDegenerateSizesDoesNotCrash` |
| EQCurveInteraction | The Cubase-style gestures, driven through the same methods the mouse handlers call. `AddPointEnablesTheBandAtTheClickedPosition`/`AddPointPicksTheSlotMatchingTheClickedFrequency`/`AddPointReturnsMinusOneOnceAllSlotsAreUsed`; `RemoveBandDisablesItAndClearsSelection`; `HitTestFindsEnabledHandlesAndIgnoresDisabledOnes` — including that a removed handle stops being hit-testable; `DragMovesTheBandInBothFrequencyAndGain` — with clamping past the edges; `ScrollAdjustsQMultiplicatively` — one notch doubles/halves Q and it saturates at the parameter bounds; `EveryEditIsBracketedByExactlyOneGesture` — balanced undo brackets, and a rejected add opens none; `DragIsNotBracketedPerStepSoOneDragIsOneUndoStep`; `ComponentPicksUpBandChangesMadeOnTheModule` — the card and pop-out window converge via the timer |
| EQWindowTest | `HostsACurveOverTheSameModule` — sizes itself, lays the curve out, and edits land on the shared module; `SpectrumToggleTracksTheCurve`; `ForwardsGestureCallbacksToTheCurve` — so pop-out edits are undoable; `PaintSmoke` |

## Scope

`Tests/UI/ModuleViews/ScopeTests.cpp` covers `Source/UI/ModuleViews/ScopeComponent.h`:
`isNoSignal` is true for peak ≤ 0.02f (boundary inclusive) and false above; `amplitudeToY` maps
+1→above centre, -1→below centre, 0→exact vertical centre, symmetric about centre; the silent
(No-Signal empty-state) and signal states both paint into a `juce::Image` with no crash.

## Wavetable oscillator

`Tests/Modules/WavetableOscillatorModule/` covers
`Source/Modules/WavetableOscillatorModule/`.

| Suite | What it covers |
|-------|----------------|
| WavetableOscillatorModuleTest | **Surface** — `FactoryInitialisation` (type/name/26 params/`kNumInputs`×`kNumOutputs` channels/16 visible in-jacks/2 out-jacks, and that the Audio R block clears the whole shared-CV block); `DeclaresEnoughOutputsForEveryCVInput` — guards the buffer-aliasing invariant (highest poly CV channel < `getTotalNumOutputChannels()`); `PortLabelsAndModulationTargets`; **`LegacyModCVChannelsKeepTheirIndices`** — Position/Octave/Coarse/Fine/Level keep their earlier raw channels, so patches saved against the six-jack module still route to the same targets; `StereoOutputPortsMapToSeparateChannelBlocks` — both legs are 8-wide poly-bus heads, and a channel between them is never a head; `LogicalPortMappingMonoAndPoly`; `ZeroChannelsDoesNotCrash`. **Core audio** — `ProducesAudioOnChannelZero`; `DefaultTablePositionZeroIsASine` (>95% of energy at the fundamental); `ScanningPositionChangesTheSpectrum`; `EveryBuiltInTableProducesAudio`; `LoadedFileChoiceFallsBackWhenNothingIsLoaded`; `OutputStaysBounded`; `LowSampleRateWithExtremeTuningStaysFinite` — 8 kHz sample rate with octave +4 / coarse +12 (>1 cycle per sample), guarding the phase wrap; **`HighNotesDoNotAlias`** — square at MIDI 108, worst magnitude in 200 Hz–3.5 kHz is <5% of the fundamental (the mip-selection guard); `PositionCVScansTheTableInMonoMode`, `LevelCVAttenuatesInMonoMode`; `PolyModeRendersOneVoicePerPitchCVChannel`, `PolyModePositionCVScansAllVoices`, `OctaveParameterTransposesInPolyMode`. **Warp** — `WarpAmountZeroLeavesTheWaveAlone` (sample-for-sample against an unwarped module), `WarpChangesTheSpectrum`, `WarpAmountCVDrivesTheWarp`. **Phase control** — `RetriggerPhaseStartsTheWaveWhereAsked` (0°/90°/270° land on the zero crossing and both peaks of the note-on block), `RandomPhaseDecorrelatesRepeatedNotes`, `UnisonSpreadDecorrelatesTheStack` (8 correlated sines pile up; spreading their start phases makes them cancel). **Stereo and voicing** — `DefaultsKeepAudioLIdenticalToAudioR` (the mono-compatibility guard for the balance pan law), `UnisonWidthSeparatesTheStereoLegs`, `PanShiftsEnergyBetweenTheLegs`, `PolyModeWritesBothAudioBlocks` (both 8-wide blocks sound, silent voices stay silent, the shared-CV block does not leak), `SubOscillatorAddsASubOctavePartial` (−1 and −2 octave), `StackModesTransposeUnisonVoices` (power chord puts energy at root/fifth/octave), `BlendFadesTheStackAgainstTheCentreVoice`, `RingModMultipliesBySyncInput` (sidebands appear, carrier suppressed), `HardSyncResetsThePhaseOnTheMasterEdge`. **Import and interpolation** — `FixedImportSizeSplitsOnThatBoundary`, `SingleCycleImportMakesExactlyOneFrame`, `PitchDetectImportFindsTheSourcePeriod` (a 512-sample period yields 32 frames with no 2nd harmonic — the octave-error guard), `SpectralImportKeepsMagnitudesAndDropsPhase` (opposed frames cancel at the midpoint on a normal import, survive on a spectral one), `HermiteInterpolationTracksLinearButStaysBounded`. **Browser and state** — `FolderBrowserScansStepsAndWraps` (non-audio files excluded, scanning loads nothing, next/prev wrap, an empty folder stays inert), `StateRoundTripRestoresNewParametersAndFolder` (both the binary blob and the `getExtraState` path). **Loading** — `LoadWavetableFileSplitsFrames`, `LoadedFramesKeepTheirDistinctHarmonics`, `LoadWavetableFileRejectsMissingAndInvalidFiles`, `LoadWavetableFileCapsFrameCount`, `ReloadingWhileRenderingStaysStable` (exercises the pending/retired handoff); `StateRoundTripRestoresParametersAndWavetable`, `StateRoundTripSurvivesAMissingWavetableFile`; **`TrustedGraphJSONRestoresTheWavetable`** — round-trips through `graphToJSON`/`applyJSONToGraph`, the path presets and undo actually use; `UntrustedPatchCannotNameAWavetableFileToOpen` — model-authored JSON must not reach `setExtraState` |
| AllWarpModes/WavetableWarpAliasTest | **`EveryWarpModeStaysCleanBelowTheFundamental`** — parameterised over all 11 warp modes. Each is swept at full warp on MIDI 108 with the squarest frame selected; the loudest bin in 300 Hz–0.85·f0 must stay under 5% of the loudest bin in the legitimate band (0.95·f0–20 kHz). No mode has legitimate content below the fundamental — they all reweight or multiply harmonics of f0 — so anything down there is folded. **A warp mode that aliases is a regression, not a feature: extend this suite when adding one.** |
| AllWarpModes/WavetableWarpBoundsTest | `EveryWarpModeStaysBounded` — the same 11 modes at full warp with 8-voice unison, 50-cent detune and the sub at full; both stereo legs must stay finite and under ±4.0 |
| WavetableMipGeometry | `LimitsDecreaseMonotonicallyToTheFundamental` — mip 0 holds 1023 harmonics, the coarsest holds 1, limits strictly decrease, and every mip's limit is within its own Nyquist with length ≥ 64 |
| MuteAndBypass/WavetableMuteBypassTest | `OutputIsSilentWhenMutedOrBypassed` — parametrized over mute and bypass; a pure source module clears on both (the documented `OscillatorModule` exception) |

## Wavetable display and curve editor

`Tests/UI/ModuleViews/WavetableDisplayTests.cpp` covers
`Source/UI/ModuleViews/WavetableDisplayComponent.h`: `quantisePosition` pins 0→0 and 1→`steps`,
clamps out-of-range input, is monotonic, and maps sub-bucket jitter to the same bucket (this is the
repaint gate); `RepeatedTimerTicksOnAnUnchangedModuleAreIdempotent` — five ticks leave the trace
bit-identical while a real position change is still picked up; `DisplayWaveformTracksScanPosition` —
position 0 and 1 traces differ by >0.2 and both stay bounded; `DisplayWaveformHandlesTinyPointCounts`
— 0 and 1 requested points still yield ≥2 samples; `PaintSmokeBuiltInTable`/
`PaintSmokeAtEveryScanPosition`/`PaintSmokeAtDegenerateSizes` — paints into a `juce::Image` with no
crash at 11 scan positions and at 0×0/1×1/4×80 bounds.

`Tests/UI/ModuleViews/CurveEditor/` (wired into the ADSR envelope card — see
[`../layout/visualizers.md`](../layout/visualizers.md#curveeditorcomponent)) covers the reusable
breakpoint curve editor at `Source/UI/ModuleViews/CurveEditor/` across four suites, sharing an
`EnvelopeShape`/`buildEnvelopeModel` fixture (`CurveEditorTestHelpers.h`) for the fixed
origin/attack-peak/hold-end/sustain/release-end topology the envelope card uses:

- **CurveModelTest** — node/segment bookkeeping, `valueAt` matching `synth::EnvelopeGenerator::shape`
  exactly, Fixed-mode ripple x-edits (`minSegment`/`maxSegment` clamping, `y` ignored when
  `!yMovable`, the origin never moving), Free-mode `addPoint`/`removePoint`/reorder.
- **CurveEditorGeometryTest** — time/level↔pixel round trips, the zero-duration segment's exact 12px
  display plateau and absent bend handle, hit-testing (nearest wins, exact tie goes to the earlier
  index, a pinned node is never hit, a node beats a bend handle), the handle tracking the curve's
  actual midpoint as bend changes, playhead mapping, grid ticks/labels.
- **CurveEditorInteractionTest** — the public drag/bend/add/remove primitives plus the real mouse
  path via synthesized `juce::MouseEvent`s (a plain click opens no gesture, a drag opens exactly
  one); a node drag freezes the visible range for its duration so repeated events at the same
  on-screen position do not runaway-grow/shrink the model's total duration, map back to the original
  duration when driven back to the start position, and re-fit once `mouseUp` clears the freeze;
  `setModel()` preserves a live drag across a same-topology model swap (the envelope card's
  parameter round trip) and cancels it — pairing `onGestureEnd` — on an actual topology change.
- **CurveEditorPaintTest** — a themed paint smoke test (accent-coloured curve pixels vs. plain
  background) plus an opt-in `CURVE_EDITOR_SNAPSHOT` PNG dump (the same pattern as `ADSR_CARD_PNG`
  in `ModuleComponentLayoutTests.cpp`).

## Minimap

`Tests/UI/Graph/MinimapComponentTests.cpp` covers `Source/UI/Graph/MinimapComponent.h/.cpp`. See
[`../layout/minimap.md`](../layout/minimap.md) for the feature itself.

| Suite | What it covers |
|-------|----------------|
| MinimapComponentTest | `computeWorldBounds` — an empty model falls back to a `kMinWorldSpan` square at the origin, nodes-only/viewport-only/both are contained, a single small node is clamped out to the min span while staying centred on it; `computeWorldToMap` — preserves aspect ratio in a non-square map area, maps inside and centres in the map area, zero-width world / zero-height map area stay NaN/Inf-free; `mapToWorld` round-trips several points through the forward transform; `setModel` reflects a differing model and no-ops on an equal one (observed via `getModel()`); `setViewport` updates only the viewport, leaving nodes/cables untouched; construction at a realistic size and a non-empty tooltip; paints a non-empty image after `setModel`; `mouseDown`/`mouseDrag` fire `onNavigate` with the point `mapToWorld` itself predicts; mouse and wheel handlers are safe no-ops with `onNavigate`/`onZoom` unset |
| MinimapModelTest | `MinimapModel::operator==`/`!=` — identical models are equal; differing viewport, node count, a moved node, a node differing only in `selected`, or a cable differing only in colour all make models unequal |

## End-to-end workflows

Full application workflow tests in `Tests/App/E2EWorkflowTests.cpp`. Each test constructs a complete
`MainComponent` with a mock AI provider and exercises real UI interaction code paths. The
conventions these depend on are in
[`test-patterns.md`](test-patterns.md#end-to-end-workflow-conventions).

| Area | What it covers |
|------|----------------|
| App initialization | Default patch has nodes/connections, panel toggle visibility, fresh undo state |
| Preset management | Load preset updates the graph, load all 7 presets without crash, load-modify-undo |
| Module management | Drop a module via `itemDropped()`, drop all 17 module types, delete a module, replace a module type |
| Connections | Connect ports via `beginConnectionDrag()`/`endConnectionDrag()` with `localPointToGlobal()` coordinate conversion, disconnect, MIDI connections, mod routing creates an attenuverter |
| Mod matrix | Add an empty routing, configure source/dest, adjust CV amount, delete a routing |
| Undo/redo | Undo add-module, complex sequences, preset-load-then-modify, a rapid 5-module sequence |
| Combined workflows | Full preset-modify-connect-undo-redo workflow |
| Layout / auto-arrange | Load a preset, call `autoArrange()`; all module components non-overlapping AND connection/node counts unchanged |

## AI patch validation

`Tests/AI/AIPatchValidationTests.cpp` is table-driven: one deliberately malformed patch per
`PatchValidationError` value, each asserting the **exact** enumerator rather than merely "was
rejected". `EveryErrorValueIsCovered` walks the enum and fails if a newly added value has no case,
so the table cannot silently fall behind the enum.

The same file pins the `getPatchSchema()` contract — the module-type enum matches the factory,
choice parameters are enumerated, `additionalProperties` stays open for numeric parameters, and no
reference data is offered as an output field.

`Tests/AI/AIPatchRetryTests.cpp` covers `applyPatchWithRetry` with a scripted provider double that
answers synchronously (so no message loop is needed): that the validation message reaches the model,
that retries stop at `kMaxPatchRetries`, and that the mode repair fires only for a rejected,
mode-less patch and never in the destructive merge-to-replace direction.

## AI patch fixture replay (corpus-driven, offline)

`Tests/AI/AIPatchFixtureReplayTests.cpp` is a **characterisation test**: it replays real model
output, recorded by [`ai-harnesses.md`](ai-harnesses.md)'s patch harness against `gpt-oss:20b` (the
confirmed production model) and at least one other local model, through the exact production path —
`extractJsonFromResponse -> JSON::parse -> validatePatch -> applyPatchWithRetry` — with no model and
no network, in milliseconds. `AIPatchValidationTests.cpp` above uses hand-written patches, which are
cleaner than what a real model emits; this suite is what actually notices a refactor breaking the
retry loop against real-world model output, and it runs in every CI build (unlike the harness
itself, which needs a live Ollama and is opt-in only).

The corpus lives at `Tests/fixtures/ai-patches/*.json` — verbatim `--json` output from the harness,
committed as-is (format documented in `Tools/AIPatchHarness/README.md`). Each record carries the raw
model text for its first answer (`rawResponse`) and, if `applyPatchWithRetry` needed a correction
round-trip, the raw text of each retry answer too (`retryResponses`). The test replays `rawResponse`
through validation directly, then drives `applyPatchWithRetry` against a `ReplayProvider` double
that hands back `retryResponses` in order (mirroring `AIPatchRetryTests.cpp`'s `ScriptedProvider`)
instead of calling a real model, and asserts the recorded outcome exactly: same parsed/valid/error,
same final applied-after-retry result. Scenario names are resolved against
`Tools/AIPatchHarness/Scenarios.h` — the one shared table both the harness and this test use, so a
fixture can never silently drift from the prompt that produced it.
`CorpusIsPopulatedAndContainsARejection` guards against an empty or all-success corpus quietly
passing.

**This suite is deliberately intolerant: a behaviour change in the retry/validation path should fail
it.** A failure is either a real regression (fix the code) or an intended change (re-record — see
"Recording the fixture corpus" in `Tools/AIPatchHarness/README.md`); never loosen an assertion here
just to turn the suite green again.
