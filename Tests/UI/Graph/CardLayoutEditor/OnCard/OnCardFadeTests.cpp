// OnCardFadeTests.cpp -- the on-card editor's small swaps fade (docs/layout/animation.md, "Fading things in and
// out"): the control panel's rows and range hint, and the ADSR strip's Controls switch.
// Headless: the animated path is forced and stepped by hand.

#include "../../../Layout/FadeVisibilityTestGuard.h"
#include "Modules/ADSRModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutControlPanel.h"

using namespace oncard_test;
using synth::CardWidget;
using synth::ui::CardLayoutControlPanel;
using synth::ui::FadeVisibility;

TEST(OnCardFades, TheRangeRowFadesOutAndThePanelShrinksWithIt) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openControlPanel(rig, *editor, "cutoff");
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(panel->isRangeRowShownForTest());
    const int full = panel->getHeight();

    FadeAnimateGuard animate;
    auto options = panel->getOptions();
    options.fullRange.reset();
    panel->setOptions(options);
    EXPECT_FALSE(panel->isRangeRowShownForTest()) << "the logical state is immediate";
    EXPECT_TRUE(panel->getMinimumEditorForTest().isVisible()) << "the row stays until the fade has ended";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_LT(panel->getHeight(), full);
    const int mid = panel->getHeight();
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(panel->getMinimumEditorForTest().isVisible());
    EXPECT_LT(panel->getHeight(), mid);
}

TEST(OnCardFades, AnInvalidRangeFadesTheHintInAndAGoodOneFadesItOut) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openControlPanel(rig, *editor, "cutoff");
    ASSERT_NE(panel, nullptr);
    const int plain = panel->getHeight();

    FadeAnimateGuard animate;
    panel->getMinimumEditorForTest().setText("abc", false);
    panel->getMaximumEditorForTest().setText("2000", false);
    panel->commitRangeForTest();
    EXPECT_TRUE(panel->isHintShownForTest());
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(panel->getHeight(), plain);
    const int mid = panel->getHeight();
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_GT(panel->getHeight(), mid);
    EXPECT_TRUE(panel->getHintForTest().isNotEmpty());

    panel->getMinimumEditorForTest().setText("100", false);
    panel->getMaximumEditorForTest().setText("5000", false);
    panel->commitRangeForTest();
    EXPECT_FALSE(panel->isHintShownForTest());
    EXPECT_TRUE(panel->getHintForTest().isNotEmpty()) << "the words stay while it fades out";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_TRUE(panel->getHintForTest().isEmpty());
    EXPECT_EQ(panel->getHeight(), plain);
}

TEST(OnCardFades, SwappingKnobSizeForFaderDirectionKeepsThePanelsHeightAndCrossFades) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openControlPanel(rig, *editor, "cutoff");
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(panel->isSizeRowShownForTest());
    const int height = panel->getHeight();

    FadeAnimateGuard animate;
    auto options = panel->getOptions();
    options.widget = CardWidget::FaderV;
    panel->setOptions(options);
    EXPECT_FALSE(panel->isSizeRowShownForTest());
    EXPECT_TRUE(panel->isDirectionRowShownForTest());
    EXPECT_TRUE(panel->getSizeForTest()->isVisible()) << "the leaving row stays until its fade has ended";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_EQ(panel->getHeight(), height) << "one slot between them: nothing jumps";
    EXPECT_TRUE(panel->getDirectionForTest()->isVisible());
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(panel->getSizeForTest()->isVisible());
    EXPECT_EQ(panel->getHeight(), height);
}

TEST(OnCardFades, TheControlsSwitchOnAnAdsrCardFadesInAndAddControlSlidesOver) {
    FadeAnimateGuard animate;
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto& toggle = editor->getTimeTempoSwitchForTest();
    ASSERT_TRUE(toggle.isVisible());
    EXPECT_LT(toggle.getAlpha(), 1.0f) << "it arrives faded, not popped";
    FadeVisibility::stepAllForTest(0.5f);
    const int mid = toggle.getWidth();
    const int addMid = editor->getAddButtonForTest().getX();
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_GT(toggle.getWidth(), mid);
    EXPECT_GT(editor->getAddButtonForTest().getX(), addMid) << "the strip's other button slides over";
    EXPECT_FLOAT_EQ(toggle.getAlpha(), 1.0f);
}
