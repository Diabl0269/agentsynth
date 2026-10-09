// PopupMotionTests.cpp -- the shared soft appear/disappear of popup windows: the pure
// math (start offset, alpha over time, Reduce motion path, out target), the Reduce motion switch,
// and the look-and-feel hooks that hand windows to the engine. No real window is opened: every
// component here is peer-less, which is also the contract that keeps headless runs untouched.

#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
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

TEST(PopupMotionMath, SoftOvershootPeaksThreePercentPastTheEndAndSettles) {
    EXPECT_FLOAT_EQ(easeOutBackSoft(0.0f), 0.0f);
    EXPECT_NEAR(easeOutBackSoft(1.0f), 1.0f, 1e-6f);
    float peak = 0.0f;
    for (int i = 0; i <= 1000; ++i)
        peak = juce::jmax(peak, easeOutBackSoft((float)i / 1000.0f));
    EXPECT_NEAR(peak, 1.03f, 0.002f);
    Style plain;
    EXPECT_FLOAT_EQ(ease(Phase::In, 0.5f, plain), easeOutCubic(0.5f)); // opt-in only
    Style bounce;
    bounce.overshoot = true;
    EXPECT_FLOAT_EQ(ease(Phase::In, 0.5f, bounce), easeOutBackSoft(0.5f));
    EXPECT_FLOAT_EQ(ease(Phase::Out, 0.5f, bounce), easeInCubic(0.5f)); // leaving never bounces
}

TEST(PopupMotionFrame, StyleSetsTheSlideAndAnOvershootKeepsAlphaAtMostOne) {
    Style style;
    style.inSlidePx = 12.0f;
    style.outSlidePx = 6.0f;
    style.overshoot = true;
    const juce::Point<int> right(1, 0);
    EXPECT_FLOAT_EQ(frameAt(Phase::In, 0.0f, right, false, style).offset.x, -12.0f);
    EXPECT_FLOAT_EQ(frameAt(Phase::Out, 1.0f, right, false, style).offset.x, -6.0f);
    const auto over = frameAt(Phase::In, 1.03f, right, false, style);
    EXPECT_FLOAT_EQ(over.alpha, 1.0f);
    EXPECT_GT(over.offset.x, 0.0f); // past rest by 3% of the slide
}

// ----------------------------------------------------------------------------------------------
// A style that scales its body (the mod dot's panel)
// ----------------------------------------------------------------------------------------------

Style growStyle() {
    Style style;
    style.overshoot = true;
    style.inSlidePx = 0.0f;
    style.outSlidePx = 0.0f;
    style.inMs = 200.0;
    style.outMs = 140.0;
    style.alphaInFraction = 0.6f;
    style.inStartScale = 0.4f;
    style.outEndScale = 0.5f;
    return style;
}

TEST(PopupMotionScale, TheDefaultStyleNeverScalesAndKeepsTheSharedDurations) {
    const juce::Point<int> down(0, 1);
    for (const float e : {0.0f, 0.5f, 1.0f}) {
        EXPECT_FLOAT_EQ(frameAt(Phase::In, e, down, false).scale, 1.0f);
        EXPECT_FLOAT_EQ(frameAt(Phase::Out, e, down, false).scale, 1.0f);
    }
    EXPECT_DOUBLE_EQ(durationMs(Phase::In, false, Style{}), 160.0);
    EXPECT_DOUBLE_EQ(durationMs(Phase::Out, false, Style{}), 110.0);
}

TEST(PopupMotionScale, GrowsFromFortyPercentWithTheOvershootAndSettlesAtOne) {
    const auto style = growStyle();
    const juce::Point<int> down(0, 1);
    EXPECT_FLOAT_EQ(frameAt(Phase::In, ease(Phase::In, 0.0f, style), down, false, style, 0.0f).scale, 0.4f);
    const float mid = frameAt(Phase::In, ease(Phase::In, 0.5f, style), down, false, style, 0.5f).scale;
    EXPECT_GT(mid, 0.4f);
    EXPECT_LT(mid, 1.0f);
    float peak = 0.0f;
    for (int i = 0; i <= 100; ++i) {
        const float t = (float)i / 100.0f;
        peak = juce::jmax(peak, frameAt(Phase::In, ease(Phase::In, t, style), down, false, style, t).scale);
    }
    EXPECT_GT(peak, 1.0f) << "the soft overshoot";
    EXPECT_LT(peak, 1.03f);
    EXPECT_NEAR(frameAt(Phase::In, ease(Phase::In, 1.0f, style), down, false, style, 1.0f).scale, 1.0f, 1e-5f);
}

