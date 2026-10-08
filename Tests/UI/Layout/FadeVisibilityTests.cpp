// FadeVisibilityTests.cpp -- the shared fade for things that appear and disappear (docs/layout/animation.md,
// "Fading things in and out"), and the places that use it that need no other fixture: the status bar's
// message and the account row. Headless: the animated path is forced and stepped by hand.

#include "Auth/InMemoryTokenStore.h"
#include "FadeVisibilityTestGuard.h"
#include "UI/Assistant/AccountRow.h"
#include "UI/Chrome/StatusBarComponent.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::AnimationMode;
using synth::ui::FadeVisibility;

struct Rig {
    juce::Component parent;
    juce::Component child;
    Rig() {
        parent.setSize(100, 100);
        parent.addChildComponent(child);
        child.setBounds(0, 0, 50, 50);
    }
};

TEST(FadeVisibilityTest, OffScreenShowAndHideLandAtOnce) {
    Rig rig;
    FadeVisibility fade{{&rig.child}};
    int frames = 0;
    fade.onFrame = [&] { ++frames; };

    fade.setShown(true);
    EXPECT_TRUE(rig.child.isVisible());
    EXPECT_FLOAT_EQ(rig.child.getAlpha(), 1.0f);
    EXPECT_FALSE(fade.isFading());

    fade.setShown(false);
    EXPECT_FALSE(rig.child.isVisible());
    EXPECT_FLOAT_EQ(rig.child.getAlpha(), 1.0f) << "alpha is put back so a plain setVisible(true) shows it whole";
    EXPECT_EQ(frames, 0) << "a synchronous change calls no frame; the caller lays out itself";
}

TEST(FadeVisibilityTest, AFadeOutTakesTheKeyboardFromTheLeavingControl) {
    FadeAnimateGuard guard;
    Rig rig;
    rig.child.setWantsKeyboardFocus(true);
    rig.parent.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    rig.parent.setVisible(true);
    FadeVisibility fade{{&rig.child}};
    fade.setShown(true);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);

    rig.child.grabKeyboardFocus();
    if (!rig.child.hasKeyboardFocus(true))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";

    fade.setShown(false);
    EXPECT_TRUE(rig.child.isVisible());
    EXPECT_FALSE(rig.child.hasKeyboardFocus(true)) << "a leaving control never keeps the keyboard";
    FadeVisibility::stepAllForTest(1.0f);
    rig.parent.removeFromDesktop();
}

TEST(FadeVisibilityTest, ShowingStartsTransparentAndEndsOpaque) {
    FadeAnimateGuard guard;
    Rig rig;
    FadeVisibility fade{{&rig.child}};

    fade.setShown(true);
    EXPECT_TRUE(rig.child.isVisible()) << "visible from frame 0";
    EXPECT_FLOAT_EQ(rig.child.getAlpha(), 0.0f);
    EXPECT_TRUE(fade.isFading());

    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(rig.child.getAlpha(), 0.5f, 0.01f);
    EXPECT_NEAR(fade.progress(), 0.5f, 1e-4f);

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(rig.child.getAlpha(), 1.0f);
    EXPECT_TRUE(rig.child.isVisible());
    EXPECT_FALSE(fade.isFading());
}

TEST(FadeVisibilityTest, HidingKeepsTheComponentUntilTheFadeHasEnded) {
    FadeAnimateGuard guard;
    Rig rig;
    rig.child.setVisible(true);
    FadeVisibility fade{{&rig.child}};
    bool hidden = false;
    fade.onHidden = [&] { hidden = true; };

    fade.setShown(false);
    EXPECT_TRUE(rig.child.isVisible()) << "not hidden before the fade ends";
    EXPECT_FALSE(fade.isShown()) << "but logically off";
    EXPECT_FALSE(interceptsClicks(rig.child)) << "a leaving control cannot be clicked twice";

    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(rig.child.getAlpha(), 0.5f, 0.01f);
    EXPECT_TRUE(rig.child.isVisible());
    EXPECT_FALSE(hidden);

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.child.isVisible());
    EXPECT_TRUE(hidden);
    EXPECT_TRUE(interceptsClicks(rig.child)) << "mouse handling is restored";
    EXPECT_FLOAT_EQ(rig.child.getAlpha(), 1.0f);
}

TEST(FadeVisibilityTest, AReversalMidFadeStartsFromTheCurrentOpacity) {
    FadeAnimateGuard guard;
    Rig rig;
    rig.child.setVisible(true);
    FadeVisibility fade{{&rig.child}};

    fade.setShown(false);
    FadeVisibility::stepAllForTest(0.75f); // 0.25 left
    ASSERT_NEAR(rig.child.getAlpha(), 0.25f, 0.01f);

    fade.setShown(true);
    EXPECT_NEAR(rig.child.getAlpha(), 0.25f, 0.01f) << "no jump back to 0 or 1";
    EXPECT_TRUE(fade.isShown());
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_TRUE(rig.child.isVisible());
    EXPECT_FLOAT_EQ(rig.child.getAlpha(), 1.0f);
}

TEST(FadeVisibilityTest, AnimationsOffCompletesInstantlyEvenOnScreen) {
    FadeAnimateGuard guard(AnimationMode::off);
    Rig rig;
    rig.child.setVisible(true);
    FadeVisibility fade{{&rig.child}};

    fade.setShown(false);
    EXPECT_FALSE(rig.child.isVisible());
    EXPECT_FALSE(fade.isFading());
    fade.setShown(true);
    EXPECT_TRUE(rig.child.isVisible());
    EXPECT_FLOAT_EQ(rig.child.getAlpha(), 1.0f);
}

