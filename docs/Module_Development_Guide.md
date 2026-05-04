# AgentSynth Module Development Guide

This guide provides comprehensive instructions for developers looking to create new audio processing modules for the AgentSynth modular synthesizer. It covers the essential steps, coding standards, and best practices to ensure seamless integration and high-quality results.

## 1. Module Structure and Setup

All audio modules in AgentSynth inherit from `ModuleBase`, which in turn extends `juce::AudioProcessor`. This provides a consistent foundation for all modules.

### Basic Steps:

1.  **Create Header File (`Source/Modules/MyNewModule.h`):**
    Define your module class, inheriting from `ModuleBase`.
    ```cpp
    #pragma once
    #include "ModuleBase.h"

    namespace gsynth {
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
    } // namespace gsynth
    ```

2.  **Create Source File (`Source/Modules/MyNewModule.cpp`):**
    Implement your module's constructor and core processing logic.
    ```cpp
    #include "MyNewModule.h"

    namespace gsynth {
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

        // Clear output if no input, or if module is bypassed
        if (isBypassed()) {
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
    } // namespace gsynth
    ```

3.  **Add to `AgentSynthCore` in `CMakeLists.txt`:**
    Ensure your new module's `.cpp` file is included in the `AgentSynthCore` target.
    ```cmake
    target_sources(AgentSynthCore PUBLIC
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
*   **Important**: If your module is bypassed or receives no active input, `buffer.clear()` the output to prevent unwanted noise or signals.

## 3. Parameter Management and Modular Routing

All module parameters should be defined in the constructor using `addParameter()`. Use `juce::AudioParameterFloat`, `juce::AudioParameterInt`, `juce::AudioParameterChoice`, etc.

**Fully Modular Architecture Rule:** To ensure AgentSynth remains a truly modular environment, *every relevant continuous parameter in a module MUST expose a dedicated CV Input Port. However, modules MUST NOT explicitly define internal "Modulation Depth" or "Modulation Amount" parameters.* 

Instead of hardcoding internal modulation amounts or matrices, AgentSynth delegates all CV scaling to the host environment. The Graph automatically instantiates `Attenuverter` nodes on every CV connection cable. Modulation sources should provide raw normalized signals (e.g. [-1.0, 1.0] or [0.0, 1.0]), and the receiving node should map this raw incoming CV directly to its full native modulation range (e.g. +/- 4 octaves, or +/- 12 semitones). Users control depth dynamically via the smart cables. Do not crowd the module UI with redundant "Mod Amount" knobs!

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

Adhering to these standards ensures the highest audio quality for AgentSynth modules:

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

Override `getStateInformation()` and `setStateInformation()` to allow your module's parameters and internal state to be saved and loaded as part of a AgentSynth patch.

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
        gsynth::MyNewModule module;
        // Assert initial parameter values or state
        ASSERT_EQ(module.getName(), "MyNewModule");
        // ... more assertions
    }

    TEST(MyNewModuleTest, ProcessesAudioCorrectly) {
        gsynth::MyNewModule module;
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
    target_sources(AgentSynthTests PUBLIC
        # ... other test files
        Tests/MyNewModuleTests.cpp
    )
    ```

3.  **Run Tests:**
    ```bash
    cmake --build build --target AgentSynthTests
    ./build/Tests/AgentSynthTests
    ```

### E2E Coverage

New modules are automatically covered by E2E workflow tests in `Tests/E2EWorkflowTests.cpp`. The `DropAllModuleTypes_NoCrash` test drops every registered module type and verifies it creates a graph node without crashing. If you add a new module type, add its name string to the `moduleTypes` array in that test.

By following this guide, you can contribute robust and high-quality audio modules to AgentSynth.
