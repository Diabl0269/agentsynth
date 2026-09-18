# Module Development Guide

How to add a new audio-processing module: the files to create, the base-class contracts to honour,
the DSP standards, and the tests the build already enforces.

Per-module specs are in [`modules.md`](modules.md); the FX suite and the shared stereo/level stages
are in [`fx-modules.md`](fx-modules.md).

## Module structure and setup

Every module inherits from `ModuleBase`, which extends `juce::AudioProcessor`.

**1. Create the header (`Source/Modules/MyNewModule.h`).**

```cpp
#pragma once
#include "ModuleBase.h"

namespace synth {
class MyNewModule : public ModuleBase {
public:
    MyNewModule();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // ... other overrides for parameters, state, etc.
private:
    // Declare any internal state or DSP objects here
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyNewModule)
};
} // namespace synth
```

**2. Create the source file (`Source/Modules/MyNewModule.cpp`)** with the constructor and the core
processing:

```cpp
#include "MyNewModule.h"

namespace synth {
MyNewModule::MyNewModule()
    : ModuleBase("MyNewModule", /* numInputs */ 1, /* numOutputs */ 1) // Adjust I/O counts
{
    // Initialize parameters here
}

void MyNewModule::prepareToPlay(double sampleRate, int samplesPerBlock) {
    ModuleBase::prepareToPlay(sampleRate, samplesPerBlock); // Always call the base first
    // Initialize or reset any sample-rate dependent DSP objects
}

void MyNewModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages); // Use MIDI if relevant

    // Bypass: dry pass-through — return early WITHOUT clearing audio channels.
    // Clear only CV channels (index >= 2) so mod CV does not leak as audio.
    if (isBypassed()) {
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
        return;
    }

    // Mute: silence the entire output buffer.
    if (isMuted()) {
        buffer.clear();
        return;
    }

    // --- Your DSP processing goes here ---
}
} // namespace synth
```

**3. Add the source file to the `Core` target.** `Core` is built from an explicit file list in the
root `CMakeLists.txt` — there is no glob, so a file nobody lists is a file nobody compiles. Add both
the header and the `.cpp` to the `add_library(Core STATIC ...)` list, alongside the other
`Source/Modules/…` entries.

**4. Register the type in the module factory.** `synth::detail::moduleFactory()`
(`Source/AI/AIStateMapper/AIStateMapperInternal.h`) maps a type-name string to a constructor. That
map is what `AIStateMapper::createModule` resolves, what preset and patch JSON name in a node's
`"type"` field, and what every factory-sweeping test iterates. **Registering a module there makes it
model-authorable by default** — a module that must not be (one that names a file, hosts third-party
code, or is created only by an internal flow) also goes into `kNonAuthorableModuleTypes` in the same
header, and `validatePatch` then rejects it on the untrusted path. See
[`ai/patch-safety.md`](../ai/patch-safety.md).

**5. Give it a library row.** `Source/UI/Library/ModuleLibraryComponent/ModuleLibraryRows.cpp` owns
the sidebar's sections and the per-module blurb. A module with no row is reachable only from saved
JSON.

## ModuleBase inheritance and core methods

### prepareToPlay

- Called before playback starts, and again whenever the sample rate or buffer size changes.
- **Always call `ModuleBase::prepareToPlay(sampleRate, samplesPerBlock);` first.**
- Use it to reset stateful DSP algorithms, recompute sample-rate-dependent coefficients, and
  allocate resources. Allocation belongs here, never in `processBlock`.

### processBlock

- `buffer` carries the audio input and must be filled with this module's output.
- `midiMessages` can be processed if the module is MIDI-aware (an instrument or a MIDI effect).
- **Bypass/mute contract — every `processBlock` MUST honour both branches separately:**
  - `isBypassed()` → **dry pass-through**: return early *without* clearing audio channels so the
    input signal flows through unchanged. Clear only CV channels at index ≥ 2 to prevent mod CV from
    leaking as audio. Never call `buffer.clear()` on bypass.
  - `isMuted()` → **silence**: call `buffer.clear()` then return.
  - Never combine the two into a single `if (isBypassed() || isMuted()) buffer.clear()` — that mutes
    on bypass instead of passing the signal through.
  - **Exception — modules with no dry audio path** *do* clear their output on bypass, still using two
    separate branches. This covers **pure sources** with no audio input (for example
    `OscillatorModule`, `PolyMidiModule`) and **audio-in / CV-out taps** with no audio output (for
    example `EnvelopeFollowerModule`, `ComparatorModule`), where a dry pass-through would send
    audio-rate samples into a CV destination.

