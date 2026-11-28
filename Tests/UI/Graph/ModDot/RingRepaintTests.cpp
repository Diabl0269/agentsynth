// A modulation ring's frame re-rasters only the ring: the knob it rings keeps its own picture, so the card blits it
// instead of painting it again, and a ring that would draw the same repaints nothing.
// docs/modules/modulation.md#modulation-rings-on-knobs, docs/layout/rendering.md#gated-timers-not-free-running-ones.
// The ring frames (ModRingPump): while a routing moves, a frame refreshes the live values and repaints each moving
// ring; a still routing repaints nothing; the frames stop after 30 quiet frames and the 30 Hz tick starts them again.
// Frames are driven by hand (a headless component gets no display refresh).

#include "KnobRingFixture.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/GraphEditor/ModRingPump.h"
#include <gtest/gtest.h>

namespace {

int maxLevelDifference(const juce::Image& a, const juce::Image& b) {
    int worst = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x) {
            const auto p = a.getPixelAt(x, y);
            const auto q = b.getPixelAt(x, y);
            worst = std::max({worst, std::abs(p.getRed() - q.getRed()), std::abs(p.getGreen() - q.getGreen()),
                              std::abs(p.getBlue() - q.getBlue()), std::abs(p.getAlpha() - q.getAlpha())});
        }
    return worst;
}

} // namespace

TEST(RingRepaint, AKnobWithARingKeepsItsOwnPictureAndLosesItWhenTheRingGoes) {
    KnobRing f;
    ASSERT_NE(f.knob, nullptr);
    f.process(20);
    EXPECT_NE(f.knob->getCachedComponentImage(), nullptr) << "ringed: the knob is a picture of its own";
    f.engine.removeModRouting(f.editor->getCachedModDisplayInfo().front().attenuverterNodeID);
    f.process(1); // a card that is not on screen lands its fade at once
    EXPECT_EQ(f.knob->getCachedComponentImage(), nullptr) << "the last ring has faded out: the knob paints as before";
}

TEST(RingRepaint, ACardPaintedWithTheBufferedKnobLooksLikeOneWithout) {
    KnobRing f;
    ASSERT_NE(f.knob, nullptr);
    f.process(20);
    ASSERT_NE(f.knob->getCachedComponentImage(), nullptr);
    const auto buffered = f.paintCard();
    f.knob->setBufferedToImage(false);
    const auto plain = f.paintCard();
    f.knob->setBufferedToImage(true);
    EXPECT_LE(maxLevelDifference(buffered, plain), 2);
}

TEST(RingRepaint, ARingStepRerastersOnlyTheRingAndDoesNotPaintTheKnob) {
    KnobRing f;
    ASSERT_NE(f.knob, nullptr);
    ASSERT_NE(f.cache, nullptr);
    f.process(20);
    f.paintCache();
    ASSERT_EQ(f.cache->getStaleAreaForTest(), 0);
    const int paintedBefore = f.lnf.knobPaints;

    f.process(21); // the LFO has moved
    const int stale = f.cache->getStaleAreaForTest();
    const auto bounds = f.knob->getBounds();
    EXPECT_GT(stale, 0) << "the ring is repainted";
    EXPECT_LT(stale, bounds.getWidth() * bounds.getHeight()) << "a ring's box is smaller than its knob";

    f.paintCache();
    EXPECT_EQ(f.lnf.knobPaints, paintedBefore) << "the card blitted the knob's picture";

    f.knob->repaint(); // the knob's own change paints it again
    f.paintCache();
    EXPECT_GT(f.lnf.knobPaints, paintedBefore);
}

TEST(RingRepaint, AStillRingRepaintsNothing) {
    KnobRing f;
    ASSERT_NE(f.knob, nullptr);
    f.process(20);
    f.paintCache();
    ASSERT_EQ(f.cache->getStaleAreaForTest(), 0);
    // No audio runs, so the reading is the one the card has already drawn.
    EXPECT_FALSE(f.card->tickModulationRings());
    EXPECT_FALSE(f.card->tickModulationRings());
    EXPECT_EQ(f.cache->getStaleAreaForTest(), 0);
}

// Regression test for the two-sources case: each routing was compared with the other's drawn state, so a knob with
// two still sources repainted on every tick.
TEST(RingRepaint, TwoStillSourcesOnOneKnobRepaintNothing) {
    KnobRing f;
    ASSERT_NE(f.knob, nullptr);
    f.addSecondLfoAndMuteBoth();
    ASSERT_NE(f.card, nullptr);
    f.cache = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(f.card->getCachedComponentImage());
    f.process(30);
    f.process(2);
    f.paintCache();
    ASSERT_EQ(f.cache->getStaleAreaForTest(), 0);
    EXPECT_FALSE(f.card->tickModulationRings());
    f.process(3);
    EXPECT_EQ(f.cache->getStaleAreaForTest(), 0);
}

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
