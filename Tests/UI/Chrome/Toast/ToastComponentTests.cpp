// The toast (Source/UI/Chrome/Toast): a message with one real, focusable action, shown over the canvas and gone by
// itself. Headless, so nothing is on screen and every change lands at once unless a test forces the animated path.

#include "UI/Chrome/Toast/ToastComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

struct ToastFixture {
    juce::Component parent;
    synth::ui::ToastComponent toast;
    ToastFixture() {
        parent.setSize(1000, 700);
        parent.addChildComponent(toast);
        toast.placeIn(parent.getLocalBounds());
    }
};

} // namespace

TEST(ToastComponentTests, ShowsTheMessageWithAFocusableActionThatHasATooltip) {
    ToastFixture f;
    f.toast.show("Made 4 modules poly", "Undo", [] {}, "Undo this change");

    EXPECT_TRUE(f.toast.isToastShown());
    EXPECT_TRUE(f.toast.isVisible());
    EXPECT_EQ(f.toast.getMessageForTest(), "Made 4 modules poly");
    auto& action = f.toast.getActionButton();
    EXPECT_TRUE(action.isVisible());
    EXPECT_TRUE(action.getWantsKeyboardFocus());
    EXPECT_EQ(action.getTooltip(), "Undo this change");
    EXPECT_EQ(action.getTitle(), "Undo");
}

TEST(ToastComponentTests, TheActionDismissesTheToastAndRunsOnce) {
    ToastFixture f;
    int runs = 0;
    f.toast.show("Made 2 modules mono", "Undo", [&] { ++runs; }, "Undo this change");

    f.toast.getActionButton().onClick();

    EXPECT_EQ(runs, 1);
    EXPECT_FALSE(f.toast.isToastShown());
    EXPECT_FALSE(f.toast.isVisible());
    f.toast.getActionButton().onClick();
    EXPECT_EQ(runs, 1) << "a second press has nothing left to run";
}

TEST(ToastComponentTests, ItHidesItselfWhenItsTimeIsUp) {
    ToastFixture f;
    f.toast.show("Made 2 modules poly", "Undo", [] {}, "Undo this change");
    f.toast.expireForTest();
    EXPECT_FALSE(f.toast.isToastShown());
    EXPECT_FALSE(f.toast.isVisible());
}

TEST(ToastComponentTests, WithoutAnActionLabelThereIsNoButton) {
    ToastFixture f;
    f.toast.show("Saved");
    EXPECT_TRUE(f.toast.isToastShown());
    EXPECT_FALSE(f.toast.getActionButton().isVisible());
}

TEST(ToastComponentTests, ASecondToastReplacesTheFirst) {
    ToastFixture f;
    int first = 0, second = 0;
    f.toast.show("one", "Undo", [&] { ++first; }, "");
    f.toast.show("two", "Undo", [&] { ++second; }, "");
    EXPECT_EQ(f.toast.getMessageForTest(), "two");
    f.toast.getActionButton().onClick();
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}

TEST(ToastComponentTests, SitsCentredJustAboveTheBottomOfTheAreaItIsPlacedIn) {
    ToastFixture f;
    f.toast.show("Made 4 modules poly", "Undo", [] {}, "Undo this change");
    const auto bounds = f.toast.getBounds();
    EXPECT_NEAR(bounds.getCentreX(), 500, 1);
    EXPECT_EQ(bounds.getBottom(), 700 - synth::ui::ToastComponent::kBottomMargin);
    EXPECT_EQ(bounds.getHeight(), synth::ui::ToastComponent::kHeight);
}

TEST(ToastComponentTests, AnimationsOffLandsAtOnceEvenWhereMotionWouldRun) {
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    {
        ToastFixture f;
        f.toast.show("Made 4 modules poly", "Undo", [] {}, "Undo this change");
        EXPECT_FLOAT_EQ(f.toast.getProgressForTest(), 1.0f);
        EXPECT_FLOAT_EQ(f.toast.getAlpha(), 1.0f);
    }
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(ToastComponentTests, AnimatedShowStartsHiddenAndSlidesUp) {
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    synth::ui::setReducedMotionForTest(false);
    {
        ToastFixture f;
        f.toast.show("Made 4 modules poly", "Undo", [] {}, "Undo this change");
        EXPECT_TRUE(f.toast.isVisible());
        EXPECT_FLOAT_EQ(f.toast.getProgressForTest(), 0.0f);
        EXPECT_FLOAT_EQ(f.toast.getAlpha(), 0.0f);
        EXPECT_EQ(f.toast.getBottom(),
                  700 - synth::ui::ToastComponent::kBottomMargin + synth::ui::ToastComponent::kSlidePx)
            << "frame 0 sits the slide distance below its resting place";
    }
    synth::ui::setReducedMotionForTest(std::nullopt);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(ToastComponentTests, ReducedMotionFadesWithoutMoving) {
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    synth::ui::setReducedMotionForTest(true);
    {
        ToastFixture f;
        f.toast.show("Made 4 modules poly", "Undo", [] {}, "Undo this change");
        EXPECT_FLOAT_EQ(f.toast.getAlpha(), 0.0f);
        EXPECT_EQ(f.toast.getBottom(), 700 - synth::ui::ToastComponent::kBottomMargin);
    }
    synth::ui::setReducedMotionForTest(std::nullopt);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}
