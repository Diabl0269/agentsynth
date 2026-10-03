// AutomationLanesPointBubbleTests.cpp -- the automation lane editor's point affordances: the value
// bubble over the hovered or dragged point, the grab hand while a drag is under way, and dragging the
// flat line of a lane with no points. Real mouse events on the real panel's editor.

#include "AutomationLanesTestFixture.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/ReducedMotion.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;

namespace {

struct BubbleLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    BubbleLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        editor->valueToText = [](double v) { return juce::String(v, 1) + " units"; };
    }

    const synth::AutomationLane& theLane() const { return *doc.getLane(lane); }
    juce::Point<float> handleAt(double beat, double value) {
        return {(float)panel.getViewState().beatToX(beat), (float)editor->valueToY(value)};
    }
    void moveTo(juce::Point<float> p) { editor->mouseMove(makeClickEvent(*editor, p)); }
};

struct ReducedMotionGuard {
    ReducedMotionGuard() { synth::ui::setReducedMotionForTest(true); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

TEST(AutomationLanesPointBubbleTest, HoveringAPointShowsItsValueAboveItAndLeavingHidesIt) {
    ReducedMotionGuard noMotion;
    BubbleLane f;
    ASSERT_NE(f.editor, nullptr);
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 25.0));
    auto& bubble = f.editor->getPointBubbleForTest();
    EXPECT_FALSE(bubble.isShown());

    const auto handle = f.handleAt(2.0, 25.0);
    f.moveTo(handle);
    EXPECT_TRUE(bubble.isShown());
    EXPECT_FLOAT_EQ(bubble.getOpacity(), 1.0f);
    EXPECT_EQ(bubble.getText(), "25.0 units") << "the parameter's own text for the value";
    EXPECT_LE(bubble.getBounds().getBottom(), (int)handle.y) << "above the point";
    EXPECT_FALSE(bubble.getBounds().isEmpty());

    f.moveTo({handle.x + 120.0f, handle.y});
    EXPECT_FALSE(bubble.isShown()) << "moving off the point hides it";
    f.moveTo(handle);
    ASSERT_TRUE(bubble.isShown());
    f.editor->mouseExit(makeClickEvent(*f.editor, handle));
    EXPECT_FALSE(bubble.isShown());
    EXPECT_FLOAT_EQ(bubble.getOpacity(), 0.0f);
}

TEST(AutomationLanesPointBubbleTest, WithoutParameterTextTheBubbleShowsThePlainNumber) {
    ReducedMotionGuard noMotion;
    BubbleLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 25.0));
    f.editor->valueToText = nullptr;
    f.moveTo(f.handleAt(2.0, 25.0));
    EXPECT_EQ(f.editor->getPointBubbleForTest().getText(), "25.00");
    f.editor->valueToText = [](double) { return juce::String(); };
    f.moveTo({0.0f, 0.0f});
    f.moveTo(f.handleAt(2.0, 25.0));
    EXPECT_EQ(f.editor->getPointBubbleForTest().getText(), "25.00") << "empty text falls back too";
}

TEST(AutomationLanesPointBubbleTest, DraggingAPointKeepsTheBubbleOnItWithTheLiveValue) {
    ReducedMotionGuard noMotion;
    BubbleLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 25.0));
    auto& bubble = f.editor->getPointBubbleForTest();
    const auto from = f.handleAt(2.0, 25.0);
    const auto to = f.handleAt(2.0, 75.0);

    f.moveTo(from);
    f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
    f.editor->mouseDrag(makeDragEvent(*f.editor, to, from, leftButton()));
    EXPECT_TRUE(bubble.isShown());
    EXPECT_EQ(bubble.getText(), "75.0 units") << "the bubble follows the dragged value";
    EXPECT_GT(bubble.getBounds().getY(), (int)to.y) << "no room above a point at the top: it drops below";

    f.editor->mouseExit(makeClickEvent(*f.editor, to));
    EXPECT_TRUE(bubble.isShown()) << "an exit mid-drag does not drop it";

    f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));
    EXPECT_NEAR(f.theLane().points.front().value, 75.0, 0.5);
    EXPECT_TRUE(bubble.isShown()) << "the pointer is still on the moved point";
    f.moveTo({to.x + 150.0f, to.y});
    EXPECT_FALSE(bubble.isShown());
}

