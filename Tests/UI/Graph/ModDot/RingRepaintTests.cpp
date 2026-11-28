// A modulation ring's frame re-rasters only the ring: the knob it rings keeps its own picture, so the card blits it
// instead of painting it again, and a ring that would draw the same repaints nothing.
// docs/modules/modulation.md#modulation-rings-on-knobs, docs/layout/rendering.md#gated-timers-not-free-running-ones.

#include "KnobRingFixture.h"
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
