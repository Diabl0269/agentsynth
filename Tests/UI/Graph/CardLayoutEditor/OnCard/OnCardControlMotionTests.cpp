// OnCardControlMotionTests.cpp -- how a control arrives on a card and leaves it: grown in from its centre with an
// 8% bounce, shrunk away backwards, a plain fade under Reduce Motion, and the layout write itself instant. The
// frame math is pinned as pure functions; the editor's add and Hide go through the real panel and row clicks.

#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"
#include "UI/Layout/ControlMotion.h"

using namespace oncard_test;
namespace cm = synth::ui::control_motion;

namespace {

/** The rig with full motion and the editor animating as if on screen. */
struct MotionRig : OnCardRig {
    MotionRig() { synth::ui::setReducedMotionForTest(false); }
};

} // namespace

TEST(ControlMotion, AControlGrowsInPastItsSizeByEightPercentThenSettles) {
    float peak = 0.0f;
    for (int i = 0; i <= 100; ++i)
        peak = std::max(peak, cm::growScale((float)i / 100.0f));
    EXPECT_NEAR(peak, 1.08f, 0.005f);
    EXPECT_FLOAT_EQ(cm::growScale(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(cm::growScale(1.0f), 1.0f);
}

TEST(ControlMotion, ALeavingControlShrinksFromWholeToNothing) {
    EXPECT_FLOAT_EQ(cm::shrinkScale(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(cm::shrinkScale(1.0f), 0.0f);
    EXPECT_LT(cm::shrinkScale(0.5f), 1.0f);
}

TEST(ControlMotion, AKnobScalesOnBothAxesAndAFaderOnlyAlongItsLength) {
    EXPECT_EQ(cm::axisFor({0, 0, 58, 58}), cm::Axis::both);
    EXPECT_EQ(cm::axisFor({0, 0, 180, 24}), cm::Axis::horizontal);
    EXPECT_EQ(cm::axisFor({0, 0, 24, 140}), cm::Axis::vertical);
    const auto t = cm::scaleAbout({100, 100, 200, 20}, 0.5f, cm::Axis::horizontal);
    float x = 200.0f, y = 110.0f; // the centre stays
    t.transformPoint(x, y);
    EXPECT_NEAR(x, 200.0f, 0.001f);
    EXPECT_NEAR(y, 110.0f, 0.001f);
    float left = 100.0f, top = 100.0f;
    t.transformPoint(left, top);
    EXPECT_NEAR(left, 150.0f, 0.001f) << "half as long";
    EXPECT_NEAR(top, 100.0f, 0.001f) << "not thinner";
}

TEST(OnCardControlMotion, HidingLeavesAShrinkingGhostThatIsGoneOnceItHasShrunk) {
    MotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    editor->setForceAnimateForTest(true);

    hideThroughPanel(rig, *editor, "drive");
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("drive")) << "the layout write is not animated";
    EXPECT_EQ(editor->getShrinkGhostCountForTest(), 1);

    editor->finishMotionForTest();
    EXPECT_EQ(editor->getShrinkGhostCountForTest(), 0);
}

TEST(OnCardControlMotion, UnderReduceMotionTheGhostFadesInPlace) {
    cm::ShrinkGhost ghost(juce::Image(juce::Image::ARGB, 8, 8, true), {0, 0, 40, 40}, cm::Axis::both, true);
    ghost.setProgress(0.5f);
    EXPECT_EQ(ghost.pictureRect().toNearestInt(), juce::Rectangle<int>(0, 0, 40, 40)) << "it does not shrink";
    EXPECT_LT(ghost.opacity(), 1.0f);
}

TEST(OnCardControlMotion, AShrinkingGhostKeepsItsCentreAndNeverTakesTheMouse) {
    cm::ShrinkGhost ghost(juce::Image(juce::Image::ARGB, 8, 8, true), {10, 10, 40, 40}, cm::Axis::both, false);
    ghost.setProgress(0.5f);
    const auto rect = ghost.pictureRect();
    EXPECT_NEAR(rect.getCentreX(), 20.0f, 0.01f);
    EXPECT_NEAR(rect.getCentreY(), 20.0f, 0.01f);
    EXPECT_LT(rect.getWidth(), 40.0f);
    bool self = true, children = true;
    ghost.getInterceptsMouseClicks(self, children);
    EXPECT_FALSE(self);
    EXPECT_FALSE(children);
}

TEST(OnCardControlMotion, AnAddedControlStartsHiddenAndSmallThenLandsWholeAndStraight) {
    MotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    editor->setForceAnimateForTest(true);

    clickRow(*panel->getRowForTest("drive"));
    auto* widget = widgetOf(*rig.card(id), "drive");
    ASSERT_NE(widget, nullptr);
    EXPECT_FLOAT_EQ(widget->getAlpha(), 0.0f) << "it starts invisible";
    EXPECT_FALSE(widget->getTransform().isIdentity()) << "and small, about its centre";

    editor->finishMotionForTest();
    EXPECT_FLOAT_EQ(widget->getAlpha(), 1.0f);
    EXPECT_TRUE(widget->getTransform().isIdentity());
}

TEST(OnCardControlMotion, HeadlessAddAndHideLandAtOnceWithNothingLeftOver) {
    MotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    EXPECT_EQ(editor->getShrinkGhostCountForTest(), 0) << "not on screen, nothing to animate";
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    clickRow(*panel->getRowForTest("drive"));
    auto* widget = widgetOf(*rig.card(id), "drive");
    ASSERT_NE(widget, nullptr);
    EXPECT_FLOAT_EQ(widget->getAlpha(), 1.0f);
    EXPECT_TRUE(widget->getTransform().isIdentity());
}