TEST(PopupMotionScale, FadeInIsDoneAtSixtyPercentOfTheArrivalAndLinearBeforeIt) {
    const auto style = growStyle();
    const juce::Point<int> down(0, 1);
    const auto alphaAtTime = [&](float t) {
        return frameAt(Phase::In, ease(Phase::In, t, style), down, false, style, t).alpha;
    };
    EXPECT_FLOAT_EQ(alphaAtTime(0.0f), 0.0f);
    EXPECT_NEAR(alphaAtTime(0.3f), 0.5f, 1e-5f);
    EXPECT_FLOAT_EQ(alphaAtTime(0.6f), 1.0f);
    EXPECT_FLOAT_EQ(alphaAtTime(1.0f), 1.0f);
}

TEST(PopupMotionScale, LeavesDownToHalfSizeWhileFadingLinearlyInTime) {
    const auto style = growStyle();
    const juce::Point<int> down(0, 1);
    EXPECT_FLOAT_EQ(frameAt(Phase::Out, 0.0f, down, false, style).scale, 1.0f);
    EXPECT_FLOAT_EQ(frameAt(Phase::Out, 1.0f, down, false, style).scale, 0.5f);
    const float e = ease(Phase::Out, 0.5f);
    EXPECT_NEAR(frameAt(Phase::Out, e, down, false, style).scale, 1.0f - 0.5f * e, 1e-6f);
    EXPECT_NEAR(frameAt(Phase::Out, e, down, false, style).alpha, 0.5f, 1e-5f) << "linear in time, as every popup";
    EXPECT_FLOAT_EQ(frameAt(Phase::Out, 1.0f, down, false, style).alpha, 0.0f);
}

