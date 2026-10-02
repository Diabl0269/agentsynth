// PopupMotionTests.cpp -- the shared soft appear/disappear of popup windows: the pure
// math (start offset, alpha over time, Reduce motion path, out target), the Reduce motion switch,
// and the look-and-feel hooks that hand windows to the engine. No real window is opened: every
// component here is peer-less, which is also the contract that keeps headless runs untouched.

#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

namespace {

using namespace synth::ui;
using namespace synth::ui::popup_motion;

constexpr const char* kAttached = "synthPopupMotion";

struct ReducedMotionOverrideGuard {
    explicit ReducedMotionOverrideGuard(std::optional<bool> v) { setReducedMotionForTest(v); }
    ~ReducedMotionOverrideGuard() { setReducedMotionForTest(std::nullopt); }
};

// ----------------------------------------------------------------------------------------------
// Durations and easing
// ----------------------------------------------------------------------------------------------

TEST(PopupMotionMath, DurationsAre160In110OutAnd80WhenReduced) {
    EXPECT_DOUBLE_EQ(durationMs(Phase::In, false), 160.0);
    EXPECT_DOUBLE_EQ(durationMs(Phase::Out, false), 110.0);
    EXPECT_DOUBLE_EQ(durationMs(Phase::In, true), 80.0);
    EXPECT_DOUBLE_EQ(durationMs(Phase::Out, true), 80.0);
}

TEST(PopupMotionMath, InEasesOutAndOutEasesIn) {
    EXPECT_FLOAT_EQ(ease(Phase::In, 0.5f), easeOutCubic(0.5f));
    EXPECT_FLOAT_EQ(ease(Phase::Out, 0.5f), easeInCubic(0.5f));
    EXPECT_GT(ease(Phase::In, 0.5f), 0.5f);  // arrives fast, settles
    EXPECT_LT(ease(Phase::Out, 0.5f), 0.5f); // leaves slowly, then goes
}

// ----------------------------------------------------------------------------------------------
// Frames
// ----------------------------------------------------------------------------------------------

TEST(PopupMotionFrame, InStartsTransparentFourPixelsTowardTheAnchorAndEndsAtRest) {
    const juce::Point<int> down(0, 1);
    const auto start = frameAt(Phase::In, 0.0f, down, false);
    EXPECT_FLOAT_EQ(start.alpha, 0.0f);
    EXPECT_FLOAT_EQ(start.offset.x, 0.0f);
    EXPECT_FLOAT_EQ(start.offset.y, -4.0f); // the anchor is above a window that slides down

    const auto end = frameAt(Phase::In, 1.0f, down, false);
    EXPECT_FLOAT_EQ(end.alpha, 1.0f);
    EXPECT_FLOAT_EQ(end.offset.y, 0.0f);
}

TEST(PopupMotionFrame, InAlphaAtTimeFollowsTheEaseOut) {
    const juce::Point<int> down(0, 1);
    const float t = 0.25f;
    const auto f = frameAt(Phase::In, ease(Phase::In, t), down, false);
    EXPECT_FLOAT_EQ(f.alpha, easeOutCubic(0.25f));
    EXPECT_FLOAT_EQ(f.offset.y, -4.0f * (1.0f - easeOutCubic(0.25f)));
}

TEST(PopupMotionFrame, OutFadesAndSlidesTwoPixelsBackTowardTheAnchor) {
    const juce::Point<int> down(0, 1);
    const auto start = frameAt(Phase::Out, 0.0f, down, false);
    EXPECT_FLOAT_EQ(start.alpha, 1.0f);
    EXPECT_FLOAT_EQ(start.offset.y, 0.0f);

    const auto end = frameAt(Phase::Out, 1.0f, down, false);
    EXPECT_FLOAT_EQ(end.alpha, 0.0f);
    EXPECT_FLOAT_EQ(end.offset.y, -2.0f); // back toward the anchor above it
}

TEST(PopupMotionFrame, ASubmenuOpeningRightSlidesInFromTheLeft) {
    const juce::Point<int> right(1, 0);
    const auto start = frameAt(Phase::In, 0.0f, right, false);
    EXPECT_FLOAT_EQ(start.offset.x, -4.0f);
    EXPECT_FLOAT_EQ(start.offset.y, 0.0f);
}

TEST(PopupMotionFrame, ReduceMotionIsAFadeOnlyInAndOut) {
    for (const juce::Point<int> dir : {juce::Point<int>(0, 1), juce::Point<int>(1, 0), juce::Point<int>(0, -1)}) {
        for (const float e : {0.0f, 0.3f, 1.0f}) {
            const auto in = frameAt(Phase::In, e, dir, true);
            EXPECT_FLOAT_EQ(in.alpha, e);
            EXPECT_FLOAT_EQ(in.offset.x, 0.0f);
            EXPECT_FLOAT_EQ(in.offset.y, 0.0f);

            const auto out = frameAt(Phase::Out, e, dir, true);
            EXPECT_FLOAT_EQ(out.alpha, 1.0f - e);
            EXPECT_FLOAT_EQ(out.offset.x, 0.0f);
            EXPECT_FLOAT_EQ(out.offset.y, 0.0f);
        }
    }
}

// ----------------------------------------------------------------------------------------------
// Which way a window slides
// ----------------------------------------------------------------------------------------------

TEST(PopupMotionDirection, AMenuOpenedAtThePointerSlidesDownAwayFromIt) {
    const juce::Rectangle<int> menu(100, 200, 160, 220);
    EXPECT_EQ(slideDirection(menu, {100, 200}), juce::Point<int>(0, 1)); // corner at the pointer
    EXPECT_EQ(slideDirection(menu, {100, 180}), juce::Point<int>(0, 1)); // pointer a little above
}

TEST(PopupMotionDirection, AMenuFlippedAboveItsAnchorSlidesUp) {
    const juce::Rectangle<int> menu(100, 100, 160, 120);
    EXPECT_EQ(slideDirection(menu, {120, 260}), juce::Point<int>(0, -1));
}

TEST(PopupMotionDirection, ASubmenuBesideItsParentRowSlidesSideways) {
    const juce::Rectangle<int> sub(300, 100, 160, 120);
    EXPECT_EQ(slideDirection(sub, {290, 120}), juce::Point<int>(1, 0));  // opened to the right
    EXPECT_EQ(slideDirection(sub, {470, 120}), juce::Point<int>(-1, 0)); // opened to the left
}

TEST(PopupMotionDirection, APointerInsideTheWindowFallsBackToDown) {
    EXPECT_EQ(slideDirection({0, 0, 100, 100}, {50, 50}), juce::Point<int>(0, 1));
}

// ----------------------------------------------------------------------------------------------
// Reduce motion switch
// ----------------------------------------------------------------------------------------------

TEST(ReducedMotion, TestOverrideWinsOverTheSystemSetting) {
    {
        ReducedMotionOverrideGuard on(true);
        EXPECT_TRUE(prefersReducedMotion());
    }
    {
        ReducedMotionOverrideGuard off(false);
        EXPECT_FALSE(prefersReducedMotion());
    }
    EXPECT_EQ(prefersReducedMotion(), detail::systemPrefersReducedMotion()); // override cleared
}

// ----------------------------------------------------------------------------------------------
// Hooks and the headless contract
// ----------------------------------------------------------------------------------------------

TEST(PopupMotionAttach, IsIdempotentAndLeavesAPeerlessWindowUntouched) {
    juce::Component window;
    window.setBounds(10, 20, 100, 80);
    PopupMotion::attach(window);
    PopupMotion::attach(window); // second call must not install a second listener
    EXPECT_TRUE(window.getProperties().contains(kAttached));

    window.setVisible(true); // no peer: nothing is animated
    EXPECT_FLOAT_EQ(window.getAlpha(), 1.0f);
    EXPECT_EQ(window.getBounds(), juce::Rectangle<int>(10, 20, 100, 80));
    window.setVisible(false);
    EXPECT_FLOAT_EQ(window.getAlpha(), 1.0f);
}

TEST(PopupMotionAttach, AWindowDeletedWhileVisibleAndPeerlessIsDeletedCleanly) {
    auto window = std::make_unique<juce::Component>();
    window->setBounds(0, 0, 50, 50);
    PopupMotion::attach(*window);
    window->setVisible(true);
    window.reset(); // the engine's listener goes with it
    SUCCEED();
}

TEST(PopupMotionLookAndFeel, PopupMenuWindowHookAttachesAndKeepsAPeerlessMenuStill) {
    synth::theme::AppLookAndFeel laf;
    juce::Component menuWindow;
    menuWindow.setBounds(5, 6, 120, 90);
    laf.preparePopupMenuWindow(menuWindow);
    EXPECT_TRUE(menuWindow.getProperties().contains(kAttached));
    menuWindow.setVisible(true);
    EXPECT_FLOAT_EQ(menuWindow.getAlpha(), 1.0f);
    EXPECT_EQ(menuWindow.getBounds(), juce::Rectangle<int>(5, 6, 120, 90));
}

TEST(PopupMotionLookAndFeel, CallOutBoxIsAttachedAsItAsksForItsBorder) {
    synth::theme::AppLookAndFeel laf;
    struct DefaultLookAndFeelScope {
        explicit DefaultLookAndFeelScope(juce::LookAndFeel& l)
            : previous(&juce::LookAndFeel::getDefaultLookAndFeel()) {
            juce::LookAndFeel::setDefaultLookAndFeel(&l);
        }
        ~DefaultLookAndFeelScope() { juce::LookAndFeel::setDefaultLookAndFeel(previous); }
        juce::LookAndFeel* previous;
    } scope(laf);

    juce::Component parent;
    parent.setBounds(0, 0, 400, 300);
    juce::Component content;
    content.setSize(100, 60);
    juce::CallOutBox box(content, {50, 50, 20, 20}, &parent); // a child call-out: no window of its own
    EXPECT_TRUE(box.getProperties().contains(kAttached));
    EXPECT_FLOAT_EQ(box.getAlpha(), 1.0f);
}

TEST(PopupMotionAttach, DisabledEngineStaysOutOfTheWay) {
    PopupMotion::setEnabled(false);
    EXPECT_FALSE(PopupMotion::isEnabled());
    juce::Component window;
    window.setBounds(0, 0, 50, 50);
    PopupMotion::attach(window);
    window.setVisible(true);
    EXPECT_FLOAT_EQ(window.getAlpha(), 1.0f);
    PopupMotion::setEnabled(true);
    EXPECT_TRUE(PopupMotion::isEnabled());
}

} // namespace
