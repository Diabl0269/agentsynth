#pragma once

// A hosted instrument that plays a steady 220 Hz sine on both outputs whatever the MIDI says, loaded into a
// real HostedPluginModule through the stub backend. It stands in for a third-party synth in the click tests:
// everything the host does to its sound (mute, an envelope and VCA after it) is ours, the tone is not.

#include "../../StubPluginInstance.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include <chrono>
#include <cmath>
#include <memory>

namespace clickrig {

class SineInstrument : public synth::test::StubPluginInstance {
public:
    SineInstrument()
        : StubPluginInstance(0, 2, "Sine Instrument") {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        constexpr double kStep = 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float v = 0.5f * (float)std::sin(phase_);
            phase_ += kStep;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, v);
        }
    }

private:
    double phase_ = 0.0;
};

/** A HostedPluginModule prepared for `sampleRate` / `blockSize` with a SineInstrument published in it. */
inline std::unique_ptr<synth::HostedPluginModule> makeHostedSine(double sampleRate, int blockSize,
                                                                 synth::test::StubBackend& backend) {
    auto module = std::make_unique<synth::HostedPluginModule>();
    module->prepareToPlay(sampleRate, blockSize);
    juce::PluginDescription description;
    description.name = "Sine Instrument";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0x51E;
    description.deprecatedUid = 0x51E;
    description.fileOrIdentifier = "/nonexistent/test/path/SineInstrument.vst3";
    module->loadPlugin(description, backend);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!module->hasInstance() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    return module;
}

inline synth::test::StubBackend::Factory sineFactory() {
    return [] { return std::make_unique<SineInstrument>(); };
}

} // namespace clickrig
