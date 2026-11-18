// CardHoverLiftTests.cpp -- the 1.5px hover lift a card gets at Depth Rich (and only there), and that it never leaves
// a timer running at rest or ignores Reduce Motion.

#include "UI/Layout/CardHoverLift.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::theme::DepthLevel;
using synth::ui::CardHoverLift;

struct LiftRig {
    explicit LiftRig(DepthLevel level) {
        auto theme = synth::theme::makeObsidian();
        theme.treatment.depth = level;
        lf.applyTheme(theme);
        card.setLookAndFeel(&lf);
        card.setBounds(0, 0, 100, 60);
        CardHoverLift::attach(card);
        lift = CardHoverLift::find(card);
    }
    ~LiftRig() { card.setLookAndFeel(nullptr); }

    void setLevel(DepthLevel level) {
        auto theme = synth::theme::makeObsidian();
        theme.treatment.depth = level;
        lf.applyTheme(theme);
        CardHoverLift::refresh(card);
    }

    synth::theme::AppLookAndFeel lf;
    juce::Component card;
    CardHoverLift* lift = nullptr;
};

class AnimateOffScreen {
public:
    AnimateOffScreen() { synth::ui::FadeVisibility::setAnimateOffScreenForTest(true); }
    ~AnimateOffScreen() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    }
};

} // namespace

TEST(CardHoverLiftTest, RichLiftsAHoveredCardByOnePointFivePixelsAndFlatAndSoftDoNot) {
    for (const auto level : {DepthLevel::Flat, DepthLevel::Soft, DepthLevel::Rich}) {
        LiftRig rig(level);
        ASSERT_NE(rig.lift, nullptr);
        rig.lift->setHoveredForTest(true);
        const float expected = level == DepthLevel::Rich ? CardHoverLift::kLiftPx : 0.0f;
        EXPECT_FLOAT_EQ(rig.lift->lift(), expected) << (int)level;
        EXPECT_FLOAT_EQ(-rig.card.getTransform().getTranslationY(), expected) << (int)level;
        EXPECT_EQ(rig.card.getBounds(), juce::Rectangle<int>(0, 0, 100, 60)) << "bounds never move, only the picture";
        rig.lift->setHoveredForTest(false);
        EXPECT_FLOAT_EQ(rig.lift->lift(), 0.0f);
        EXPECT_TRUE(rig.card.getTransform().isIdentity());
    }
}

TEST(CardHoverLiftTest, TheLiftIsAShortTweenThatEndsAndLeavesNothingRunningAtRest) {
    AnimateOffScreen animate;
    synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
    LiftRig rig(DepthLevel::Rich);
    EXPECT_FALSE(rig.lift->isRunning()) << "nothing runs before the pointer arrives";
    rig.lift->setHoveredForTest(true);
    EXPECT_TRUE(rig.lift->isRunning());
    EXPECT_LT(rig.lift->lift(), CardHoverLift::kLiftPx);
    rig.lift->landForTest();
    EXPECT_FALSE(rig.lift->isRunning()) << "the tween ends";
    EXPECT_FLOAT_EQ(rig.lift->lift(), CardHoverLift::kLiftPx);
    rig.lift->setHoveredForTest(false);
    rig.lift->landForTest();
    EXPECT_FALSE(rig.lift->isRunning());
    EXPECT_FLOAT_EQ(rig.lift->lift(), 0.0f);
}

TEST(CardHoverLiftTest, ReduceMotionAndAnimationsOffLiftAtOnceWithNoTween) {
    for (const auto mode : {synth::ui::AnimationMode::reduced, synth::ui::AnimationMode::off}) {
        AnimateOffScreen animate;
        synth::ui::setAnimationMode(mode);
        LiftRig rig(DepthLevel::Rich);
        rig.lift->setHoveredForTest(true);
        EXPECT_FALSE(rig.lift->isRunning());
        EXPECT_FLOAT_EQ(rig.lift->lift(), CardHoverLift::kLiftPx);
        rig.lift->setHoveredForTest(false);
        EXPECT_FALSE(rig.lift->isRunning());
        EXPECT_FLOAT_EQ(rig.lift->lift(), 0.0f);
    }
}

TEST(CardHoverLiftTest, LeavingRichDropsALiftedCardAtOnce) {
    LiftRig rig(DepthLevel::Rich);
    rig.lift->setHoveredForTest(true);
    ASSERT_FLOAT_EQ(rig.lift->lift(), CardHoverLift::kLiftPx);
    rig.setLevel(DepthLevel::Soft);
    EXPECT_FLOAT_EQ(rig.lift->lift(), 0.0f);
    EXPECT_TRUE(rig.card.getTransform().isIdentity());
    EXPECT_FALSE(rig.lift->isRunning());
}

TEST(CardHoverLiftTest, ACardAnotherMotionTransformsIsLeftAlone) {
    LiftRig rig(DepthLevel::Rich);
    const auto reveal = juce::AffineTransform::scale(0.9f);
    rig.card.setTransform(reveal);
    rig.lift->setHoveredForTest(true);
    EXPECT_EQ(rig.card.getTransform(), reveal);
}
