// Stereo pair into a mono-only input averages at -6 dB instead of the graph's unity sum
// (docs/architecture/audio-engine.md#stereo-down-mix-fro326). Every test drives a real AudioEngine
// graph through the Standalone device callback, the same render path NormallingTests.cpp uses, and
// calls AudioEngine::refreshNormalling() by hand after each connection change -- the scan rides that
// function, so it shares its three real triggers (publishTimeline, the plugin's setStateInformation,
// and the graph's own change broadcast). The constant-level doubles below make the sums exact.
//
// Headless house rules as everywhere else: no real audio device, no sleeps.

#include "../FakeAudioIODevice.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

using synth::test::FakeAudioIODevice;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate;
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;

// A stereo source: Left on raw 0, Right on raw 1 (the FX layout), each a constant level.
class ConstStereoSource : public ModuleBase {
public:
    ConstStereoSource(float left, float right)
        : ModuleBase("ConstStereo", 0, 2)
        , left_(left)
        , right_(right) {}
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::FloatVectorOperations::fill(buffer.getWritePointer(0), left_, buffer.getNumSamples());
        juce::FloatVectorOperations::fill(buffer.getWritePointer(1), right_, buffer.getNumSamples());
    }
    int rightAudioLegChannel() const override { return 1; }
    ModuleType getModuleType() const override { return ModuleType::Oscillator; }

private:
    float left_, right_;
};

// A plain mono source (no second leg), constant level.
class ConstMonoSource : public ModuleBase {
public:
    explicit ConstMonoSource(float level)
        : ModuleBase("ConstMono", 0, 1)
        , level_(level) {}
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::FloatVectorOperations::fill(buffer.getWritePointer(0), level_, buffer.getNumSamples());
    }
    ModuleType getModuleType() const override { return ModuleType::Oscillator; }

private:
    float level_;
};

// A mono-only input that passes what it receives straight through (in place), so the level the
// module saw reaches Audio Output unchanged.
class MonoPassThrough : public ModuleBase {
public:
    MonoPassThrough()
        : ModuleBase("MonoThrough", 1, 1) {}
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::VCA; }
};

// A Dual I/O-style stereo input pair that opts into L/Mono normalling.
class StereoPairInput : public ModuleBase {
public:
    StereoPairInput()
        : ModuleBase("PairIn", 2, 2, StereoAudio::None) {}
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        applyLeftRightNormalling(buffer);
    }
    int rightAudioLegChannel() const override { return 1; }
    ModuleType getModuleType() const override { return ModuleType::VCA; }
};

float renderLeftSample(AudioEngine& engine) {
    FakeAudioIODevice fake(0, 2);
    engine.audioDeviceAboutToStart(&fake);
    std::vector<float> left(static_cast<std::size_t>(kBlockSize), -9999.0f);
    std::vector<float> right(static_cast<std::size_t>(kBlockSize), -9999.0f);
    float* outs[] = {left.data(), right.data()};
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, kBlockSize, {});
    engine.audioDeviceStopped();
    return left[static_cast<std::size_t>(kBlockSize / 2)];
}

struct Rig {
    AudioEngine engine{AudioEngine::HostMode::Standalone};
    juce::AudioProcessorGraph& graph = engine.getGraph();
    juce::AudioProcessorGraph::Node::Ptr sink;
    juce::AudioProcessorGraph::Node::Ptr out;

    Rig() {
        graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        sink = graph.addNode(std::make_unique<MonoPassThrough>());
        out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
        graph.addConnection({{sink->nodeID, 0}, {out->nodeID, 0}});
    }
    ModuleBase& sinkModule() { return *dynamic_cast<ModuleBase*>(sink->getProcessor()); }
    bool connect(juce::AudioProcessorGraph::Node::Ptr from, int channel) {
        return graph.addConnection({{from->nodeID, channel}, {sink->nodeID, 0}});
    }
};

} // namespace

TEST(StereoDownMixTest, StereoPairIntoMonoInputAveragesInsteadOfSumming) {
    Rig rig;
    auto source = rig.graph.addNode(std::make_unique<ConstStereoSource>(0.4f, 0.2f));
    ASSERT_TRUE(rig.connect(source, 0));
    ASSERT_TRUE(rig.connect(source, 1)) << "Left AND Right of one source onto the same mono jack";

    rig.engine.refreshNormalling();
    EXPECT_EQ(rig.sinkModule().getInputDownMixMask(), 1u);
    EXPECT_NEAR(renderLeftSample(rig.engine), 0.3f, 1e-6f) << "0.5 * (0.4 + 0.2), not the unity sum 0.6";
}

TEST(StereoDownMixTest, ASingleLegStaysAtUnity) {
    Rig rig;
    auto source = rig.graph.addNode(std::make_unique<ConstStereoSource>(0.4f, 0.2f));
    ASSERT_TRUE(rig.connect(source, 0));

    rig.engine.refreshNormalling();
    EXPECT_EQ(rig.sinkModule().getInputDownMixMask(), 0u);
    EXPECT_NEAR(renderLeftSample(rig.engine), 0.4f, 1e-6f);
}

