// The no-pop check's classification: a change that shows up partway first is an animation, one that is complete in the
// first frame that shows it is a pop.

#include "TestMode/TestModePopWatch.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::testmode::classifyPop;

namespace {

constexpr int kW = 100;
constexpr int kH = 40;

juce::Image blank() {
    juce::Image image(juce::Image::ARGB, kW, kH, true, juce::SoftwareImageType());
    juce::Graphics(image).fillAll(juce::Colours::black);
    return image;
}

// A black picture with a white bar `width` pixels wide from x 10.
juce::Image withBar(int width, int x = 10) {
    auto image = blank();
    juce::Graphics g(image);
    g.setColour(juce::Colours::white);
    g.fillRect(x, 10, width, 20);
    return image;
}

} // namespace

TEST(TestModePopWatch, AFadeShowsPartOfTheChangeInItsFirstFrame) {
    const auto v = classifyPop(blank(), {withBar(20), withBar(60), withBar(80)}, withBar(80), {});
    EXPECT_TRUE(v.changed);
    EXPECT_FALSE(v.pop);
    EXPECT_EQ(v.firstFrame, 0);
    EXPECT_EQ(v.regionPixels, 80 * 20);
    EXPECT_EQ(v.region, juce::Rectangle<int>(10, 10, 80, 20));
    EXPECT_EQ(v.intermediateFrames, 2);
}

TEST(TestModePopWatch, ASlideShowsTheOldPlaceBeforeTheNewOne) {
    // The bar moves from x 10 to x 60: the middle frame has it half way.
    const auto v =
        classifyPop(withBar(20, 10), {withBar(20, 30), withBar(20, 45), withBar(20, 60)}, withBar(20, 60), {});
    EXPECT_TRUE(v.changed);
    EXPECT_FALSE(v.pop);
}

TEST(TestModePopWatch, AFirstChangedFrameThatAlreadyEqualsTheFinalOneIsAPop) {
    const auto v = classifyPop(blank(), {blank(), withBar(80), withBar(80)}, withBar(80), {});
    EXPECT_TRUE(v.changed);
    EXPECT_TRUE(v.pop);
    EXPECT_EQ(v.firstFrame, 1);
    EXPECT_EQ(v.intermediateFrames, 0);
}

TEST(TestModePopWatch, AChangeNoCapturedFrameShowsIsAPop) {
    const auto v = classifyPop(blank(), {blank()}, withBar(80), {});
    EXPECT_TRUE(v.changed);
    EXPECT_TRUE(v.pop);
    EXPECT_EQ(v.firstFrame, -1);
}

TEST(TestModePopWatch, NoChangeIsNotAPop) {
    const auto v = classifyPop(withBar(30), {withBar(30)}, withBar(30), {});
    EXPECT_FALSE(v.changed);
    EXPECT_FALSE(v.pop);
    EXPECT_EQ(v.regionPixels, 0);
}

TEST(TestModePopWatch, AChangeInsideAnIgnoredAreaDoesNotCount) {
    juce::RectangleList<int> ignore;
    ignore.add({0, 0, kW, kH});
    EXPECT_FALSE(classifyPop(blank(), {withBar(80)}, withBar(80), ignore).changed);
    ignore.clear();
    ignore.add({0, 0, 40, kH}); // the left part is ignored, the rest still pops
    const auto v = classifyPop(blank(), {withBar(80)}, withBar(80), ignore);
    EXPECT_TRUE(v.changed);
    EXPECT_EQ(v.region.getX(), 40);
}

TEST(TestModePopWatch, AFrameOfAnotherSizeIsSkipped) {
    juce::Image small(juce::Image::ARGB, 10, 10, true, juce::SoftwareImageType());
    const auto v = classifyPop(blank(), {small}, withBar(80), {});
    EXPECT_TRUE(v.pop);
    EXPECT_EQ(v.firstFrame, -1);
}
