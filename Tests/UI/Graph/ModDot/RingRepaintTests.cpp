// A modulation ring's frame re-rasters only the ring: the knob it rings keeps its own picture, so the card blits it
// instead of painting it again, and a ring that would draw the same repaints nothing.
// docs/modules/modulation.md#modulation-rings-on-knobs, docs/layout/rendering.md#gated-timers-not-free-running-ones.

#include "../CardBody/CardBodyTestHelpers.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/LFOModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

// The themed look, counting how many times a knob was painted.
struct CountingLnf : synth::theme::AppLookAndFeel {
    int knobPaints = 0;
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider& s) override {
        ++knobPaints;
        AppLookAndFeel::drawRotarySlider(g, x, y, w, h, pos, start, end, s);
    }
};

// A real LFO cabled to a VCA's Level knob, the graph processed offline.
struct KnobRing {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    CountingLnf lnf;
    ModuleComponent* card = nullptr;
    juce::Slider* knob = nullptr;
    synth::ui::ZoomFrozenCachedImage* cache = nullptr;
    juce::AudioProcessorGraph::NodeID lfoId, vcaId;
    int channel = -1;

    KnobRing() {
        engine.setAudioDeviceDisabled(true);
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        auto& graph = engine.getGraph();
        auto lfo = std::make_unique<LFOModule>();
        if (auto* mode = findParameterByID(lfo.get(), "mode"))
            mode->setValueNotifyingHost(0.0f);
        if (auto* rate = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(lfo.get(), "rateHz")))
            rate->setValueNotifyingHost(rate->convertTo0to1(1.0f));
        lfoId = graph.addNode(std::move(lfo))->nodeID;
        auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
        vcaId = vcaNode->nodeID;
        channel = dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->modulationChannelForParam("gain");
        synth::setCardLayoutOverride(
            graph, nullptr, vcaId,
            cardbody_test::automaticLayoutWith(*vcaNode->getProcessor(), {{"gain", synth::CardWidget::Knob}}));
        editor->updateComponents();
        editor->connectModulationSource(lfoId, 0, vcaId, channel, 1.0f);
        editor->updateComponents();
        editor->timerCallback();
        card = findModuleComp(*editor, vcaNode->getProcessor());
        if (card != nullptr) {
            card->setLookAndFeel(&lnf);
            knob = dynamic_cast<juce::Slider*>(card->getCardBody()->findWidget("gain"));
            cache = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(card->getCachedComponentImage());
        }
        graph.setPlayConfigDetails(0, 2, kRate, kBlock);
        graph.prepareToPlay(kRate, kBlock);
    }
    ~KnobRing() {
        if (card != nullptr)
            card->setLookAndFeel(nullptr);
        editor.reset();
        engine.shutdown();
    }

    // A second LFO on the same knob, and both LFOs held still (muted: a steady 0).
    void addSecondLfoAndMuteBoth() {
        auto& graph = engine.getGraph();
        const auto second = graph.addNode(std::make_unique<LFOModule>())->nodeID;
        editor->updateComponents();
        editor->connectModulationSource(second, 0, vcaId, channel, 1.0f);
        editor->updateComponents();
        for (const auto id : {lfoId, second})
            dynamic_cast<ModuleBase*>(graph.getNodeForId(id)->getProcessor())->setMuted(true);
        card = findModuleComp(*editor, graph.getNodeForId(vcaId)->getProcessor());
    }

    // Runs the graph `blocks` blocks, then lets the card see the new reading.
    void process(int blocks) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        for (int i = 0; i < blocks; ++i) {
            buffer.clear();
            engine.getGraph().processBlock(buffer, midi);
        }
        editor->timerCallback();
        card->timerCallback();
    }

    void paintCache() {
        juce::Image target(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(target);
        cache->paint(g);
    }

    juce::Image paintCard() {
        juce::Image image(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        card->paintEntireComponent(g, true);
        return image;
    }
};

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
