#include "../Source/UI/ZoomFrozenCachedImage.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

// A minimal owner whose paint tree we can count re-runs of. Never added to a window — headless.
class PaintCountingComponent : public juce::Component {
public:
    void paint(juce::Graphics&) override { ++paintCount; }
    int paintCount = 0;
};

// Drives the cache's paint() with an accumulated transform scale of `scale`, simulating the
// zoomLevel * deviceScale a real canvas paint would present (see ZoomFrozenCachedImage.h's
// doc comment). The backing image is a plain (software-rendered) juce::Image, whose
// LowLevelGraphicsContext reports getPhysicalPixelScaleFactor() from the accumulated transform.
void paintAtScale(synth::ui::ZoomFrozenCachedImage& cache, float scale) {
    juce::Image target(juce::Image::ARGB, 512, 512, true);
    juce::Graphics g(target);
    g.addTransform(juce::AffineTransform::scale(scale));
    cache.paint(g);
}

} // namespace

class ZoomFrozenCachedImageTest : public ::testing::Test {};

// Pins that plain (non-frozen) buffering behaves exactly like
// juce::detail::StandardCachedComponentImage: every scale change reallocates and re-rasters.
TEST_F(ZoomFrozenCachedImageTest, MatchesStandardBehaviourWhenNotFrozen) {
    PaintCountingComponent owner;
    owner.setSize(100, 80);
    synth::ui::ZoomFrozenCachedImage cache(owner);

    paintAtScale(cache, 1.0f);
    EXPECT_EQ(cache.getImageBoundsForTest(), owner.getLocalBounds() * 1.0f);
    EXPECT_EQ(cache.getRasterCountForTest(), 1);
    EXPECT_EQ(owner.paintCount, 1);

    paintAtScale(cache, 1.5f);
    EXPECT_EQ(cache.getImageBoundsForTest(), owner.getLocalBounds() * 1.5f);
    EXPECT_EQ(cache.getRasterCountForTest(), 2);
    EXPECT_EQ(owner.paintCount, 2);
}

// The whole fix: once frozen, a scale change resamples the existing image instead of
// reallocating and re-running the owner's paint tree.
TEST_F(ZoomFrozenCachedImageTest, FrozenReusesTheImageAcrossScaleChanges) {
    PaintCountingComponent owner;
    owner.setSize(100, 80);
    synth::ui::ZoomFrozenCachedImage cache(owner);

    paintAtScale(cache, 1.0f);
    ASSERT_EQ(cache.getRasterCountForTest(), 1);
    const auto boundsAtFreeze = cache.getImageBoundsForTest();

    cache.setFrozen(true);
    ASSERT_TRUE(cache.isFrozen());

    const float scales[] = {0.4f, 0.55f, 0.7f,  0.85f, 1.0f,  1.1f, 1.2f, 1.3f, 1.4f, 1.5f,
                            1.6f, 1.7f,  1.75f, 1.8f,  1.85f, 1.9f, 0.5f, 0.6f, 0.9f, 1.9f};
    static_assert(sizeof(scales) / sizeof(scales[0]) == 20, "expected 20 sample scales");
    for (float s : scales) {
        paintAtScale(cache, s);
        EXPECT_EQ(cache.getImageBoundsForTest(), boundsAtFreeze) << "scale " << s;
        EXPECT_EQ(cache.getRasterCountForTest(), 1) << "scale " << s;
    }
    EXPECT_EQ(owner.paintCount, 1);
}

// Thawing drops the pinned image and renders exactly once, crisp, at the real scale — then
// stays put (no repeat raster) on a subsequent paint at that same scale.
TEST_F(ZoomFrozenCachedImageTest, ThawRerastersExactlyOnceAtTheNewScale) {
    PaintCountingComponent owner;
    owner.setSize(100, 80);
    synth::ui::ZoomFrozenCachedImage cache(owner);

    paintAtScale(cache, 1.0f);
    cache.setFrozen(true);
    for (float s : {0.5f, 1.3f, 1.9f})
        paintAtScale(cache, s);
    ASSERT_EQ(cache.getRasterCountForTest(), 1);

    cache.setFrozen(false);
    ASSERT_FALSE(cache.isFrozen());

    constexpr float finalScale = 1.9f;
    paintAtScale(cache, finalScale);
    EXPECT_EQ(cache.getRasterCountForTest(), 2);
    EXPECT_EQ(cache.getImageBoundsForTest(), owner.getLocalBounds() * finalScale);

    paintAtScale(cache, finalScale);
    EXPECT_EQ(cache.getRasterCountForTest(), 2) << "repainting at the same scale must not re-raster";
}

// A layout change (not a zoom scale change) must never be swallowed by the freeze.
TEST_F(ZoomFrozenCachedImageTest, ResizeStillReallocatesWhileFrozen) {
    PaintCountingComponent owner;
    owner.setSize(100, 80);
    synth::ui::ZoomFrozenCachedImage cache(owner);

    paintAtScale(cache, 1.0f);
    cache.setFrozen(true);
    ASSERT_EQ(cache.getRasterCountForTest(), 1);

    owner.setSize(200, 80); // w * 2
    paintAtScale(cache, 1.0f);

    EXPECT_EQ(cache.getRasterCountForTest(), 2);
    EXPECT_EQ(cache.getImageBoundsForTest(), owner.getLocalBounds() * 1.0f);
}

// The 15 Hz gated card timer (activity glow / step indicator) must keep working mid-gesture:
// invalidateAll() still forces a repaint into the pinned-size image.
TEST_F(ZoomFrozenCachedImageTest, InvalidateStillForcesARepaintWhileFrozen) {
    PaintCountingComponent owner;
    owner.setSize(100, 80);
    synth::ui::ZoomFrozenCachedImage cache(owner);

    paintAtScale(cache, 1.0f);
    cache.setFrozen(true);
    ASSERT_EQ(cache.getRasterCountForTest(), 1);
    const auto boundsWhileFrozen = cache.getImageBoundsForTest();

    EXPECT_TRUE(cache.invalidateAll());
    paintAtScale(cache, 1.4f); // a different scale request while frozen: still pinned
    EXPECT_EQ(cache.getRasterCountForTest(), 2);
    EXPECT_EQ(cache.getImageBoundsForTest(), boundsWhileFrozen);
}
