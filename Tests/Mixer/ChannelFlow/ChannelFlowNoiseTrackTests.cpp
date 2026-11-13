// ChannelFlowNoiseTrackTests.cpp
//
// A Noise instrument track ("Add Track -> Instrument -> Noise" and a model's addInstrumentTrack op): the
// Noise module takes no MIDI, so the build draws no Track In -> Noise MIDI cable, yet the ADSR is still
// gated by Track In and the noise is audible only while a note plays.

#include "../../FakeAudioIODevice.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MainComponent/MainComponent.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {

void addNoiseTrackFromMenu(MainComponent& mc) {
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentNoiseMenuId);
}

struct NoiseParts {
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    juce::AudioProcessorGraph::Node* noise = nullptr;
    juce::AudioProcessorGraph::Node* adsr = nullptr;
    juce::AudioProcessorGraph::Node* vca = nullptr;
    juce::AudioProcessorGraph::Node* strip = nullptr;
};

NoiseParts findParts(MainComponent& mc) {
    auto& graph = mc.getAudioEngine().getGraph();
    const auto& macro = mc.getGraphEditor().getMacros().getAll().front();
    return {findMacroMemberOfTypeCFT(graph, macro, ModuleType::TimelineMidiSource),
            findMacroMemberOfTypeCFT(graph, macro, ModuleType::Noise),
            findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR),
            findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA),
            findMacroMemberOfTypeCFT(graph, macro, ModuleType::ChannelStrip)};
}

void expectNoiseTrackTopology(MainComponent& mc) {
    auto& graph = mc.getAudioEngine().getGraph();
    const auto parts = findParts(mc);
    ASSERT_NE(parts.trackIn, nullptr);
    ASSERT_NE(parts.noise, nullptr);
    ASSERT_NE(parts.adsr, nullptr);
    ASSERT_NE(parts.vca, nullptr);
    ASSERT_NE(parts.strip, nullptr);

    const auto midi = juce::AudioProcessorGraph::midiChannelIndex;
    EXPECT_FALSE(graph.isConnected({{parts.trackIn->nodeID, midi}, {parts.noise->nodeID, midi}}))
        << "Noise takes no MIDI, so no cable is drawn into it";
    EXPECT_TRUE(graph.isConnected({{parts.trackIn->nodeID, midi}, {parts.adsr->nodeID, midi}}))
        << "Track In still gates the ADSR";
    EXPECT_TRUE(graph.isConnected({{parts.noise->nodeID, 0}, {parts.vca->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{parts.noise->nodeID, 0}, {parts.vca->nodeID, VCAModule::kRightBase}}))
        << "a mono source feeds both VCA legs";
    EXPECT_TRUE(graph.isConnected({{parts.adsr->nodeID, 0}, {parts.vca->nodeID, 1}}));
    for (const auto& c : graph.getConnections())
        EXPECT_FALSE(c.destination.nodeID == parts.noise->nodeID && c.source.isMIDI());
}

struct Rig {
    synth::test::FakeAudioIODevice fake{0, 2};
    AudioEngine& engine;
    TimelineMidiSourceModule& trackIn;
    Rig(AudioEngine& e, TimelineMidiSourceModule& t)
        : engine(e)
        , trackIn(t) {
        engine.audioDeviceAboutToStart(&fake);
    }
    ~Rig() { engine.audioDeviceStopped(); }
    // RMS over the last 4 of `blocks` blocks.
    float level(int blocks = 60) {
        const int n = synth::test::kFakeDeviceBlockSize;
        std::vector<float> left((size_t)n), right((size_t)n);
        float* outs[] = {left.data(), right.data()};
        float sum = 0.0f;
        for (int b = 0; b < blocks; ++b) {
            engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, n, {});
            if (b >= blocks - 4)
                for (int i = 0; i < n; ++i)
                    sum += left[(size_t)i] * left[(size_t)i];
        }
        return std::sqrt(sum / (4.0f * (float)n));
    }
};

} // namespace

TEST_F(ChannelFlowTest, NoiseTrackFromTheMenuHasNoMidiCableIntoNoiseButTheAdsrIsGated) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getGraphEditor().setDefaultDualIOForNewModules(true); // split jacks, so the VCA's Right leg stays wired
    addNoiseTrackFromMenu(mc);
    expectNoiseTrackTopology(mc);
}

TEST_F(ChannelFlowTest, NoiseTrackFromAnAddInstrumentTrackOpBuildsTheSameChain) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getGraphEditor().setDefaultDualIOForNewModules(true);
    const auto applied = mc.getAiServiceForTest().applyTimelineOps(
        juce::JSON::parse(R"({"timelineOps": [{"op": "addInstrumentTrack", "name": "Hiss", "instrument": "Noise"}]})"));
    ASSERT_TRUE(applied.ok) << applied.message;
    expectNoiseTrackTopology(mc);
}

TEST_F(ChannelFlowTest, NoiseTrackIsBuiltMonoAndPolyNoiseOpIsRejected) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    addNoiseTrackFromMenu(mc);
    const auto parts = findParts(mc);
    ASSERT_NE(parts.noise, nullptr);
    for (auto* param : parts.noise->getProcessor()->getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(param); b != nullptr && b->paramID == "poly")
            EXPECT_FALSE(b->get());

    const auto rejected = mc.getAiServiceForTest().applyTimelineOps(juce::JSON::parse(
        R"({"timelineOps": [{"op": "addInstrumentTrack", "name": "Wind", "instrument": "Noise", "poly": true}]})"));
    EXPECT_FALSE(rejected.ok);
    EXPECT_TRUE(rejected.message.contains("poly")) << rejected.message.toStdString();
}

// Audible while a note is held, silent after the release: the track's sound comes from the envelope.
TEST_F(ChannelFlowTest, NoiseTrackSoundsOnlyWhileANotePlays) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    addNoiseTrackFromMenu(mc);
    const auto parts = findParts(mc);
    auto* trackIn = dynamic_cast<TimelineMidiSourceModule*>(parts.trackIn->getProcessor());
    ASSERT_NE(trackIn, nullptr);

    Rig rig(mc.getAudioEngine(), *trackIn);
    EXPECT_LT(rig.level(), 1.0e-3f) << "no note yet: the envelope is closed";
    trackIn->pushAuditionNote(60, 100, true);
    EXPECT_GT(rig.level(), 0.01f) << "a held note opens the envelope and the noise is heard";
    trackIn->pushAuditionNote(60, 100, false);
    EXPECT_LT(rig.level(400), 1.0e-3f) << "after the release the track is silent";
}
