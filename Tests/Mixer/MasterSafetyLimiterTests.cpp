// Master's always-on safety limiter: a -1 dBFS brickwall after the gain, ON by default, saved
// with the module, and ON for a project saved before the param existed. docs/mixer/mixer.md#safety-limiter.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/MasterSplice.h"
#include "Modules/MasterModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

juce::AudioProcessorParameter* findParam(juce::AudioProcessor& p, const juce::String& id) {
    for (auto* param : p.getParameters())
        if (auto* q = dynamic_cast<juce::AudioProcessorParameterWithID*>(param); q != nullptr && q->paramID == id)
            return param;
    return nullptr;
}

void setSafety(MasterModule& m, bool on) { findParam(m, MasterModule::kSafetyLimiterId)->setValueNotifyingHost(on); }

// Pushes a 1 kHz sine of `peakDb` dBFS through Mix L/R for `seconds`; returns the largest output magnitude
// over the second half (past the limiter's first-sample settling) and the largest input magnitude.
float renderSinePeak(MasterModule& master, float peakDb, double seconds = 0.5) {
    master.setPlayConfigDetails(MasterModule::kNumInputs, MasterModule::kNumOutputs, kRate, kBlock);
    master.prepareToPlay(kRate, kBlock);
    const float amp = juce::Decibels::decibelsToGain(peakDb);
    const int total = (int)(seconds * kRate);
    float peak = 0.0f;
    int n = 0;
    juce::MidiBuffer midi;
    for (int done = 0; done < total; done += kBlock) {
        juce::AudioBuffer<float> buffer(MasterModule::kNumInputs, kBlock);
        buffer.clear();
        for (int i = 0; i < kBlock; ++i, ++n) {
            const float v = amp * std::sin(juce::MathConstants<float>::twoPi * 1000.0f * (float)n / (float)kRate);
            buffer.setSample(0, i, v);
            buffer.setSample(1, i, v);
        }
        master.processBlock(buffer, midi);
        if (done >= total / 2)
            peak = std::max(peak, std::max(buffer.getMagnitude(0, 0, kBlock), buffer.getMagnitude(1, 0, kBlock)));
    }
    return peak;
}

} // namespace

TEST(MasterSafetyLimiter, DefaultsOn) {
    MasterModule master;
    auto* p = findParam(master, MasterModule::kSafetyLimiterId);
    ASSERT_NE(p, nullptr);
    EXPECT_GT(p->getValue(), 0.5f);
}

TEST(MasterSafetyLimiter, KeepsAPlusSixDbSineUnderTheCeiling) {
    MasterModule master;
    const float peak = renderSinePeak(master, 6.0f);
    EXPECT_LE(juce::Decibels::gainToDecibels(peak), MasterModule::kSafetyCeilingDb + 0.05f);
    EXPECT_GT(juce::Decibels::gainToDecibels(peak), -6.0f) << "limited, not muted";
}

TEST(MasterSafetyLimiter, OffPassesTheHotSineUnchanged) {
    MasterModule master;
    setSafety(master, false);
    const float peak = renderSinePeak(master, 6.0f);
    EXPECT_NEAR(juce::Decibels::gainToDecibels(peak), 6.0f, 0.1f);
}

TEST(MasterSafetyLimiter, LeavesAMinusTwelveDbSineAlone) {
    MasterModule master;
    const float peak = renderSinePeak(master, -12.0f);
    EXPECT_NEAR(juce::Decibels::gainToDecibels(peak), -12.0f, 0.1f);
}

TEST(MasterSafetyLimiter, RoundTripsThroughSaveAndLoad) {
    MasterModule a;
    setSafety(a, false);
    juce::MemoryBlock state;
    a.getStateInformation(state);
    MasterModule b;
    b.setStateInformation(state.getData(), (int)state.getSize());
    EXPECT_LT(findParam(b, MasterModule::kSafetyLimiterId)->getValue(), 0.5f);
}

TEST(MasterSafetyLimiter, AProjectSavedWithoutTheParamLoadsWithItOn) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    auto node = graph.addNode(synth::AIStateMapper::createModule("Master"));
    ASSERT_NE(node, nullptr);
    setSafety(*dynamic_cast<MasterModule*>(node->getProcessor()), false);

    auto json = synth::AIStateMapper::graphToJSON(graph);
    int stripped = 0;
    if (auto* nodes = json.getProperty("nodes", {}).getArray())
        for (auto& n : *nodes)
            if (n.getProperty("type", {}).toString() == "Master")
                if (auto* params = n.getProperty("params", {}).getDynamicObject()) {
                    EXPECT_TRUE(params->hasProperty(MasterModule::kSafetyLimiterId)) << "the param is saved";
                    params->removeProperty(MasterModule::kSafetyLimiterId);
                    ++stripped;
                }
    ASSERT_EQ(stripped, 1);

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));
    auto* loaded = dynamic_cast<MasterModule*>(synth::findMasterNode(graph)->getProcessor());
    ASSERT_NE(loaded, nullptr);
    EXPECT_GT(findParam(*loaded, MasterModule::kSafetyLimiterId)->getValue(), 0.5f);
}
