// A knob's modulation ring fades in with its first modulation and out with its last removal instead of popping:
// the fade is stepped by hand (the card is not on screen) and the alpha the card would paint with is sampled
// part-way; and a ring whose amount changes under a still source repaints on the next tick.
// docs/layout/mod-ring-fade.md.

#include "ModDotTestFixture.h"

#include "UI/Layout/FadeAmount.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include <gtest/gtest.h>

namespace {

struct AnimateOffScreen {
    AnimateOffScreen() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
        synth::ui::setReducedMotionForTest(false);
    }
    ~AnimateOffScreen() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

// The alpha of the ring the card would paint on the gain knob now (0: none drawn).
float paintedAlpha(Fixture& f) {
    float alpha = 0.0f;
    for (const auto& ring : f.vcaCard->getModRingFades().rings(f.editor->getCachedModDisplayInfo(), f.vcaId))
        if (ring.info.destChannelIndex == f.gainChannel)
            alpha = std::max(alpha, ring.alpha);
    return alpha;
}

// The card's own 15 Hz tick, which follows the editor's routing cache.
void tickCard(Fixture& f) {
    f.editor->timerCallback();
    f.vcaCard->timerCallback();
}

} // namespace

// Regression test for FRO795: the ring around a modulated knob popped in and out.
TEST_F(ModuleComponentTest, AKnobsRingFadesInWithItsFirstModulation) {
    AnimateOffScreen animate;
    Fixture f(/*modulated*/ false);
    tickCard(f); // the card has met the (empty) patch
    EXPECT_FLOAT_EQ(paintedAlpha(f), 0.0f);

    f.engine.addModRouting(f.lfoId, 0, f.vcaId, f.gainChannel);
    tickCard(f);

    EXPECT_FLOAT_EQ(paintedAlpha(f), 0.0f) << "frame 0 of the fade is invisible, not a full ring";
    synth::ui::FadeAmount::stepAllForTest(0.5f);
    const float half = paintedAlpha(f);
    EXPECT_GT(half, 0.2f);
    EXPECT_LT(half, 0.8f);
    synth::ui::FadeAmount::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(paintedAlpha(f), 1.0f);
}

TEST_F(ModuleComponentTest, AKnobsRingFadesOutWithItsLastModulationAndIsGoneAfterwards) {
    AnimateOffScreen animate;
    Fixture f;
    tickCard(f);
    ASSERT_FLOAT_EQ(paintedAlpha(f), 1.0f) << "a patch that opens modulated shows its rings at once";

    f.engine.removeModRouting(f.attenId);
    tickCard(f);

    EXPECT_TRUE(f.editor->getCachedModDisplayInfo().empty()) << "the routing is already gone from the cache";
    EXPECT_FLOAT_EQ(paintedAlpha(f), 1.0f) << "the ring is still there on frame 0 of its fade";
    synth::ui::FadeAmount::stepAllForTest(0.5f);
    const float half = paintedAlpha(f);
    EXPECT_GT(half, 0.2f) << "the ring is drawn from the routing it last saw";
    EXPECT_LT(half, 0.8f);
    synth::ui::FadeAmount::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(paintedAlpha(f), 0.0f);
    tickCard(f);
    EXPECT_FLOAT_EQ(f.vcaCard->getModRingFades().alphaFor(f.gainChannel), 0.0f);
}

TEST_F(ModuleComponentTest, ASecondModulationOnARingedKnobDoesNotFadeAndRemovingOneKeepsTheRing) {
    AnimateOffScreen animate;
    Fixture f;
    tickCard(f);
    const auto second = f.addSecondLfo();
    tickCard(f);
    EXPECT_FLOAT_EQ(paintedAlpha(f), 1.0f) << "the knob already had a ring";

    f.engine.removeModRouting(second);
    tickCard(f);
    synth::ui::FadeAmount::stepAllForTest(0.5f);
    EXPECT_FLOAT_EQ(paintedAlpha(f), 1.0f) << "the other source still modulates it";
}

TEST_F(ModuleComponentTest, ACardThatIsNotOnScreenLandsItsRingAtOnce) {
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
    Fixture f(/*modulated*/ false);
    tickCard(f);
    const auto atten = f.engine.addModRouting(f.lfoId, 0, f.vcaId, f.gainChannel);
    tickCard(f);
    EXPECT_FLOAT_EQ(paintedAlpha(f), 1.0f) << "a card that is not on screen does not animate";
    f.engine.removeModRouting(atten);
    tickCard(f);
    EXPECT_FLOAT_EQ(paintedAlpha(f), 0.0f);
}

// The card's whole cache painted once, so its stale area counts only what a later repaint invalidates.
void paintCardCache(ModuleComponent& card, synth::ui::ZoomFrozenCachedImage& cache) {
    juce::Image target(juce::Image::ARGB, card.getWidth(), card.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(target);
    cache.paint(g);
}

// Regression test for FRO1861: a button macro that is off sends a still 0, so dragging the amount on its cable moved
// the knob's depth band only once the mouse was let go. Each amount step now repaints the knob (only the knob) on the
// next tick, whatever the source does.
TEST_F(ModuleComponentTest, AnAmountChangeUnderAStillSourceRepaintsTheRingOnTheNextTick) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    // A muted LFO sends a still 0, whether or not the machine's audio device runs the graph.
    dynamic_cast<ModuleBase*>(f.engine.getGraph().getNodeForId(f.lfoId)->getProcessor())->setMuted(true);
    ASSERT_TRUE(synth::test::waitUntil([&] {
        tickCard(f);
        const auto& info = f.editor->getCachedModDisplayInfo();
        return info.size() == 1u && std::abs(info.front().modSignalValue) <= 0.001f;
    })) << "a still source";
    auto* cache = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(f.vcaCard->getCachedComponentImage());
    ASSERT_NE(cache, nullptr);
    paintCardCache(*f.vcaCard, *cache);
    int repaints = cache->getLiveRepaintCountForTest();
    tickCard(f);
    EXPECT_EQ(cache->getLiveRepaintCountForTest(), repaints) << "nothing changed, so nothing repaints";

    const auto knob = f.gainKnob->getBounds().expanded(8);
    f.editor->beginModAmountGesture();
    for (const float target : {0.8f, 0.3f, -0.4f}) {
        f.editor->adjustModAmount(f.attenId, target - f.amount(f.attenId)); // the cable midpoint drag's own step
        paintCardCache(*f.vcaCard, *cache); // what the step's own repaint drew, from the routing cache before the tick
        repaints = cache->getLiveRepaintCountForTest();
        tickCard(f);
        EXPECT_FLOAT_EQ(f.editor->getCachedModDisplayInfo().front().amount, target);
        EXPECT_GT(cache->getLiveRepaintCountForTest(), repaints)
            << "amount " << target << ": the ring stayed put until the mouse was let go";
        const auto area = synth::ui::ZoomFrozenCachedImage::lastRepaint().area;
        EXPECT_TRUE(knob.contains(area)) << "only the driven knob repaints, never the card";
    }
    f.editor->commitModAmountGesture();

    paintCardCache(*f.vcaCard, *cache);
    repaints = cache->getLiveRepaintCountForTest();
    tickCard(f);
    EXPECT_EQ(cache->getLiveRepaintCountForTest(), repaints)
        << "once the amount rests, the tick repaints nothing again";
}
