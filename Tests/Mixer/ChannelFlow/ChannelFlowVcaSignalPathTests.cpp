// ChannelFlowVcaSignalPathTests.cpp
//
// What a user hears from "+ Track -> Instrument -> Oscillator" (Track In -> Oscillator -> ADSR/VCA ->
// Gate ...) while they unplug cables. Every case drives the REAL engine through the fake device with
// a note held (Track In's audition path) and reads the Audio Output level.
//
// The bug these pin: with the default preference (collapsed jacks) the chain still wired the
// Oscillator -> VCA and VCA -> Gate RIGHT legs, which have no jack on a collapsed split-block module.
// The cables were invisible and could not be unplugged, so removing the one cable the user could see
// left the sound playing through the hidden one.

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

struct Levels {
    float left = 0.0f;
    float right = 0.0f;
};

// One started device with a note held; level() renders more blocks and reports the output RMS.
struct Rig {
    synth::test::FakeAudioIODevice fake{0, 2};
    AudioEngine& engine;
    Rig(AudioEngine& e, TimelineMidiSourceModule& trackIn)
        : engine(e) {
        engine.audioDeviceAboutToStart(&fake);
        trackIn.pushAuditionNote(60, 100, true);
    }
    ~Rig() { engine.audioDeviceStopped(); }
    Levels level(int blocks = 40) {
        const int n = synth::test::kFakeDeviceBlockSize;
        std::vector<float> left((size_t)n), right((size_t)n);
        float* outs[] = {left.data(), right.data()};
        Levels sum;
        for (int b = 0; b < blocks; ++b) {
            engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, n, {});
            if (b >= blocks - 4)
                for (int i = 0; i < n; ++i) {
                    sum.left += left[(size_t)i] * left[(size_t)i];
                    sum.right += right[(size_t)i] * right[(size_t)i];
                }
        }
        return {std::sqrt(sum.left / (4.0f * (float)n)), std::sqrt(sum.right / (4.0f * (float)n))};
    }
};

struct Track {
    MainComponent mc{std::make_unique<MockProviderCFT>()};
    juce::AudioProcessorGraph::Node* vca = nullptr;
    TimelineMidiSourceModule* trackIn = nullptr;

    Track(ChannelFlowTest& fixture, bool splitJacks) {
        (void)fixture;
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.getGraphEditor().setDefaultDualIOForNewModules(splitJacks);
        mc.getTimelinePanel().applyAddTrackMenuChoice(
            synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
        auto& graph = mc.getAudioEngine().getGraph();
        const auto& macro = mc.getGraphEditor().getMacros().getAll().front();
        vca = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
        if (auto* n = findMacroMemberOfTypeCFT(graph, macro, ModuleType::TimelineMidiSource))
            trackIn = dynamic_cast<TimelineMidiSourceModule*>(n->getProcessor());
    }

    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }

    // The audio cables on the VCA's input (isInput) or output side, excluding its Gain CV input (ch1).
    std::vector<juce::AudioProcessorGraph::Connection> audioCables(bool isInput) {
        std::vector<juce::AudioProcessorGraph::Connection> found;
        for (const auto& c : graph().getConnections()) {
            if (c.source.isMIDI())
                continue;
            if (isInput && c.destination.nodeID == vca->nodeID && c.destination.channelIndex != 1)
                found.push_back(c);
            if (!isInput && c.source.nodeID == vca->nodeID)
                found.push_back(c);
        }
        return found;
    }
};

} // namespace

// Collapsed jacks (the default): the VCA shows one Audio in and one Audio out, and the graph must hold
// exactly one cable for each. Unplugging either one stops the sound.
TEST_F(ChannelFlowTest, CollapsedInstrumentTrackHasNoCableOnAHiddenRightJack) {
    Track t(*this, /*splitJacks=*/false);
    ASSERT_NE(t.vca, nullptr);
    ASSERT_NE(t.trackIn, nullptr);
    auto* vca = dynamic_cast<VCAModule*>(t.vca->getProcessor());
    ASSERT_NE(vca, nullptr);
    ASSERT_FALSE(vca->isDualIO());

    for (const auto& c : t.graph().getConnections()) {
        if (c.source.isMIDI())
            continue;
        EXPECT_FALSE(c.source.nodeID == t.vca->nodeID && c.source.channelIndex >= VCAModule::kRightBase)
            << "hidden VCA Audio R output still wired";
        EXPECT_FALSE(c.destination.nodeID == t.vca->nodeID && c.destination.channelIndex >= VCAModule::kRightBase)
            << "hidden VCA Audio R input still wired";
    }
    EXPECT_EQ(t.audioCables(true).size(), 1u);
    EXPECT_EQ(t.audioCables(false).size(), 2u) << "one VCA jack fans onto the Gate's collapsed pair";

    Rig rig(t.mc.getAudioEngine(), *t.trackIn);
    const auto playing = rig.level();
    EXPECT_GT(playing.left, 0.1f);
    EXPECT_GT(playing.right, 0.1f);

    // Unplug the one Oscillator -> VCA cable the user can see: silence.
    for (const auto& c : t.audioCables(true))
        t.graph().removeConnection(c);
    const auto silent = rig.level();
    EXPECT_FLOAT_EQ(silent.left, 0.0f);
    EXPECT_FLOAT_EQ(silent.right, 0.0f);
}

TEST_F(ChannelFlowTest, CollapsedInstrumentTrackStopsWhenTheVcaOutputIsUnplugged) {
    Track t(*this, /*splitJacks=*/false);
    ASSERT_NE(t.vca, nullptr);
    ASSERT_NE(t.trackIn, nullptr);
    Rig rig(t.mc.getAudioEngine(), *t.trackIn);
    ASSERT_GT(rig.level().left, 0.1f);

    for (const auto& c : t.audioCables(false))
        t.graph().removeConnection(c);
    const auto silent = rig.level();
    EXPECT_FLOAT_EQ(silent.left, 0.0f);
    EXPECT_FLOAT_EQ(silent.right, 0.0f);
}

// Split jacks: each leg has its own cable, so unplugging one leaves exactly the other leg sounding.
// This is the way to hear Left/Right working: pull the VCA's Audio L (or R) output cable.
TEST_F(ChannelFlowTest, SplitInstrumentTrackPlaysOnlyTheLegThatIsStillPlugged) {
    Track t(*this, /*splitJacks=*/true);
    ASSERT_NE(t.vca, nullptr);
    ASSERT_NE(t.trackIn, nullptr);
    Rig rig(t.mc.getAudioEngine(), *t.trackIn);
    const auto both = rig.level();
    ASSERT_GT(both.left, 0.1f);
    ASSERT_GT(both.right, 0.1f);

    const auto outs = t.audioCables(false);
    ASSERT_EQ(outs.size(), 2u);
    for (const auto& c : outs)
        if (c.source.channelIndex == 0)
            t.graph().removeConnection(c);
    const auto rightOnly = rig.level();
    EXPECT_FLOAT_EQ(rightOnly.left, 0.0f);
    EXPECT_GT(rightOnly.right, 0.1f);
}
