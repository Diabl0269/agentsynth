// CalloutRevealTests.cpp (docs/layout/animation.md#popup-reveal): a colour picker opened in a CallOutBox eases in
// (a 160 ms fade and a short slide out of the element that opened it) and lands exactly on its final state; it lands at
// once under the OS's reduced-motion setting or when the callout is not on screen. The frame maths is pure; the
// landing rules drive a real CallOutBox hosted in a plain parent component (no native window).
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/CalloutReveal.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::CalloutReveal;
using synth::ui::ColourPickerPopup;

// Restores the real reduced-motion answer whatever a test did to it.
struct ReducedMotionGuard {
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// A callout hosting `popup` inside `parent`, the way juce::CallOutBox::launchAsynchronously builds one (minus the
// desktop window, which a headless test cannot have).
struct HostedPopup {
    juce::Component parent;
    ColourPickerPopup popup{juce::Colours::red, nullptr, {}, {}};
    std::unique_ptr<juce::CallOutBox> box;

    HostedPopup() {
        parent.setSize(600, 600);
        box = std::make_unique<juce::CallOutBox>(popup, juce::Rectangle<int>(280, 20, 20, 20), &parent);
    }
    ~HostedPopup() { box.reset(); }
};

void pumpMessages() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

} // namespace

TEST(CalloutRevealTest, TheFrameFadesInAndSlidesFromTheStartOffsetToExactlyZero) {
    const juce::Point<int> start(0, -CalloutReveal::kRisePx);
    const auto first = CalloutReveal::frameAt(0.0f, start);
    EXPECT_EQ(first.alpha, 0.0f);
    EXPECT_EQ(first.offset, start);

    const auto middle = CalloutReveal::frameAt(0.5f, start);
    EXPECT_GT(middle.alpha, 0.0f);
    EXPECT_LT(middle.alpha, 1.0f);
    EXPECT_EQ(middle.offset.y, -CalloutReveal::kRisePx / 2);

    const auto last = CalloutReveal::frameAt(1.0f, start);
    EXPECT_EQ(last.alpha, 1.0f);
    EXPECT_EQ(last.offset, juce::Point<int>(0, 0)) << "it lands exactly on its final bounds";
    EXPECT_EQ(CalloutReveal::frameAt(7.0f, start).alpha, 1.0f) << "progress is clamped";
    EXPECT_EQ(CalloutReveal::kInMs, 160.0) << "the small-reveal duration in docs/layout/animation.md";
}

TEST(CalloutRevealTest, ThePopupGrowsOutOfTheSideThatFacesTheElementThatOpenedIt) {
    // The callout's arrow side carries the largest inset, and that side faces the anchor.
    EXPECT_EQ(CalloutReveal::directionToAnchor({36, 20, 20, 20}), juce::Point<int>(0, -1)) << "anchor above";
    EXPECT_EQ(CalloutReveal::directionToAnchor({20, 20, 36, 20}), juce::Point<int>(0, 1)) << "anchor below";
    EXPECT_EQ(CalloutReveal::directionToAnchor({20, 36, 20, 20}), juce::Point<int>(-1, 0)) << "anchor to the left";
    EXPECT_EQ(CalloutReveal::directionToAnchor({20, 20, 20, 36}), juce::Point<int>(1, 0)) << "anchor to the right";
    EXPECT_EQ(CalloutReveal::directionToAnchor({20, 20, 20, 20}), juce::Point<int>(0, 0)) << "no arrow: no slide";
}

TEST(CalloutRevealTest, ACalloutThatIsNotOnScreenLandsAtFullOpacityWithoutAnimating) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(false);
    HostedPopup hosted;
    ASSERT_NE(hosted.box, nullptr);
    EXPECT_EQ(hosted.box->getAlpha(), 0.0f) << "hidden from the moment the popup is parented, so it never flashes";

    pumpMessages();
    EXPECT_EQ(hosted.box->getAlpha(), 1.0f) << "nothing is showing, so there is nothing to animate: it lands";
}

TEST(CalloutRevealTest, ReducedMotionLandsAtOnceWithNoFadeAtAll) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(true);
    HostedPopup hosted;
    EXPECT_EQ(hosted.box->getAlpha(), 1.0f) << "never hidden under reduced motion";
    pumpMessages();
    EXPECT_EQ(hosted.box->getAlpha(), 1.0f);
}

TEST(CalloutRevealTest, AFrameAppliedToTheCalloutMovesItsAlphaAndPositionAndTheLastOneLandsExactly) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(true); // keep the entrance out of the way: this drives the frames by hand
    HostedPopup hosted;
    const auto finalPosition = hosted.box->getPosition();

    // The popup's own reveal, through the seam: a half-way frame sits between hidden and shown.
    ASSERT_TRUE(hosted.popup.getRevealForTest().applyFrameForTest(0.5f));
    EXPECT_GT(hosted.box->getAlpha(), 0.0f);
    EXPECT_LT(hosted.box->getAlpha(), 1.0f);

    ASSERT_TRUE(hosted.popup.getRevealForTest().applyFrameForTest(1.0f));
    EXPECT_EQ(hosted.box->getAlpha(), 1.0f);
    EXPECT_EQ(hosted.box->getPosition(), finalPosition);
}

TEST(CalloutRevealTest, APopupNotInACalloutNeverTouchesAnything) {
    ColourPickerPopup popup(juce::Colours::red, nullptr, {}, {});
    EXPECT_FALSE(popup.getRevealForTest().applyFrameForTest(0.5f));
    EXPECT_FALSE(popup.getRevealForTest().isRunning());
}

TEST(CalloutRevealTest, TheOsReducedMotionAnswerCanBeForcedAndRestored) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(true);
    EXPECT_TRUE(synth::ui::prefersReducedMotion());
    synth::ui::setReducedMotionForTest(false);
    EXPECT_FALSE(synth::ui::prefersReducedMotion());
    synth::ui::setReducedMotionForTest(std::nullopt);
    (void)synth::ui::prefersReducedMotion(); // the real read must simply work on this platform
}
