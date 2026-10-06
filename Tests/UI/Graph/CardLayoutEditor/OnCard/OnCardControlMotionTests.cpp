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

static bool hiddenInStore(OnCardRig& rig, NodeID id, const juce::String& paramId) {
    const auto stored = rig.storedLayout(id);
    return stored.has_value() && stored->hidden.contains(paramId);
}

TEST(OnCardControlMotion, HidingShrinksTheControlAwayFirstAndOnlyThenTakesItOffTheLayout) {
    MotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    editor->setForceAnimateForTest(true);
    auto* widget = widgetOf(*rig.card(id), "drive");
    ASSERT_NE(widget, nullptr);

    hideThroughPanel(rig, *editor, "drive");
    EXPECT_EQ(editor->getShrinkGhostCountForTest(), 1);
    EXPECT_TRUE(editor->hasPendingHideForTest());
    EXPECT_FALSE(hiddenInStore(rig, id, "drive")) << "the card keeps its place while the picture shrinks";
    EXPECT_FLOAT_EQ(widget->getAlpha(), 0.0f) << "only the shrinking picture shows";

    editor->finishMotionForTest();
    EXPECT_EQ(editor->getShrinkGhostCountForTest(), 0);
    EXPECT_FALSE(editor->hasPendingHideForTest());
    EXPECT_TRUE(hiddenInStore(rig, id, "drive"));
}

TEST(OnCardControlMotion, NothingMovesUnderTheShrinkingPicture) {
    MotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    editor->setForceAnimateForTest(true);
    const auto before = synth::ui::collectCells(*rig.card(id));
    const auto ghostRect = editor->getCellRectForTest("drive");

    hideThroughPanel(rig, *editor, "drive");
    for (const auto& cell : before) {
        if (cell.key == "drive")
            continue;
        EXPECT_EQ(editor->getCellRectForTest(cell.key), cell.rect) << cell.key << " stays put";
        EXPECT_FALSE(editor->getCellRectForTest(cell.key).intersects(ghostRect)) << cell.key;
    }
    editor->finishMotionForTest();
}

TEST(OnCardControlMotion, CancelWhileItShrinksBringsTheControlBack) {
    MotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    editor->setForceAnimateForTest(true);
    auto* widget = widgetOf(*rig.card(id), "drive");
    ASSERT_NE(widget, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    editor->cancel();
    EXPECT_FALSE(hiddenInStore(rig, id, "drive"));
    auto* now = widgetOf(*rig.card(id), "drive");
    ASSERT_NE(now, nullptr);
    EXPECT_FLOAT_EQ(now->getAlpha(), 1.0f);
}

TEST(OnCardControlMotion, AnotherEditWhileItShrinksWritesTheHideFirst) {
    MotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    editor->setForceAnimateForTest(true);
    hideThroughPanel(rig, *editor, "drive");
    editor->flushNudgeForTest(); // what every write and Done run first
    EXPECT_TRUE(hiddenInStore(rig, id, "drive"));
    EXPECT_FALSE(editor->hasPendingHideForTest());
    editor->finishMotionForTest();
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

// ---- What the canvas actually paints, frame by frame ------------------------------------------------------------

namespace {

/** `area` of `canvas` (the card's parent) as it paints, through the card's cached image and the editor's overlay. */
juce::Image paintArea(juce::Component& canvas, juce::Rectangle<int> area) {
    juce::Image image(juce::Image::ARGB, area.getWidth(), area.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.setOrigin(-area.getPosition());
    g.reduceClipRegion(area);
    canvas.paintEntireComponent(g, true);
    return image;
}

/** How wide the part of `frame` that differs from `empty` is, in pixels (0 when nothing differs). */
int paintedWidth(const juce::Image& frame, const juce::Image& empty) {
    int left = frame.getWidth(), right = -1;
    for (int y = 0; y < frame.getHeight(); ++y)
        for (int x = 0; x < frame.getWidth(); ++x)
            if (frame.getPixelAt(x, y) != empty.getPixelAt(x, y)) {
                left = std::min(left, x);
                right = std::max(right, x);
            }
    return right < left ? 0 : right - left + 1;
}

/** `rect` (card pixels) on the card's parent, with room around it for an overshoot. */
juce::Rectangle<int> onCanvas(ModuleComponent& card, juce::Rectangle<int> rect) {
    return rect.translated(card.getX(), card.getY()).expanded(rect.getWidth() / 5, rect.getHeight() / 5);
}

} // namespace

TEST(OnCardControlMotion, TheShrinkingPictureIsPaintedSmallerEachFrame) {
    MotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    editor->setForceAnimateForTest(true);
    auto& card = *rig.card(id);
    auto* canvas = card.getParentComponent();
    ASSERT_NE(canvas, nullptr);
    const auto area = onCanvas(card, editor->getCellRectForTest("drive"));

    hideThroughPanel(rig, *editor, "drive");
    editor->setShrinkGhostProgressForTest(1.0f);
    const auto empty = paintArea(*canvas, area);
    std::vector<int> widths;
    // The picture carries the card's own background, so its edge only shows once it has started to shrink.
    for (float t : {0.5f, 0.7f, 0.85f, 0.95f}) {
        editor->setShrinkGhostProgressForTest(t);
        widths.push_back(paintedWidth(paintArea(*canvas, area), empty));
    }
    EXPECT_GT(widths.back(), 0) << "still there near the end";
    for (size_t i = 1; i < widths.size(); ++i)
        EXPECT_LT(widths[i], widths[i - 1]) << "frame " << i << " is painted smaller than the one before";
    editor->finishMotionForTest();
}

TEST(OnCardControlMotion, TheAddedControlIsPaintedGrowingPastItsSizeThenSettling) {
    MotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    editor->setForceAnimateForTest(true);
    clickRow(*panel->getRowForTest("drive"));
    auto& card = *rig.card(id);
    auto* canvas = card.getParentComponent();
    ASSERT_NE(canvas, nullptr);
    const auto area = onCanvas(card, editor->getCellRectForTest("drive"));

    float peakT = 0.0f;
    for (int i = 1; i < 100; ++i)
        if (cm::growScale((float)i / 100.0f) > cm::growScale(peakT))
            peakT = (float)i / 100.0f;
    editor->applyAddFrameForTest(0.0f);
    const auto empty = paintArea(*canvas, area);
    const auto widthAt = [&](float t) {
        editor->applyAddFrameForTest(t);
        return paintedWidth(paintArea(*canvas, area), empty);
    };
    const int early = widthAt(0.15f), peak = widthAt(peakT), settled = widthAt(1.0f);
    EXPECT_GT(early, 0);
    EXPECT_LT(early, settled) << "it starts small";
    EXPECT_GT(peak, settled) << "it passes its size on the way";
    editor->finishMotionForTest();
}