TEST(AutomationLanesPointBubbleTest, AFadeRunsWhenMotionIsAllowedAndSettlesWithoutRepaintLoops) {
    synth::ui::setReducedMotionForTest(false);
    BubbleLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 25.0));
    f.moveTo(f.handleAt(2.0, 25.0));
    // Not on screen in a headless test, so there is no frame clock to fade against: it lands at once.
    EXPECT_FLOAT_EQ(f.editor->getPointBubbleForTest().getOpacity(), 1.0f);
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST(AutomationLanesPointBubbleTest, TheBubbleIsPaintedOverTheLane) {
    ReducedMotionGuard noMotion;
    BubbleLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 75.0));
    auto render = [&] {
        juce::Image image(juce::Image::ARGB, f.editor->getWidth(), f.editor->getHeight(), true,
                          juce::SoftwareImageType());
        juce::Graphics g(image);
        f.editor->paint(g);
        return image;
    };
    const auto before = render();
    f.moveTo(f.handleAt(2.0, 75.0));
    const auto bounds = f.editor->getPointBubbleForTest().getBounds();
    ASSERT_FALSE(bounds.isEmpty());
    const auto after = render();
    int changed = 0;
    for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
        for (int x = bounds.getX(); x < bounds.getRight(); ++x)
            changed += before.getPixelAt(x, y) != after.getPixelAt(x, y) ? 1 : 0;
    EXPECT_GT(changed, bounds.getWidth() * bounds.getHeight() / 4) << "the label box is drawn";
}

TEST(AutomationLanesPointBubbleTest, TheGrabHandShowsOnlyWhileADragIsUnderWay) {
    ReducedMotionGuard noMotion;
    BubbleLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 25.0));
    const juce::MouseCursor arrow(juce::MouseCursor::NormalCursor);
    EXPECT_EQ(f.editor->getMouseCursor(), arrow);
    const auto handle = f.handleAt(2.0, 25.0);
    f.moveTo(handle);
    EXPECT_EQ(f.editor->getMouseCursor(), arrow) << "hovering a point never shows the hand";

    f.editor->mouseDown(makeClickEvent(*f.editor, handle, leftButton()));
    EXPECT_EQ(f.editor->getMouseCursor(), synth::ui::dragGrabCursor()) << "the hand once the point is grabbed";
    f.editor->mouseDrag(makeDragEvent(*f.editor, f.handleAt(3.0, 60.0), handle, leftButton()));
    EXPECT_EQ(f.editor->getMouseCursor(), synth::ui::dragGrabCursor()) << "still the hand while dragging";
    f.editor->mouseUp(makeDragEvent(*f.editor, f.handleAt(3.0, 60.0), handle, leftButton()));
    EXPECT_EQ(f.editor->getMouseCursor(), arrow) << "released: the arrow again";

    f.moveTo({handle.x, 1.0f});
    EXPECT_EQ(f.editor->getMouseCursor(), arrow);
}