TEST(PopupMotionScale, ReduceMotionIsThePlainEightyMillisecondFadeWithNoScale) {
    const auto style = growStyle();
    const juce::Point<int> down(0, 1);
    EXPECT_DOUBLE_EQ(durationMs(Phase::In, true, style), 80.0);
    EXPECT_DOUBLE_EQ(durationMs(Phase::Out, true, style), 80.0);
    EXPECT_DOUBLE_EQ(durationMs(Phase::In, false, style), 200.0);
    EXPECT_DOUBLE_EQ(durationMs(Phase::Out, false, style), 140.0);
    for (const float e : {0.0f, 0.5f, 1.0f}) {
        EXPECT_FLOAT_EQ(frameAt(Phase::In, e, down, true, style, e).scale, 1.0f);
        EXPECT_FLOAT_EQ(frameAt(Phase::Out, e, down, true, style, e).scale, 1.0f);
    }
    // the fade keeps the shared curve, not the shortened one
    EXPECT_NEAR(frameAt(Phase::In, 0.5f, down, true, style, 0.5f).alpha, alphaAt(Phase::In, 0.5f), 1e-6f);
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

TEST(PopupMotionFrame, TheFadeIsLinearInTimeWhileTheSlideKeepsItsCubicEase) {
    const juce::Point<int> down(0, 1);
    const float t = 0.25f;
    const auto f = frameAt(Phase::In, ease(Phase::In, t), down, false);
    EXPECT_NEAR(f.alpha, 0.25f, 1e-5f) << "a quarter of the time is a quarter of the fade";
    const auto out = frameAt(Phase::Out, ease(Phase::Out, 0.5f), down, false);
    EXPECT_NEAR(out.alpha, 0.5f, 1e-5f) << "halfway through leaving it is half gone, not still 87% there";
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
            EXPECT_NEAR(in.alpha, 1.0f - std::cbrt(1.0f - e), 1e-5f);
            EXPECT_FLOAT_EQ(in.offset.x, 0.0f);
            EXPECT_FLOAT_EQ(in.offset.y, 0.0f);

            const auto out = frameAt(Phase::Out, e, dir, true);
            EXPECT_NEAR(out.alpha, 1.0f - std::cbrt(e), 1e-5f);
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

// ----------------------------------------------------------------------------------------------
// Leaving: dismiss() fades the live window, then closes
// ----------------------------------------------------------------------------------------------

namespace {

struct AnimateOffScreenGuard {
    explicit AnimateOffScreenGuard(bool on) { PopupMotion::setAnimateOffScreenForTest(on); }
    ~AnimateOffScreenGuard() { PopupMotion::setAnimateOffScreenForTest(false); }
};

// Runs the message loop until `done` or two seconds pass.
bool pumpUntil(const std::function<bool()>& done) {
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (!done() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    return done();
}

} // namespace

TEST(PopupMotionDismiss, ClosesAtOnceWhenTheWindowIsNotOnScreen) {
    juce::Component window;
    window.setBounds(0, 0, 50, 50);
    PopupMotion::attach(window);
    window.setVisible(true);
    bool closed = false;
    PopupMotion::dismiss(window, [&] { closed = true; });
    EXPECT_TRUE(closed); // no peer, no seam: the final state is there before dismiss() returns
    EXPECT_FALSE(PopupMotion::isDismissing(window));
}

TEST(PopupMotionDismiss, ClosesAtOnceWhenTheWindowWasNeverAttached) {
    juce::Component window;
    window.setVisible(true);
    bool closed = false;
    PopupMotion::dismiss(window, [&] { closed = true; });
    EXPECT_TRUE(closed);
}

TEST(PopupMotionDismiss, ClosesAtOnceWhenTheEngineIsDisabled) {
    AnimateOffScreenGuard seam(true);
    PopupMotion::setEnabled(false);
    juce::Component window;
    PopupMotion::attach(window);
    window.setVisible(true);
    bool closed = false;
    PopupMotion::dismiss(window, [&] { closed = true; });
    PopupMotion::setEnabled(true);
    EXPECT_TRUE(closed);
}

TEST(PopupMotionDismiss, StartsAFadeAndClosesOnlyWhenItEnds) {
    AnimateOffScreenGuard seam(true);
    juce::Component window;
    window.setBounds(0, 0, 50, 50);
    PopupMotion::attach(window);
    window.setVisible(true);
    int closes = 0;
    PopupMotion::dismiss(window, [&] { ++closes; });
    EXPECT_EQ(closes, 0); // the close did not happen directly
    EXPECT_TRUE(PopupMotion::isDismissing(window));
    {
        bool self = true, kids = true;
        window.getInterceptsMouseClicks(self, kids);
        EXPECT_FALSE(self);
    } // cannot be acted on twice while it fades

    PopupMotion::dismiss(window, [&] { closes += 100; }); // a second dismiss is ignored
    ASSERT_TRUE(pumpUntil([&] { return closes > 0; }));
    EXPECT_EQ(closes, 1);
    EXPECT_FALSE(PopupMotion::isDismissing(window));
}

TEST(PopupMotionDismiss, ReduceMotionStillFadesBriefly) {
    AnimateOffScreenGuard seam(true);
    ReducedMotionOverrideGuard reduced(true);
    juce::Component window;
    PopupMotion::attach(window);
    window.setVisible(true);
    bool closed = false;
    PopupMotion::dismiss(window, [&] { closed = true; });
    EXPECT_FALSE(closed); // reduce motion shortens the fade, it does not skip it
    EXPECT_TRUE(pumpUntil([&] { return closed; }));
}

TEST(PopupMotionDismiss, ADismissedWindowDeletedByItsCloseLeavesNoGhostBehind) {
    AnimateOffScreenGuard seam(true);
    auto window = std::make_unique<juce::Component>();
    window->setBounds(0, 0, 50, 50);
    PopupMotion::attach(*window);
    window->setVisible(true);
    auto* raw = window.get();
    bool closed = false;
    PopupMotion::dismiss(*raw, [&] {
        closed = true;
        window.reset();
    });
    ASSERT_TRUE(pumpUntil([&] { return closed; }));
    EXPECT_EQ(window, nullptr);
}

TEST(PopupMotionDismiss, CallOutDismissGoesThroughTheFadeToo) {
    AnimateOffScreenGuard seam(true);
    synth::theme::AppLookAndFeel laf;
    juce::LookAndFeel::setDefaultLookAndFeel(&laf);
    {
        juce::Component parent;
        parent.setBounds(0, 0, 400, 300);
        juce::Component content;
        content.setSize(100, 60);
        juce::CallOutBox box(content, {50, 50, 20, 20}, &parent);
        box.setVisible(true);
        PopupMotion::dismissCallOut(box);
        EXPECT_TRUE(PopupMotion::isDismissing(box));
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

// ----------------------------------------------------------------------------------------------
// Closes JUCE does itself leave on a picture. These need a real native window; a platform that
// cannot give one in a test run skips them.
// ----------------------------------------------------------------------------------------------

namespace {

struct RedWindow : juce::Component {
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::red); }
};

// The label-gated ASAN job's Xvfb display rejects a temporary native window with an X11 BadAtom error, which ends
// the whole test process (as the lane point tests once did). These tests need a real window, so
// they skip there; every other job still runs them.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define POPUP_MOTION_TESTS_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(POPUP_MOTION_TESTS_ASAN)
#define POPUP_MOTION_TESTS_ASAN 1
#endif

bool nativeWindowsAbortHere() {
#if JUCE_LINUX && defined(POPUP_MOTION_TESTS_ASAN)
    return true;
#else
    return false;
#endif
}

std::unique_ptr<RedWindow> showNativeWindow() {
    auto window = std::make_unique<RedWindow>();
    window->setBounds(200, 200, 100, 60);
    PopupMotion::attach(*window);
    window->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    window->setVisible(true);
    return window;
}

void letItSettle() {
    const auto until = juce::Time::getMillisecondCounter() + 300;
    while (juce::Time::getMillisecondCounter() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

} // namespace

TEST(PopupMotionLeaving, AWindowJuceHidesLeavesAFadingPicture) {
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
    auto window = showNativeWindow();
    if (!window->isOnDesktop())
        GTEST_SKIP() << "no native window in this environment";
    letItSettle();
    window->setVisible(false);
    EXPECT_EQ(PopupMotion::getNumLeavingGhosts(), 1);
    EXPECT_TRUE(pumpUntil([] { return PopupMotion::getNumLeavingGhosts() == 0; }));
}

TEST(PopupMotionLeaving, AWindowJuceDeletesWhileShowingLeavesAFadingPicture) {
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
    auto window = showNativeWindow();
    if (!window->isOnDesktop())
        GTEST_SKIP() << "no native window in this environment";
    letItSettle();
    window.reset();
    EXPECT_EQ(PopupMotion::getNumLeavingGhosts(), 1);
    EXPECT_TRUE(pumpUntil([] { return PopupMotion::getNumLeavingGhosts() == 0; }));
}

// A menu's picture outlives the menu: when juce brings the app window to the front as the menu closes, a picture
// at the normal window level would be buried before it had faded (the menu then just vanished).
TEST(PopupMotionLeaving, TheLeavingPictureStaysAboveTheAppWindow) {
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
    auto window = showNativeWindow();
    if (!window->isOnDesktop())
        GTEST_SKIP() << "no native window in this environment";
    letItSettle();
    window->setVisible(false);
    const auto ghosts = PopupMotion::getLeavingGhostsForTest();
    ASSERT_EQ(ghosts.size(), 1u);
    EXPECT_TRUE(ghosts.front()->isAlwaysOnTop());
    EXPECT_TRUE(pumpUntil([] { return PopupMotion::getNumLeavingGhosts() == 0; }));
}

// The platform's own close animation ran after ours and froze the app window's frames behind it.
TEST(PopupMotionLeaving, ThePlatformsOwnWindowAnimationIsOff) {
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
    auto window = showNativeWindow();
    if (!window->isOnDesktop())
        GTEST_SKIP() << "no native window in this environment";
    EXPECT_TRUE(PopupMotion::isPlatformAnimationOffForTest(*window));
    window->setVisible(false);
    pumpUntil([] { return PopupMotion::getNumLeavingGhosts() == 0; });
}

namespace {

// Every opacity the window takes while it is still shown.
struct AlphaLog : juce::Component {
    std::vector<float> whileShown;
    void alphaChanged() override {
        if (isVisible())
            whileShown.push_back(getAlpha());
    }
};

} // namespace

// A call-out's dismiss() only posts its hide: the faded window must stay faded until it has gone, not come back
// whole for a moment and then vanish.
TEST(PopupMotionDismiss, AnAsynchronousCloseLeavesTheWindowFadedUntilItIsGone) {
    AnimateOffScreenGuard seam(true);
    AlphaLog window;
    window.setBounds(0, 0, 50, 50);
    PopupMotion::attach(window);
    window.setVisible(true);
    PopupMotion::dismiss(window, [&window] {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<juce::Component>(&window)] {
            if (safe != nullptr)
                safe->setVisible(false);
        });
    });
    ASSERT_TRUE(pumpUntil([&] { return !window.isVisible(); }));
    ASSERT_FALSE(window.whileShown.empty());
    EXPECT_FLOAT_EQ(window.whileShown.back(), 0.0f) << "faded out, and still faded when it went";
    EXPECT_TRUE(pumpUntil([&] { return !PopupMotion::isDismissing(window); }));
    EXPECT_FLOAT_EQ(window.getAlpha(), 1.0f) << "whole again for the next time it is shown";
}