TEST(FadeVisibilityTest, EveryTargetFadesTogether) {
    FadeAnimateGuard guard;
    Rig rig;
    juce::Component second;
    rig.parent.addChildComponent(second);
    FadeVisibility fade{{&rig.child, &second}};

    fade.setShown(true);
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(rig.child.getAlpha(), 0.5f, 0.01f);
    EXPECT_NEAR(second.getAlpha(), 0.5f, 0.01f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_TRUE(second.isVisible());
}

// ---- Status bar ------------------------------------------------------------------------------

TEST(StatusBarFadeTest, MessageLandsAtOnceOffScreen) {
    StatusBarComponent bar;
    bar.setSize(600, 24);
    bar.showMessage("Saved");
    EXPECT_FLOAT_EQ(bar.getMessageAlphaForTest(), 1.0f);
    bar.clearMessage(); // transient: left to its own timer
    bar.showStickyMessage("Armed");
    bar.clearMessage();
    EXPECT_FLOAT_EQ(bar.getMessageAlphaForTest(), 0.0f);
    EXPECT_TRUE(bar.getDisplayedMessageForTest().isEmpty());
    EXPECT_TRUE(bar.getTransientMessageForTest().isEmpty());
}

TEST(StatusBarFadeTest, MessageFadesInAndTheTextStaysUntilItHasFadedOut) {
    FadeAnimateGuard guard;
    StatusBarComponent bar;
    bar.setSize(600, 24);

    bar.showStickyMessage("Armed");
    EXPECT_FLOAT_EQ(bar.getMessageAlphaForTest(), 0.0f) << "starts from the normal status";
    EXPECT_EQ(bar.getDisplayedMessageForTest(), "Armed");
    bar.stepMessageFadeForTest(0.5f);
    EXPECT_NEAR(bar.getMessageAlphaForTest(), 0.5f, 1e-4f);
    bar.stepMessageFadeForTest(1.0f);
    EXPECT_FLOAT_EQ(bar.getMessageAlphaForTest(), 1.0f);

    bar.clearMessage();
    EXPECT_TRUE(bar.getTransientMessageForTest().isEmpty()) << "logically cleared at once";
    EXPECT_EQ(bar.getDisplayedMessageForTest(), "Armed") << "still painted while it fades out";
    bar.stepMessageFadeForTest(0.5f);
    EXPECT_NEAR(bar.getMessageAlphaForTest(), 0.5f, 1e-4f);
    bar.stepMessageFadeForTest(1.0f);
    EXPECT_FLOAT_EQ(bar.getMessageAlphaForTest(), 0.0f);
    EXPECT_TRUE(bar.getDisplayedMessageForTest().isEmpty());

    juce::Image image(juce::Image::ARGB, 600, 24, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    bar.paint(g); // both ends paint without a layer left open
}

TEST(StatusBarFadeTest, AnimationsOffLandsTheFinalStateAtOnce) {
    FadeAnimateGuard guard(AnimationMode::off);
    StatusBarComponent bar;
    bar.setSize(600, 24);
    bar.showStickyMessage("Armed");
    EXPECT_FLOAT_EQ(bar.getMessageAlphaForTest(), 1.0f);
    bar.clearMessage();
    EXPECT_FLOAT_EQ(bar.getMessageAlphaForTest(), 0.0f);
}

// ---- Account row -----------------------------------------------------------------------------

struct AccountRowRig {
    synth::AccountService service{"http://mock-host:8787",
                                  [](const juce::String&, const juce::String&, const juce::StringPairArray&,
                                     const juce::String&, int,
                                     const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
                                      synth::AuthClient::HttpResult r;
                                      r.transportFailed = true;
                                      return r;
                                  },
                                  std::make_unique<synth::InMemoryTokenStore>()};
    juce::Component parent;
    synth::AccountRow row;
    AccountRowRig() {
        parent.setSize(300, 100);
        parent.addChildComponent(row);
        row.setBounds(0, 0, 300, 28);
    }
    ~AccountRowRig() { row.setAccountService(nullptr); }
};

TEST(AccountRowFadeTest, RowFadesInAndOutAsAWhole) {
    FadeAnimateGuard guard;
    AccountRowRig rig;

    rig.row.setAccountService(&rig.service);
    EXPECT_TRUE(rig.row.isVisible());
    EXPECT_FLOAT_EQ(rig.row.getAlpha(), 0.0f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(rig.row.getAlpha(), 1.0f);

    rig.row.setAccountService(nullptr);
    EXPECT_TRUE(rig.row.isVisible()) << "stays until the fade has ended";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.row.isVisible());
}

TEST(AccountRowFadeTest, SignInButtonIsVisibleOnceTheRowIsAttachedOffScreen) {
    AccountRowRig rig;
    rig.row.setAccountService(&rig.service);
    EXPECT_TRUE(rig.row.isVisible());
    juce::TextButton* signIn = nullptr;
    for (auto* c : rig.row.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->getButtonText() == "Sign in")
            signIn = b;
    ASSERT_NE(signIn, nullptr);
    EXPECT_TRUE(signIn->isVisible());
    EXPECT_FLOAT_EQ(signIn->getAlpha(), 1.0f);
}

} // namespace
