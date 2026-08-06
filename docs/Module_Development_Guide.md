# Agent Synth Module Development Guide

This guide provides comprehensive instructions for developers looking to create new audio processing modules for the Agent Synth modular synthesizer. It covers the essential steps, coding standards, and best practices to ensure seamless integration and high-quality results.

## 1. Module Structure and Setup

All audio modules in Agent Synth inherit from `ModuleBase`, which in turn extends `juce::AudioProcessor`. This provides a consistent foundation for all modules.

### Basic Steps:

1.  **Create Header File (`Source/Modules/MyNewModule.h`):**
    Define your module class, inheriting from `ModuleBase`.
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

2.  **Create Source File (`Source/Modules/MyNewModule.cpp`):**
    Implement your module's constructor and core processing logic.
    ```cpp
    #include "MyNewModule.h"

    namespace synth {
    MyNewModule::MyNewModule()
        : ModuleBase("MyNewModule", /* numInputs */ 1, /* numOutputs */ 1) // Adjust I/O counts
    {
        // Initialize parameters here
    }

    void MyNewModule::prepareToPlay(double sampleRate, int samplesPerBlock) {
        ModuleBase::prepareToPlay(sampleRate, samplesPerBlock); // Always call base class method
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
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
            float* channelData = buffer.getWritePointer(channel);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
                // Example: simple pass-through
                // channelData[sample] = inputChannelData[sample];
            }
        }
    }
    } // namespace synth
    ```

3.  **Add to `Core` in `CMakeLists.txt`:**
    Ensure your new module's `.cpp` file is included in the `Core` target.
    ```cmake
    target_sources(Core PUBLIC
        # ... other source files
        Source/Modules/MyNewModule.cpp
    )
    ```

## 2. `ModuleBase` Inheritance and Core Methods

### `prepareToPlay(double sampleRate, int samplesPerBlock)`

*   Called before playback starts or when sample rate/buffer size changes.
*   **Always call `ModuleBase::prepareToPlay(sampleRate, samplesPerBlock);` first.**
*   Use this to reset any stateful DSP algorithms, update coefficients dependent on sample rate, or allocate resources.

### `processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)`

*   This is where your core audio processing happens.
*   `buffer` contains the audio input and should be filled with your module's output.
*   `midiMessages` can be processed if your module is MIDI-aware (e.g., an instrument or MIDI effect).
*   **Important — bypass/mute contract (every `processBlock` MUST honour both branches separately):**
    *   `isBypassed()` → **dry pass-through**: return early *without* clearing audio channels so the input signal flows through unchanged. Clear only CV channels at index ≥ 2 to prevent mod CV from leaking as audio. Never call `buffer.clear()` on bypass.
    *   `isMuted()` → **silence**: call `buffer.clear()` then return.
    *   Never combine the two into a single `if (isBypassed() || isMuted()) buffer.clear()` — that mutes on bypass instead of passing the signal through.
    *   **Exception — pure source modules** (e.g. `OscillatorModule`, `PolyMidiModule`) have no audio input, so there is no dry signal to pass through. These modules *do* clear their output on bypass.

## 3. Parameter Management and Modular Routing

All module parameters should be defined in the constructor using `addParameter()`. Use `juce::AudioParameterFloat`, `juce::AudioParameterInt`, `juce::AudioParameterChoice`, etc.

**Fully Modular Architecture Rule:** To ensure Agent Synth remains a truly modular environment, *every relevant continuous parameter in a module MUST expose a dedicated CV Input Port. However, modules MUST NOT explicitly define internal "Modulation Depth" or "Modulation Amount" parameters.* 

Instead of hardcoding internal modulation amounts or matrices, Agent Synth delegates all CV scaling to the host environment. The Graph automatically instantiates `Attenuverter` nodes on every CV connection cable. Modulation sources should provide raw normalized signals (e.g. [-1.0, 1.0] or [0.0, 1.0]), and the receiving node should map this raw incoming CV directly to its full native modulation range (e.g. +/- 4 octaves, or +/- 12 semitones). Users control depth dynamically via the smart cables. Do not crowd the module UI with redundant "Mod Amount" knobs!

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

## 4. DSP Standards for Professional Sound Quality

Adhering to these standards ensures the highest audio quality for Agent Synth modules:

*   **Parameter Smoothing**: For any continuous parameters (e.g., gain, cutoff, frequency), use `juce::SmoothedValue<float>` to avoid clicks and zipper noise when parameters are automated or changed rapidly.
    ```cpp
    juce::SmoothedValue<float> smoothedGain;
    // In prepareToPlay:
    smoothedGain.reset (getSampleRate(), 0.05); // 50ms smoothing time
    // In processBlock:
    smoothedGain.setTargetValue (*myGainParam);
    float currentGain = smoothedGain.getNextValue();
    // ... apply currentGain to audio
    ```

*   **Anti-Aliased Oscillators**: For waveforms with sharp edges (Square, Saw), use anti-aliasing techniques like PolyBLEP or band-limited synthesis to prevent aliasing artifacts at higher frequencies. JUCE's `juce::dsp::Oscillator` can handle this automatically if configured correctly.

