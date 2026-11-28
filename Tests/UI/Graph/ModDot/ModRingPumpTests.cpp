// The modulation rings follow the display: while a routing moves, a frame refreshes the live values and repaints each
// moving ring; a still routing repaints nothing; the frames stop after 30 quiet frames and the 30 Hz tick starts them
// again. Frames are driven by hand (a headless component gets no display refresh).
// docs/layout/animation.md#the-time-bounded-animation-rule, docs/modules/modulation.md#modulation-rings-on-knobs.

#include "KnobRingFixture.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/GraphEditor/ModRingPump.h"
#include <gtest/gtest.h>

namespace {

// One audio block, then one display frame (the card is not on screen, so the editor gets a size that holds it).
struct PumpFixture : KnobRing {
    synth::ui::ModRingPump& pump;
    PumpFixture()
        : pump(editor->getModRingPump()) {
        editor->setSize(4000, 4000);
        process(20);
        paintCache();
    }
    void block() {
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        engine.getGraph().processBlock(buffer, midi);
    }
};

} // namespace

TEST(ModRingPump, AMovingRoutingRepaintsItsRingOnEveryFrame) {
    PumpFixture f;
    ASSERT_NE(f.cache, nullptr);
    f.pump.runFrameForTest(); // wakes nothing by itself; the first frame settles the readings
    for (int frame = 0; frame < 10; ++frame) {
        f.paintCache();
        ASSERT_EQ(f.cache->getStaleAreaForTest(), 0);
        f.block();
        f.pump.runFrameForTest();
        EXPECT_GT(f.cache->getStaleAreaForTest(), 0) << "frame " << frame << ": the ring moved";
    }
}

TEST(ModRingPump, AStillRoutingRepaintsNothingOnAFrame) {
    PumpFixture f;
    dynamic_cast<ModuleBase*>(f.engine.getGraph().getNodeForId(f.lfoId)->getProcessor())->setMuted(true);
    for (int i = 0; i < 4; ++i)
        f.block();
    f.pump.runFrameForTest();
    f.pump.runFrameForTest();
    f.paintCache();
    for (int frame = 0; frame < 5; ++frame) {
        f.block();
        f.pump.runFrameForTest();
        EXPECT_EQ(f.cache->getStaleAreaForTest(), 0) << "frame " << frame;
    }
}

TEST(ModRingPump, TheFramesStopAfterThirtyQuietFramesAndTheTickStartsThemAgain) {
    PumpFixture f;
    auto* lfo = dynamic_cast<ModuleBase*>(f.engine.getGraph().getNodeForId(f.lfoId)->getProcessor());
    f.editor->timerCallback();
    EXPECT_TRUE(f.pump.running()) << "a moving routing starts the frames";

    lfo->setMuted(true);
    for (int i = 0; i < 4; ++i)
        f.block();
    f.pump.runFrameForTest(); // the last movement settles
    for (int frame = 0; frame < synth::ui::ModRingPump::kQuietFrames - 1; ++frame)
        f.pump.runFrameForTest();
    EXPECT_TRUE(f.pump.running()) << "29 quiet frames are not enough";
    f.pump.runFrameForTest();
    EXPECT_FALSE(f.pump.running());

    f.editor->timerCallback();
    EXPECT_FALSE(f.pump.running()) << "a still routing does not restart the frames";
    lfo->setMuted(false);
    for (int i = 0; i < 8; ++i)
        f.block();
    f.editor->timerCallback();
    EXPECT_TRUE(f.pump.running()) << "the 30 Hz tick saw it move again";
}

TEST(ModRingPump, TheFrameRateReadsTheFramesOfTheLastSecond) {
    PumpFixture f;
    double now = 10000.0;
    f.pump.setClockForTest([&now] { return now; });
    for (int frame = 0; frame < 60; ++frame) {
        now += 1000.0 / 60.0;
        f.pump.runFrameForTest();
    }
    EXPECT_NEAR(f.pump.framesPerSecond(), 60, 1);
    now += 2000.0;
    EXPECT_EQ(f.pump.framesPerSecond(), 0);
}

// What a frame reads is what the full rebuild reads: an LFO on a Filter's cutoff, through an attenuverter and direct.
TEST(ModRingPump, RefreshingTheValuesEqualsTheFullRebuildAfterTheEngineRendersABlock) {
    AudioEngine engine;
    engine.setAudioDeviceDisabled(true);
    engine.initialise();
    engine.getGraph().clear();
    auto& graph = engine.getGraph();
    const auto lfo = graph.addNode(std::make_unique<LFOModule>())->nodeID;
    const auto direct = graph.addNode(std::make_unique<LFOModule>())->nodeID;
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto* filter = dynamic_cast<ModuleBase*>(filterNode->getProcessor());
    const int cutoff = filter->modulationChannelForParam("cutoff");
    const int resonance = filter->modulationChannelForParam("resonance");
    engine.addModRouting(lfo, 0, filterNode->nodeID, cutoff);
    graph.addConnection({{direct, 0}, {filterNode->nodeID, resonance}});
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    graph.prepareToPlay(44100.0, 512);

    auto stale = engine.getModulationDisplayInfo(); // before any audio
    ASSERT_GE(stale.size(), 2u);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 12; ++i) {
        buffer.clear();
        graph.processBlock(buffer, midi);
    }
    const auto fresh = engine.getModulationDisplayInfo();
    engine.refreshModulationDisplayValues(stale);
    ASSERT_EQ(stale.size(), fresh.size());
    bool anyMoved = false;
    for (size_t i = 0; i < fresh.size(); ++i) {
        EXPECT_EQ(stale[i].modSignalValue, fresh[i].modSignalValue) << i;
        EXPECT_EQ(stale[i].modSignalPeak, fresh[i].modSignalPeak) << i;
        anyMoved = anyMoved || std::abs(fresh[i].modSignalValue) > 0.0f;
    }
    EXPECT_TRUE(anyMoved) << "the LFO drove something";
    engine.shutdown();
}