TEST(StereoDownMixTest, AHandBuiltMixWithAnUnpairedFeedStaysAtUnity) {
    Rig rig;
    auto stereo = rig.graph.addNode(std::make_unique<ConstStereoSource>(0.4f, 0.2f));
    auto mono = rig.graph.addNode(std::make_unique<ConstMonoSource>(0.1f));
    ASSERT_TRUE(rig.connect(stereo, 0));
    ASSERT_TRUE(rig.connect(stereo, 1));
    ASSERT_TRUE(rig.connect(mono, 0)) << "a third, unpaired cable: the user is mixing by hand";

    rig.engine.refreshNormalling();
    EXPECT_EQ(rig.sinkModule().getInputDownMixMask(), 0u);
    EXPECT_NEAR(renderLeftSample(rig.engine), 0.7f, 1e-6f) << "every feed at unity, as before";
}

TEST(StereoDownMixTest, LegsFromTwoDifferentSourcesAreNotAPair) {
    Rig rig;
    auto a = rig.graph.addNode(std::make_unique<ConstStereoSource>(0.4f, 0.2f));
    auto b = rig.graph.addNode(std::make_unique<ConstStereoSource>(0.3f, 0.1f));
    ASSERT_TRUE(rig.connect(a, 0));
    ASSERT_TRUE(rig.connect(b, 1));

    rig.engine.refreshNormalling();
    EXPECT_EQ(rig.sinkModule().getInputDownMixMask(), 0u);
    EXPECT_NEAR(renderLeftSample(rig.engine), 0.5f, 1e-6f);
}

TEST(StereoDownMixTest, TwoStereoPairsIntoOneInputAreEachAveraged) {
    Rig rig;
    auto a = rig.graph.addNode(std::make_unique<ConstStereoSource>(0.4f, 0.2f));
    auto b = rig.graph.addNode(std::make_unique<ConstStereoSource>(0.3f, 0.1f));
    for (auto node : {a, b}) {
        ASSERT_TRUE(rig.connect(node, 0));
        ASSERT_TRUE(rig.connect(node, 1));
    }

    rig.engine.refreshNormalling();
    EXPECT_NEAR(renderLeftSample(rig.engine), 0.3f + 0.2f, 1e-6f);
}

TEST(StereoDownMixTest, UnpluggingRightReturnsToUnity) {
    Rig rig;
    auto source = rig.graph.addNode(std::make_unique<ConstStereoSource>(0.4f, 0.2f));
    ASSERT_TRUE(rig.connect(source, 0));
    ASSERT_TRUE(rig.connect(source, 1));
    rig.engine.refreshNormalling();
    ASSERT_EQ(rig.sinkModule().getInputDownMixMask(), 1u);

    ASSERT_TRUE(rig.graph.removeConnection({{source->nodeID, 1}, {rig.sink->nodeID, 0}}));
    rig.engine.refreshNormalling();
    EXPECT_EQ(rig.sinkModule().getInputDownMixMask(), 0u);
    EXPECT_NEAR(renderLeftSample(rig.engine), 0.4f, 1e-6f);
}

// A plain cable drag never reaches publishTimeline; the graph's own change broadcast must pick the
// pair up (AudioEngine::changeListenerCallback -> refreshNormalling), as it does for normalling.
TEST(StereoDownMixTest, CanvasStyleCableEditIsPickedUpByTheGraphBroadcast) {
    Rig rig;
    auto source = rig.graph.addNode(std::make_unique<ConstStereoSource>(0.4f, 0.2f));
    ASSERT_TRUE(rig.connect(source, 0));
    ASSERT_TRUE(rig.connect(source, 1));

    for (int i = 0; i < 100 && rig.sinkModule().getInputDownMixMask() != 1u; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_EQ(rig.sinkModule().getInputDownMixMask(), 1u);
}

// The down-mix runs before the module's own processBlock body, so a Dual I/O Left fed by a pair is
// halved BEFORE L/Mono normalling copies it onto the unpatched Right.
TEST(StereoDownMixTest, AStereoPairIntoADualLeftIsHalvedBeforeRightBorrowsIt) {
    StereoPairInput module;
    module.setInputDownMixMask(1u);
    module.setNormalLeftToRight(true);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();
    juce::FloatVectorOperations::fill(buffer.getWritePointer(0), 0.6f, kBlockSize); // L+R summed by the graph
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    EXPECT_NEAR(buffer.getSample(0, 0), 0.3f, 1e-6f);
    EXPECT_NEAR(buffer.getSample(1, 0), 0.3f, 1e-6f) << "Right borrows the averaged Left, not the raw sum";
}

// The ticket's own check with a real module: a centred Dual I/O Oscillator's Left and Right both
// cabled into one mono input play at the level of one leg, not double.
TEST(StereoDownMixTest, RealDualOscillatorPairMatchesOneLegsLevel) {
    const auto render = [](bool bothLegs) {
        Rig rig;
        auto osc = rig.graph.addNode(std::make_unique<OscillatorModule>());
        for (auto* p : osc->getProcessor()->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p);
                withId && withId->paramID == "dualIO")
                p->setValueNotifyingHost(1.0f);
        auto* mb = dynamic_cast<ModuleBase*>(osc->getProcessor());
        EXPECT_TRUE(rig.connect(osc, 0));
        if (bothLegs)
            EXPECT_TRUE(rig.connect(osc, mb->rightAudioLegChannel()));
        rig.engine.refreshNormalling();
        return renderLeftSample(rig.engine);
    };

    const float oneLeg = render(false);
    ASSERT_GT(std::abs(oneLeg), 1e-4f) << "sanity: the oscillator is audible mid-block";
    EXPECT_NEAR(render(true), oneLeg, 1e-5f) << "average of two equal legs == one leg, not +6 dB";
}