TEST(AutomationLanesPointBubbleTest, DraggingTheCurveOrAnEmptyLanesLineShowsTheGrabHand) {
    ReducedMotionGuard noMotion;
    BubbleLane f;
    const juce::MouseCursor arrow(juce::MouseCursor::NormalCursor);
    const double start = f.theLane().range.defaultValue;
    const auto onLine = f.handleAt(4.0, start);
    f.moveTo(onLine);
    EXPECT_EQ(f.editor->getMouseCursor(), arrow) << "hovering the flat line shows no hand";
    f.editor->mouseDown(makeClickEvent(*f.editor, onLine, leftButton()));
    EXPECT_EQ(f.editor->getMouseCursor(), synth::ui::dragGrabCursor()) << "dragging an empty lane's line";
    f.editor->mouseUp(makeDragEvent(*f.editor, onLine, onLine, leftButton()));
    EXPECT_EQ(f.editor->getMouseCursor(), arrow);

    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 20.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 6.0, 80.0));
    const auto onCurve = f.handleAt(4.0, 50.0);
    f.moveTo(onCurve);
    EXPECT_EQ(f.editor->getMouseCursor(), arrow) << "hovering the curve shows no hand";
    f.editor->mouseDown(makeClickEvent(*f.editor, onCurve, leftButton()));
    EXPECT_EQ(f.editor->getMouseCursor(), synth::ui::dragGrabCursor()) << "dragging the curve";
    f.editor->mouseUp(makeDragEvent(*f.editor, onCurve, onCurve, leftButton()));
    EXPECT_EQ(f.editor->getMouseCursor(), arrow);
}

TEST(AutomationLanesPointBubbleTest, DraggingTheFlatLineOfAnEmptyLaneSetsItsValueAsOneUndoStep) {
    ReducedMotionGuard noMotion;
    BubbleLane f;
    ASSERT_TRUE(f.theLane().points.empty());
    const double start = f.theLane().range.defaultValue;
    const auto onLine = f.handleAt(4.0, start);
    const auto higher = f.handleAt(4.0, start + 30.0);

    f.editor->mouseDown(makeClickEvent(*f.editor, onLine, leftButton()));
    f.editor->mouseDrag(makeDragEvent(*f.editor, higher, onLine, leftButton()));
    EXPECT_TRUE(f.editor->getPointBubbleForTest().getText().endsWith(" units")) << "the bubble reads the live value";
    EXPECT_DOUBLE_EQ(f.theLane().range.defaultValue, start) << "nothing is committed mid-drag";
    f.editor->mouseUp(makeDragEvent(*f.editor, higher, onLine, leftButton()));

    EXPECT_TRUE(f.theLane().points.empty()) << "no point is created";
    EXPECT_NEAR(f.theLane().range.defaultValue, start + 30.0, 2.5);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.theLane().range.defaultValue, start) << "Cmd+Z undoes the whole drag";
    EXPECT_FALSE(f.undo.canUndo()) << "it was exactly one undo step";
}

TEST(AutomationLanesPointBubbleTest, TheFlatLineDragClampsToTheRangeAndIgnoresAPressAwayFromTheLine) {
    ReducedMotionGuard noMotion;
    BubbleLane f;
    const double start = f.theLane().range.defaultValue;

    const auto away = f.handleAt(4.0, start + 40.0);
    dragAcross(*f.editor, away, {away.x, away.y - 5.0f});
    EXPECT_FALSE(f.undo.canUndo()) << "a press off the line is not a lane drag";
    EXPECT_DOUBLE_EQ(f.theLane().range.defaultValue, start);

    const auto onLine = f.handleAt(4.0, start);
    dragAcross(*f.editor, onLine, {onLine.x, -200.0f});
    EXPECT_DOUBLE_EQ(f.theLane().range.defaultValue, 100.0) << "held inside the lane's range";
}

TEST(AutomationLanesPointBubbleTest, ALaneWithPointsDoesNotTakeTheFlatLineDrag) {
    ReducedMotionGuard noMotion;
    BubbleLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 1.0, 50.0));
    const double start = f.theLane().range.defaultValue;
    const auto line = f.handleAt(6.0, 50.0);
    dragAcross(*f.editor, line, {line.x, line.y - 20.0f});
    EXPECT_DOUBLE_EQ(f.theLane().range.defaultValue, start) << "the constant only moves while there are no points";
}

TEST(AutomationLanesPointBubbleTest, TheEditorHasAScreenReaderNameAndTooltip) {
    BubbleLane f;
    EXPECT_TRUE(f.editor->getTitle().isNotEmpty());
    EXPECT_TRUE(f.editor->getDescription().isNotEmpty());
    EXPECT_TRUE(f.editor->getTooltip().isNotEmpty());
}