*   **Oversampling**: For non-linear processes (e.g., distortion, waveshapers), employ `juce::dsp::Oversampling` to mitigate harmonic folding and aliasing.
    ```cpp
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    // In constructor:
    oversampler.reset(new juce::dsp::Oversampling<float>(buffer.getNumChannels(), 2, juce::dsp::Oversampling<float>::FilterType::filterHalfBandPOLYPHASE));
    // In prepareToPlay:
    oversampler->initProcessing(samplesPerBlock);
    // In processBlock:
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::AudioBlock<float> oversampledBlock = oversampler->processSamplesUp(block);
    // ... apply non-linear processing to oversampledBlock ...
    oversampler->processSamplesDown(block); // Downsample back to original rate
    ```

## 5. State Management (Preset Save/Load)

Override `getStateInformation()` and `setStateInformation()` to allow your module's parameters and internal state to be saved and loaded as part of a Agent Synth patch.

```cpp
void MyNewModule::getStateInformation(juce::MemoryBlock& destData) override {
    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos); // Save all parameters managed by APVTS
    // Save any other custom internal state variables
}

void MyNewModule::setStateInformation(const void* data, int sizeInBytes) override {
    juce::MemoryInputStream mis(data, sizeInBytes, false);
    apvts.replaceState(juce::ValueTree::readFromStream(mis));
    // Restore any other custom internal state variables
    // Ensure smoothed values are reset with their new target values if needed
}
```
*(Note: `apvts` refers to an `AudioProcessorValueTreeState` instance, which is commonly used in JUCE for parameter management.)*

## 6. Unit Testing Your Module

All new modules **must** have unit tests in the `Tests/` directory.

### Basic Steps:

1.  **Create Test File (`Tests/MyNewModuleTests.cpp`):**
    Use the GoogleTest framework to write your tests.
    ```cpp
    #include <gtest/gtest.h>
    #include "../Source/Modules/MyNewModule.h" // Correct include path

    TEST(MyNewModuleTest, InitialState) {
        synth::MyNewModule module;
        // Assert initial parameter values or state
        ASSERT_EQ(module.getName(), "MyNewModule");
        // ... more assertions
    }

    TEST(MyNewModuleTest, ProcessesAudioCorrectly) {
        synth::MyNewModule module;
        module.prepareToPlay(44100.0, 512); // Simulate prepareToPlay

        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear(); // Ensure buffer is empty or contains known input

        // Simulate some processing
        module.processBlock(buffer, juce::MidiBuffer());

        // Assert expected output characteristics
        // Example: check if buffer is not empty after processing a signal
    }
    ```

2.  **Add to `Tests/CMakeLists.txt`:**
    Ensure your test file is compiled as part of the test suite.
    ```cmake
    target_sources(Tests PUBLIC
        # ... other test files
        Tests/MyNewModuleTests.cpp
    )
    ```

3.  **Run Tests:**
    ```bash
    cmake --build build --target Tests
    ./build/Tests/Tests
    ```

### E2E Coverage

New modules are automatically covered by E2E workflow tests in `Tests/E2EWorkflowTests.cpp`. The `DropAllModuleTypes_NoCrash` test drops every registered module type and verifies it creates a graph node without crashing. If you add a new module type, add its name string to the `moduleTypes` array in that test.

## 7. Poly Module Channel Conventions

If your module supports polyphonic operation, follow the standard channel layout so the logical-port API and GraphEditor can correctly draw collapsed poly-bus wires and fan out drag-created connections.

**Rule:** In poly mode, voices occupy channels 0-7 (audio/pitch/gate) and the shared-CV block starts at channel 8. Declare `numOutputs >= highest CV input channel index you read`, to avoid JUCE `AudioProcessorGraph` buffer aliasing when `inputChan >= getTotalNumOutputChannels()`.

For example, a poly oscillator that reads CV on ch8-12 must declare at least 13 output channels even if only channels 0-7 carry audio. Channels 8-12 become silent pass-through outputs that prevent JUCE from aliasing them with another node's output buffer.

Override `mapInputChannel()` and `mapOutputChannel()` to return a `LogicalPort` for each raw channel, describing:
- `visibleJackIndex` — which visible jack the wire should anchor to in the UI
- `role` — `PortRole::Audio`, `PortRole::Pitch`, `PortRole::Gate`, `PortRole::ModCV`, etc.
- `isPolyGroupHead` — `true` only for the lowest channel of a fan (raw == 0 for voice fans, raw == 8 for the first shared-CV channel)
- `polyVoiceSpan` — `8` for the head of an 8-voice fan; `1` for all others

Only declare a `mapOutputChannel()` fan (`isPolyGroupHead = true`, `polyVoiceSpan = 8`) if your module actually emits one signal per voice — the way Oscillator, Filter and Noise fan their per-voice audio onto a single output jack. If your module sums voices down to a shared/stereo output instead (like VCA), leave `mapOutputChannel()` at the default. These overrides also drive connection *creation*, not just display: `GraphEditor` uses `getJackTargets()` (the inverse of `mapInput/OutputChannel`) and `resolvePolyLink()` to fan a dragged cable out to all N voices at once, so an incorrect fan on a summed output would make the editor try to wire N connections out of something that only ever produces one signal. See [docs/modulation.md — Creating Poly Connections](modulation.md#creating-poly-connections).

Override `isAutoPromotableModTarget()` to return `false` when `polyParam->get()` is true, so that poly CV connections are kept as plain `DirectCV` routings rather than being auto-wrapped in attenuverters.

See [docs/modules.md — Poly Channel Layout](modules.md#poly-channel-layout) for the full per-module channel table and [docs/modulation.md](modulation.md) for the routing model reference.

By following this guide, you can contribute robust and high-quality audio modules to Agent Synth.