### Every audio-output module needs a level control

If the module writes audio to its output channels and does not already define its own `level`/`gain`
parameter, opt into the shared stage:

- `addOutputLevelParameter()` as the **last** `addParameter()` call in the constructor (before
  `addMuteParameter()`),
- `prepareOutputLevel(sampleRate)` in `prepareToPlay`,
- `applyOutputLevel(buffer, numAudioChannels)` as the last statement of the normal `processBlock`
  path — after both early returns, never inside them.

`ModuleAdoptionTests.EveryAudioOutputModuleHasALevelControl` enforces this: a new audio module
without a level control fails the build's test run until you either adopt the stage or classify the
module in that file's table. Skip it **only** if the module outputs pitch/gate CV or MIDI — scaling a
V/oct pitch CV detunes it and scaling a gate drops it under the `> 0.5f` trigger threshold. Rules and
rationale: [`fx-modules.md`](fx-modules.md#output-level-shared-stage).

### Look parameters up by ID, not index

Use `findParameterByID(processor, "paramID")`. `getParameters()[n]` silently repoints whenever a
parameter is added ahead of it.

**Why this is stated so firmly.** Moving the Dual I/O toggle into `ModuleBase`'s constructor shifted
every stereo module's own parameters by one, and the positional lookups that broke did not fail —
they resolved to the wrong parameter and quietly did nothing.

### Dual I/O is automatic — you opt out, never in

**Do not add a Dual I/O parameter.** `ModuleBase`'s constructor adds it when the channel shape says
stereo — `hasStereoOutputPairShape()`: **≥ 2 inputs and exactly 2 outputs** (audio on raw ch0/ch1,
any further inputs being CV). That shape also inherits the collapsing output jack, so
`ModuleBase("MyFX", 4, 2)` gives you the header toggle, the `"Audio"` / `Left` / `Right` labels and
the two-leg cable fan with no code of your own.

If the module has a stereo **input** pair as well, declare that part explicitly — it is not
inferable from the shape (Voice Mixer's ch0-7 are voice inputs; the Ring Modulator's ch0/ch1 are
Carrier and Modulator):

```cpp
juce::String getInputPortLabel(int i) const override { return stereoInputLabel(i, kNumCV, cvLabels); }
int getVisibleInputPortCount() const override { return stereoVisibleInputCount(kNumCV); }
LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, kNumCV); }
```

Opt out of the inference, or override it, with the constructor's fourth argument,
`ModuleBase::StereoAudio`:

| Value | Use it when | Effect |
|---|---|---|
| `Auto` (default) | ordinary stereo FX | shape decides; toggle ships **collapsed** |
| `Declared` | you have a second audio leg the shape cannot see — its own `kRightBase` block above the CV inputs, or a ch0/ch1 pair alongside further outputs | toggle ships **split**; you own your jack maps |
| `None` | the shape matches but there is no stereo pair (CV-only jacks, a hidden tap, two stereo input blocks) | no toggle |

`StereoDeclaration.EveryFactoryModuleFollowsTheShapeRuleOrADocumentedException` sweeps every factory
module against that rule, so a module that needs `Declared` or `None` must also be added to the
matching table in `Tests/Modules/StereoVoiceModule/StereoVoiceModuleDeclarationTests.cpp` **with a
written reason** — the build's test run fails until it is. That is deliberate: the decision cannot be
skipped, only made explicitly. Details and the current exception list:
[`fx-modules.md`](fx-modules.md#the-toggle-is-inherited-not-registered).

## Parameters and modular routing

Define every parameter in the constructor with `addParameter()`, using
`juce::AudioParameterFloat`, `juce::AudioParameterInt`, `juce::AudioParameterChoice` and friends.

**Every relevant continuous parameter MUST expose a dedicated CV input port, and no module may define
its own "Modulation Depth" or "Modulation Amount" parameter.** CV scaling is delegated to the graph,
which instantiates an `Attenuverter` node on every CV connection. A modulation source provides raw
normalised signals (`[-1, 1]` or `[0, 1]`) and the receiving node maps that incoming CV directly onto
its full native modulation range (±4 octaves, ±12 semitones, and so on); depth is the user's, set
dynamically on the smart cable.

**Why.** A per-module depth knob duplicates the attenuverter that is already on the cable, and two
controls for one quantity disagree the moment either is automated.

```cpp
MyNewModule::MyNewModule()
    : ModuleBase("MyNewModule", /* numInputs */ 1, /* numOutputs */ 1) {
    addParameter(myFloatParam = new juce::AudioParameterFloat(
        "floatID", // parameter ID
        "My Float Param", // parameter name
        juce::NormalisableRange<float>(0.0f, 1.0f), // range
        0.5f, // default value
        {}, // label
        juce::AudioProcessorParameter::genericParameter, // attributes
        [](float value) { return juce::String(value, 2); }, // value to text
        [](const juce::String& text) { return text.getFloatValue(); } // text to value
    ));
    // ... add other parameters
}
```

## DSP standards

**Parameter smoothing (enforced).** **Every `juce::AudioParameterFloat` a module owns must either be
smoothed or carry a one-line comment justifying why it isn't.** Timeline automation writes a
parameter's base value once per block (or per 64-sample slice) through `param->setValue(...)`, so
anything that reaches a DSP coefficient raw steps at block rate and zippers.
`Tests/Timeline/AutomationZipperTests.cpp` enforces the *coverage* half of this: it derives its cases
from the live module factory, so a new module — or a new float parameter on an existing one — is
swept automatically, and a module the config table neither covers nor excludes fails the build.

```cpp
juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain;

// In prepareToPlay — reset(), then SNAP to the current knob value. Snapping is what keeps a
// static render bit-identical to the un-smoothed version, which is the acceptance bar for
// touching an existing module (the golden .raw renders must not be regenerated).
smoothedGain.reset (sampleRate, 0.01);                 // 10 ms: an anti-click gain ramp
smoothedGain.setCurrentAndTargetValue (*myGainParam);

// In processBlock — retarget once per block, consume per sample.
smoothedGain.setTargetValue (*myGainParam);
for (int i = 0; i < numSamples; ++i)
    data[i] *= smoothedGain.getNextValue();
```

Ramp times in use: **5 ms** for filter cutoff/resonance/drive, **10 ms** for gains and levels,
**20 ms** for EQ coefficients and envelope sustain, **50 ms** for delay times and LFO rates. Where
the underlying DSP object only takes a parameter once per block
(`juce::dsp::Compressor::setThreshold`, `juce::dsp::Chorus::setCentreDelay`, a biquad coefficient
set), advance the smoother a block at a time instead — `getCurrentValue()` before the call and
`skip(numSamples)` after.

Legitimate reasons **not** to smooth, each of which belongs in a comment next to the read:

- **Frequencies and rates** (oscillator detune, LFO rate, S&H clock, sampler playback pitch): a step
  changes a phase *increment*, and the phase itself stays continuous, so there is no discontinuity.
- **Detector time constants** (compressor/limiter attack & release, envelope-follower attack &
  release): a step changes how fast a follower tracks, never the current gain.
- **Values consulted only at a discrete event** (sequencer BPM and gate length, sampler grain size /
  density / spray, wavetable retrigger phase, LFO glide): they shape the next event, not the samples
  between events.
- **Parameters the wrapped DSP object already smooths internally** — check before adding a second
  smoother, which only adds lag. `juce::Reverb` smooths all five of its parameters;
  `juce::dsp::Chorus`/`Phaser` smooth feedback and mix (`DryWetMixer`) but *not* centre delay /
  centre frequency.

**Anti-aliased oscillators.** For waveforms with sharp edges (Square, Saw), use PolyBLEP or
band-limited synthesis to prevent aliasing at higher frequencies. `juce::dsp::Oscillator` can handle
this automatically when configured correctly.

**Oversampling.** For non-linear processes (distortion, waveshapers), use `juce::dsp::Oversampling`
to mitigate harmonic folding and aliasing.

```cpp
std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
// In constructor:
oversampler.reset(new juce::dsp::Oversampling<float>(buffer.getNumChannels(), 2,
                                                     juce::dsp::Oversampling<float>::FilterType::filterHalfBandPOLYPHASE));
// In prepareToPlay:
oversampler->initProcessing(samplesPerBlock);
// In processBlock:
juce::dsp::AudioBlock<float> block(buffer);
juce::dsp::AudioBlock<float> oversampledBlock = oversampler->processSamplesUp(block);
// ... apply non-linear processing to oversampledBlock ...
oversampler->processSamplesDown(block); // Downsample back to original rate
```

**Only gain controls may add gain.** `ModuleGainAudit` sweeps every parameter of every audio module
and fails above +6 dB unless that parameter is allow-listed with a reason. See
[`development/gain-staging.md`](../development/gain-staging.md).

## State (preset save/load)

**Parameters need no state code at all.** `ModuleBase::getStateInformation` /
`setStateInformation` already serialise every `juce::AudioProcessorParameterWithID` a module owns,
keyed by `paramID`, into a `"ModuleState"` value tree — and `AIStateMapper` keys by `paramID` too. So
parameter order is never part of a module's saved shape, and adding a parameter cannot repoint an
existing patch.

**Non-parameter state goes through `getExtraState()` / `setExtraState()`**, which `AIStateMapper`
serialises as the node's `"state"` object. That is what a preset load, undo and redo actually replay
— a value stored only in the binary `ModuleState` blob is silently dropped by those paths.

**`setExtraState` is reached on the TRUSTED path only.** A module is free to interpret extra state as
a filename, so honouring it for model-authored output would turn a patch suggestion into an arbitrary
file read. Untrusted JSON never reaches it. See [`ai/patch-safety.md`](../ai/patch-safety.md).

Override `getStateInformation`/`setStateInformation` yourself only when the plain
`juce::AudioProcessor` contract needs something the parameter sweep cannot carry; call the base
implementation and add your own bytes around it, and reset smoothed values to their new targets after
a restore.

## Tests

Every new module **must** have unit tests under `Tests/`, following the repo's
`Tests/<Area>/<Class>/<Class><Topic>Tests.cpp` layout (`Tests/Modules/MyNewModuleTests.cpp` for a
single-topic module). Includes are **Source-rooted**, never relative:

```cpp
#include "Modules/MyNewModule.h"
#include <gtest/gtest.h>

TEST(MyNewModuleTest, InitialState) {
    synth::MyNewModule module;
    ASSERT_EQ(module.getName(), "MyNewModule");
}

TEST(MyNewModuleTest, ProcessesAudioCorrectly) {
    synth::MyNewModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    module.processBlock(buffer, juce::MidiBuffer());

    // Assert expected output characteristics
}
```

Add the test file to the `add_executable(Tests ...)` list in `Tests/CMakeLists.txt`, with a path
relative to `Tests/` (`Modules/MyNewModuleTests.cpp`). Then:

```bash
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target Tests
./build/Tests/Tests
```

The full build/test reference is in [`development/testing.md`](../development/testing.md).

**E2E coverage.** `Tests/App/E2EWorkflowTests.cpp`'s `DropAllModuleTypes_NoCrash` drops every
registered module type and verifies it creates a graph node without crashing. Its `moduleTypes` array
is hand-maintained, so add the new module's name string to it.

## Poly module channel conventions

If the module supports polyphonic operation, follow the standard channel layout so the logical-port
API and GraphEditor can correctly draw collapsed poly-bus wires and fan out drag-created connections.

**Rule:** in poly mode, voices occupy channels 0-7 (audio/pitch/gate) and the shared-CV block starts
at channel 8. Declare `numOutputs >= the highest CV input channel index you read`, to avoid JUCE
`AudioProcessorGraph` buffer aliasing when `inputChan >= getTotalNumOutputChannels()`. A poly
oscillator that reads CV on ch8-12 must therefore declare at least 13 output channels even if only
channels 0-7 carry audio; channels 8-12 become silent pass-through outputs that prevent JUCE from
aliasing them with another node's output buffer.

Override `mapInputChannel()` and `mapOutputChannel()` to return a `LogicalPort` for each raw channel,
describing:

- `visibleJackIndex` — which visible jack the wire should anchor to in the UI
- `role` — `PortRole::Audio`, `PortRole::Pitch`, `PortRole::Gate`, `PortRole::ModCV`, etc.
- `isPolyGroupHead` — `true` only for the lowest channel of a fan (raw == 0 for voice fans, raw == 8
  for the first shared-CV channel)
- `polyVoiceSpan` — `8` for the head of an 8-voice fan; `1` for all others

**Declare a `mapOutputChannel()` fan (`isPolyGroupHead = true`, `polyVoiceSpan = 8`) only if the
module actually emits one signal per voice** — the way Oscillator, Filter and Noise fan their
per-voice audio onto a single output jack. If the module sums voices down to a shared/stereo output
instead (like VCA), leave `mapOutputChannel()` at the default. These overrides also drive connection
*creation*, not just display: `GraphEditor` uses `getJackTargets()` (the inverse of
`mapInput/OutputChannel`) and `resolvePolyLink()` to fan a dragged cable out to all N voices at once,
so an incorrect fan on a summed output would make the editor try to wire N connections out of
something that only ever produces one signal. See
[`modulation.md`](modulation.md#creating-poly-connections).

Override `isAutoPromotableModTarget()` to return `false` when `polyParam->get()` is true, so that
poly CV connections stay plain `DirectCV` routings rather than being auto-wrapped in attenuverters.

The full per-module channel table is in
[`poly-channel-layout.md`](poly-channel-layout.md#channel-table), and the routing model reference is
in [`modulation.md`](modulation.md).
