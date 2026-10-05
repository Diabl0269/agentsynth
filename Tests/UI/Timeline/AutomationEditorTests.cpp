// AutomationEditorTests.cpp
//
// The automation lane editor — pointer/pencil/line/eraser tools, tension drag, per-segment
// curve toggle, lane record-mode selector, right-click-any-knob "Show automation lane".
//
// Three groups:
//   1. synth::ui::AutomationLaneEditor in isolation — pointer/pencil/line/eraser gestures, tension
//      scrub, curve-toggle hook, double-click-adds-point, the publish-discipline pin (no mutation
//      during mouseDrag, exactly one revision bump on commit) and a paint smoke test. The
//      component compiles and runs unconditionally, same as TimelineClipLaneArea/PianoRollComponent.
//   2. synth::ui::TimelinePanelComponent's lane rows — showAutomationLane opens the track, the
//      record-mode combo, and each track's fold arrow. The deeper lane-row coverage lives in
//      Tests/UI/Timeline/AutomationLanes/.
//   3. MainComponent integration — right-click-any-knob's headless hook
//      (MainComponent::automateParameter).

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Timeline/AutomationKernel.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineViewState.h"
#include "UserSettings.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::AutomationLane;
using synth::LaneId;
using synth::TimelineDoc;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::TimelineViewState;

namespace {

// The mouse is whole pixels, and a lane maps its range onto about half a hundred of them: a value read back after a
// drag is within a pixel of the one aimed at.
constexpr double kPixelValueSlack = 4.5;

juce::MouseEvent makeMouseEvent(juce::Component& comp, juce::Point<float> position, juce::ModifierKeys mods,
                                bool mouseWasDragged, juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

juce::MouseEvent leftClick(juce::Component& comp, juce::Point<float> pos, int extraFlags = 0) {
    return makeMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), false,
                          pos);
}

juce::MouseEvent leftDrag(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor) {
    return makeMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), true, anchor);
}

juce::Point<float> centreOf(juce::Rectangle<int> rect) { return {(float)rect.getCentreX(), (float)rect.getCentreY()}; }

// ============================================================================
// 1. synth::ui::AutomationLaneEditor
// ============================================================================

struct AutomationEditorFixture {
    TimelineDoc doc;
    TimelineViewState state;
    AppUndoManager undo;
    AutomationLaneEditor editor{state};
    synth::TrackId trackId;
    LaneId laneId;

    AutomationEditorFixture() {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Quarter;

        editor.setTimelineDoc(&doc);
        editor.setUndoManager(&undo);
        editor.setSize(1000, 200); // height 200 over range [0,100]: 2 px per unit value

        trackId = doc.addTrack(TrackKind::Automation, "Automation");
        AutomationLane::RangeSnapshot range;
        range.minValue = 0.0f;
        range.maxValue = 100.0f;
        range.defaultValue = 50.0f;
        laneId = doc.addLane(trackId, "node-uuid-1", "cutoff", range);
        editor.setActiveLane(laneId);
    }
};

} // namespace

TEST(AutomationLaneEditorTest, PointerMoveHandleOneStep) {
    AutomationEditorFixture f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 2.0, 50.0));
    const auto revBefore = f.doc.getRevision();

    const auto rect = f.editor.getHandleRectForTest(2.0);
    ASSERT_FALSE(rect.isEmpty());
    const auto anchor = centreOf(rect);
    // +1.3 beats (52 px @ 40 px/beat), +20 value (40 px @ 2 px/unit, up = higher value).
    const juce::Point<float> dragged(anchor.x + 52.0f, anchor.y - 40.0f);

    f.editor.mouseDown(leftClick(f.editor, anchor));
    EXPECT_EQ(f.doc.getRevision(), revBefore) << "no mutation on mouseDown";
    f.editor.mouseDrag(leftDrag(f.editor, dragged, anchor));
    EXPECT_EQ(f.doc.getRevision(), revBefore) << "no mutation during drag";
    f.editor.mouseUp(leftDrag(f.editor, dragged, anchor));

    EXPECT_EQ(f.doc.getRevision(), revBefore + 1);
    const auto* lane = f.doc.getLane(f.laneId);
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 1u);
    EXPECT_DOUBLE_EQ(lane->points[0].beat, 3.0) << "2.0 + 1.3 beats, Beat snap -> exactly 3.0";
    EXPECT_NEAR(lane->points[0].value, 70.0, kPixelValueSlack);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    const auto* restored = f.doc.getLane(f.laneId);
    ASSERT_NE(restored, nullptr);
    ASSERT_EQ(restored->points.size(), 1u);
    EXPECT_DOUBLE_EQ(restored->points[0].beat, 2.0);
    EXPECT_NEAR(restored->points[0].value, 50.0, 1e-6);
}

TEST(AutomationLaneEditorTest, TensionScrubOnSegmentClampedAndOneStep) {
    AutomationEditorFixture f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 0.0, 20.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 4.0, 80.0));
    const auto revBefore = f.doc.getRevision();

    // Beat 2.0 sits mid-segment (halfway between the two points, x = 80 px), well away from both
    // handles' hit radii.
    const auto anchor = juce::Point<float>((float)f.state.beatToX(2.0), 100.0f);
    const auto dragged = juce::Point<float>(anchor.x, anchor.y - 20.0f); // up 20 px -> +0.20 tension

    f.editor.mouseDown(leftClick(f.editor, anchor));
    ASSERT_TRUE(f.editor.isDragActiveForTest());
    f.editor.mouseDrag(leftDrag(f.editor, dragged, anchor));
    EXPECT_EQ(f.doc.getRevision(), revBefore) << "no mutation during drag";
    f.editor.mouseUp(leftDrag(f.editor, dragged, anchor));

    EXPECT_EQ(f.doc.getRevision(), revBefore + 1);
    const auto* lane = f.doc.getLane(f.laneId);
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 2u);
    EXPECT_NEAR(lane->points[0].tension, 0.2f, 1e-3f);
    EXPECT_DOUBLE_EQ(lane->points[0].beat, 0.0) << "the LEFT point's beat/value must be untouched";
    EXPECT_NEAR(lane->points[0].value, 20.0, 1e-6);

    // Clamp: a huge upward drag pins at +1.0, still one step. The segment now bows away from the first press, and a
    // press has to be on the curve itself to scrub it (anywhere else in the span starts a box), so press on the
    // reshaped line.
    std::vector<synth::TimelineSnapshot::Point> shape;
    for (const auto& bp : f.doc.getLane(f.laneId)->points)
        shape.push_back({bp.beat, bp.value, bp.tension, bp.curve});
    synth::AutomationCursor cursor{};
    const double onCurve = synth::AutomationKernel::evaluate(shape.data(), (int)shape.size(), 2.0, 50.0, cursor);
    const auto second = juce::Point<float>(anchor.x, (float)f.editor.valueToY(onCurve));
    const auto hugeDrag = juce::Point<float>(second.x, second.y - 500.0f);
    f.editor.mouseDown(leftClick(f.editor, second));
    f.editor.mouseDrag(leftDrag(f.editor, hugeDrag, second));
    f.editor.mouseUp(leftDrag(f.editor, hugeDrag, second));
    EXPECT_NEAR(f.doc.getLane(f.laneId)->points[0].tension, 1.0f, 1e-6f);

    f.undo.undo();
    EXPECT_NEAR(f.doc.getLane(f.laneId)->points[0].tension, 0.2f, 1e-3f);
    f.undo.undo();
    EXPECT_NEAR(f.doc.getLane(f.laneId)->points[0].tension, 0.0f, 1e-6f);
}

TEST(AutomationLaneEditorTest, CurveToggleViaMenuHookIsOneStepEach) {
    AutomationEditorFixture f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 0.0, 20.0, 0.0f, static_cast<int>(synth::BreakpointCurve::Linear)));
    const auto revBefore = f.doc.getRevision();

    f.editor.applySegmentCurveChoice(0.0, static_cast<int>(synth::BreakpointCurve::Hold));
    EXPECT_EQ(f.doc.getRevision(), revBefore + 1);
    EXPECT_EQ(f.doc.getLane(f.laneId)->points[0].curve, static_cast<int>(synth::BreakpointCurve::Hold));
    // The point's beat/value must survive the toggle untouched.
    EXPECT_DOUBLE_EQ(f.doc.getLane(f.laneId)->points[0].beat, 0.0);
    EXPECT_NEAR(f.doc.getLane(f.laneId)->points[0].value, 20.0, 1e-6);

    f.editor.applySegmentCurveChoice(0.0, static_cast<int>(synth::BreakpointCurve::Linear));
    EXPECT_EQ(f.doc.getRevision(), revBefore + 2);
    EXPECT_EQ(f.doc.getLane(f.laneId)->points[0].curve, static_cast<int>(synth::BreakpointCurve::Linear));

    // A beat that doesn't resolve to a real point is a no-op.
    f.editor.applySegmentCurveChoice(99.0, static_cast<int>(synth::BreakpointCurve::Hold));
    EXPECT_EQ(f.doc.getRevision(), revBefore + 2);
}

TEST(AutomationLaneEditorTest, PencilThinsAndReplacesSpanOneStep) {
    AutomationEditorFixture f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 1.0, 20.0)); // outside the dragged span — untouched
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 3.0, 50.0)); // dense span, all exactly colinear
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 3.5, 55.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 4.0, 60.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 4.5, 65.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 5.0, 70.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 8.0, 30.0)); // outside the dragged span — untouched
    const auto revBefore = f.doc.getRevision();

    f.editor.setTool(AutomationLaneEditor::Tool::Pencil);
    const juce::Point<float> start((float)f.state.beatToX(3.0), (float)f.editor.valueToY(50.0));
    f.editor.mouseDown(leftClick(f.editor, start));
    EXPECT_EQ(f.doc.getRevision(), revBefore);

    juce::Point<float> last = start;
    for (int i = 1; i <= 8; ++i) {
        const double beat = 3.0 + (double)i * 0.25;  // sweeps 3.0 -> 5.0
        const double value = 50.0 + (double)i * 2.5; // perfectly colinear with (3.0, 50.0)
        last = juce::Point<float>((float)f.state.beatToX(beat), (float)f.editor.valueToY(value));
        f.editor.mouseDrag(leftDrag(f.editor, last, start));
        EXPECT_EQ(f.doc.getRevision(), revBefore) << "no mutation during drag, sample " << i;
    }
    f.editor.mouseUp(leftDrag(f.editor, last, start));

    EXPECT_EQ(f.doc.getRevision(), revBefore + 1) << "the whole stroke is ONE mutation";
    const auto* lane = f.doc.getLane(f.laneId);
    ASSERT_NE(lane, nullptr);

    bool foundOne = false, foundEight = false;
    int insideSpan = 0;
    for (const auto& bp : lane->points) {
        if (std::abs(bp.beat - 1.0) < 1e-6) {
            foundOne = true;
            EXPECT_NEAR(bp.value, 20.0, 1e-6);
        }
        if (std::abs(bp.beat - 8.0) < 1e-6) {
            foundEight = true;
            EXPECT_NEAR(bp.value, 30.0, 1e-6);
        }
        if (bp.beat > 1.0 + 1e-6 && bp.beat < 8.0 - 1e-6)
            ++insideSpan;
    }
    EXPECT_TRUE(foundOne) << "out-of-span point must survive untouched";
    EXPECT_TRUE(foundEight) << "out-of-span point must survive untouched";
    EXPECT_GT(insideSpan, 0) << "the drawn curve must still be represented";
    EXPECT_LT(insideSpan, 9) << "the stroke (nine raw samples) must have been thinned";
}

TEST(AutomationLaneEditorTest, LineToolTwoEndpointsOneStep) {
    AutomationEditorFixture f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 1.0, 10.0)); // outside span — untouched
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 3.0, 40.0)); // inside dragged span — replaced
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 8.0, 90.0)); // outside span — untouched
    const auto revBefore = f.doc.getRevision();

    f.editor.setTool(AutomationLaneEditor::Tool::Line);
    const juce::Point<float> start((float)f.state.beatToX(2.0), (float)f.editor.valueToY(20.0));
    const juce::Point<float> end((float)f.state.beatToX(5.0), (float)f.editor.valueToY(80.0));
    f.editor.mouseDown(leftClick(f.editor, start));
    f.editor.mouseDrag(leftDrag(f.editor, end, start));
    EXPECT_EQ(f.doc.getRevision(), revBefore) << "no mutation during drag";
    f.editor.mouseUp(leftDrag(f.editor, end, start));

    EXPECT_EQ(f.doc.getRevision(), revBefore + 1);
    const auto* lane = f.doc.getLane(f.laneId);
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 4u); // 1.0, 2.0, 5.0, 8.0
    EXPECT_DOUBLE_EQ(lane->points[0].beat, 1.0);
    EXPECT_NEAR(lane->points[0].value, 10.0, 1e-6);
    EXPECT_DOUBLE_EQ(lane->points[1].beat, 2.0);
    EXPECT_NEAR(lane->points[1].value, 20.0, kPixelValueSlack);
    EXPECT_DOUBLE_EQ(lane->points[2].beat, 5.0);
    EXPECT_NEAR(lane->points[2].value, 80.0, kPixelValueSlack);
    EXPECT_DOUBLE_EQ(lane->points[3].beat, 8.0);
    EXPECT_NEAR(lane->points[3].value, 90.0, 1e-6);

    f.undo.undo();
    EXPECT_EQ(f.doc.getLane(f.laneId)->points.size(), 3u);
}

TEST(AutomationLaneEditorTest, EraserRemovesTouchedHandlesOneStep) {
    AutomationEditorFixture f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 1.0, 10.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 3.0, 40.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 6.0, 90.0)); // untouched — the drag never reaches it
    const auto revBefore = f.doc.getRevision();

    f.editor.setTool(AutomationLaneEditor::Tool::Eraser);
    const auto a1 = centreOf(f.editor.getHandleRectForTest(1.0));
    const auto a3 = centreOf(f.editor.getHandleRectForTest(3.0));

    f.editor.mouseDown(leftClick(f.editor, a1));
    f.editor.mouseDrag(leftDrag(f.editor, a3, a1));
    EXPECT_EQ(f.doc.getRevision(), revBefore) << "no mutation during drag";
    f.editor.mouseUp(leftDrag(f.editor, a3, a1));

    EXPECT_EQ(f.doc.getRevision(), revBefore + 1);
    const auto* lane = f.doc.getLane(f.laneId);
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 1u);
    EXPECT_DOUBLE_EQ(lane->points[0].beat, 6.0) << "untouched handle survives";

    f.undo.undo();
    EXPECT_EQ(f.doc.getLane(f.laneId)->points.size(), 3u);
}

TEST(AutomationLaneEditorTest, DoubleClickAddsPointOneStep) {
    AutomationEditorFixture f;
    const auto revBefore = f.doc.getRevision();

    const juce::Point<float> pos((float)f.state.beatToX(2.3), (float)f.editor.valueToY(37.0));
    f.editor.mouseDoubleClick(leftClick(f.editor, pos));

    EXPECT_EQ(f.doc.getRevision(), revBefore + 1);
    const auto* lane = f.doc.getLane(f.laneId);
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 1u);
    EXPECT_DOUBLE_EQ(lane->points[0].beat, 2.0) << "2.3 snapped to Beat -> 2.0";
    EXPECT_NEAR(lane->points[0].value, 37.0, kPixelValueSlack);

    // A double-click ON an existing handle is a no-op.
    const auto handleCentre = centreOf(f.editor.getHandleRectForTest(2.0));
    f.editor.mouseDoubleClick(leftClick(f.editor, handleCentre));
    EXPECT_EQ(f.doc.getRevision(), revBefore + 1);
}

TEST(AutomationLaneEditorTest, NoMutationDuringDragPublishDisciplinePin) {
    AutomationEditorFixture f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 0.0, 20.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 4.0, 80.0));
    const auto revBefore = f.doc.getRevision();

    f.editor.setTool(AutomationLaneEditor::Tool::Pencil);
    const juce::Point<float> start(20.0f, 150.0f);
    f.editor.mouseDown(leftClick(f.editor, start));
    EXPECT_EQ(f.doc.getRevision(), revBefore);

    for (int i = 1; i <= 12; ++i) {
        const juce::Point<float> p(20.0f + (float)i * 8.0f, 150.0f - (float)i * 2.0f);
        f.editor.mouseDrag(leftDrag(f.editor, p, start));
        EXPECT_EQ(f.doc.getRevision(), revBefore) << "revision must not move during mouseDrag, iteration " << i;
    }

    f.editor.mouseUp(leftDrag(f.editor, juce::Point<float>(120.0f, 130.0f), start));
    EXPECT_EQ(f.doc.getRevision(), revBefore + 1) << "exactly one bump at mouse-up";
}

TEST(AutomationLaneEditorTest, SnapshotSmoke) {
    AutomationEditorFixture f;
    f.editor.setSize(1000, 72);
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 0.0, 10.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 4.0, 90.0, 0.5f));
    ASSERT_TRUE(f.doc.addBreakpoint(f.laneId, 8.0, 30.0));

    const juce::Image img = f.editor.createComponentSnapshot(f.editor.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getWidth(), 1000);
    EXPECT_EQ(img.getHeight(), 72);
}

// ============================================================================
// 2. synth::ui::TimelinePanelComponent — lanes fold out under their track
// ============================================================================

TEST(TimelinePanelAutomationLanesTest, ShowAutomationLaneOpensTheTrackAndAddsTheLaneRow) {
    // Doc BEFORE panel (members die in reverse): ~TimelinePanelComponent de-registers from the
    // doc, so the doc must still be alive then — flagged as a stack-use-after-scope by ASAN.
    TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    panel.setTimelineDoc(&doc);

    const auto trackId = doc.addTrack(TrackKind::Midi, "Bass");
    AutomationLane::RangeSnapshot range;
    range.minValue = 0.0f;
    range.maxValue = 1.0f;
    const auto laneId = doc.addLane(trackId, "node-uuid-2", "cutoff", range);
    ASSERT_TRUE(laneId.isValid());

    const int totalBefore = panel.getClipLaneArea().getRowLayout().totalHeight();
    EXPECT_FALSE(panel.isTrackAutomationExpandedForTest(trackId)) << "a track starts folded";
    EXPECT_EQ(panel.laneEditorForTest(laneId), nullptr);

    panel.showAutomationLane(laneId);
    EXPECT_TRUE(panel.isTrackAutomationExpandedForTest(trackId));
    EXPECT_EQ(panel.getSelectedAutomationLane(), laneId);
    EXPECT_FALSE(panel.laneRowBoundsForTest(laneId).isEmpty());
    ASSERT_NE(panel.laneEditorForTest(laneId), nullptr);
    EXPECT_EQ(panel.laneEditorForTest(laneId)->getActiveLane(), laneId);
    EXPECT_GT(panel.getClipLaneArea().getRowLayout().totalHeight(), totalBefore) << "the lane row adds height";
    EXPECT_EQ(panel.getClipLaneArea().getHeight(), panel.getLanesBounds().getHeight() - panel.getRuler().getHeight())
        << "nothing is carved off the bottom of the lanes any more";

    panel.setTrackAutomationExpanded(trackId, false);
    EXPECT_EQ(panel.getClipLaneArea().getRowLayout().totalHeight(), totalBefore) << "folding restores the rows";
    EXPECT_EQ(panel.laneEditorForTest(laneId), nullptr);
}

TEST(TimelinePanelAutomationLanesTest, RecordModeComboWritesTheDocAsOneUndoStep) {
    // Doc/undo before the panel — same destruction-order rule as the test above.
    TimelineDoc doc;
    AppUndoManager undo;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    panel.setTimelineDoc(&doc);
    panel.setUndoManager(&undo);

    const auto trackId = doc.addTrack(TrackKind::Midi, "Lead");
    AutomationLane::RangeSnapshot range;
    const auto laneId = doc.addLane(trackId, "node-uuid-3", "resonance", range);
    panel.showAutomationLane(laneId);
    auto* header = panel.laneHeaderForTest(laneId);
    ASSERT_NE(header, nullptr);

    EXPECT_EQ(doc.getLane(laneId)->recordMode, static_cast<int>(synth::LaneRecordMode::Read)) << "default";
    EXPECT_EQ(header->getRecordModeCombo().getSelectedId(), static_cast<int>(synth::LaneRecordMode::Read) + 1);

    // The combo's own change notification, as a pick from its menu delivers it.
    header->getRecordModeCombo().setSelectedId(4, juce::sendNotificationSync); // Latch
    EXPECT_EQ(doc.getLane(laneId)->recordMode, static_cast<int>(synth::LaneRecordMode::Latch));
    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_EQ(doc.getLane(laneId)->recordMode, static_cast<int>(synth::LaneRecordMode::Read));
    EXPECT_EQ(panel.laneHeaderForTest(laneId), header) << "the header survives an edit that keeps its lane";
    EXPECT_EQ(header->getRecordModeCombo().getSelectedId(), static_cast<int>(synth::LaneRecordMode::Read) + 1)
        << "the combo follows the undo";
}

TEST(TimelinePanelAutomationLanesTest, EachTracksFoldArrowFoldsOnlyThatTrack) {
    // Doc before the panel — same destruction-order rule as the tests above.
    TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    panel.setTimelineDoc(&doc);

    const auto trackA = doc.addTrack(TrackKind::Midi, "A");
    AutomationLane::RangeSnapshot range;
    const auto laneA = doc.addLane(trackA, "node-uuid-a", "cutoff", range);
    const auto trackB = doc.addTrack(TrackKind::Midi, "B");
    const auto laneB = doc.addLane(trackB, "node-uuid-b", "resonance", range);
    ASSERT_TRUE(laneA.isValid());
    ASSERT_TRUE(laneB.isValid());

    ASSERT_EQ(panel.getTrackHeaderCount(), 2);
    auto* headerA = panel.getTrackHeaderAt(0);
    auto* headerB = panel.getTrackHeaderAt(1);
    ASSERT_NE(headerA, nullptr);
    ASSERT_NE(headerB, nullptr);

    headerA->getFoldArrow().onClick();
    EXPECT_TRUE(panel.isTrackAutomationExpandedForTest(trackA));
    EXPECT_FALSE(panel.isTrackAutomationExpandedForTest(trackB));
    EXPECT_NE(panel.laneEditorForTest(laneA), nullptr);
    EXPECT_EQ(panel.laneEditorForTest(laneB), nullptr);

    headerB->getFoldArrow().onClick();
    EXPECT_TRUE(panel.isTrackAutomationExpandedForTest(trackA)) << "opening B leaves A open";
    EXPECT_TRUE(panel.isTrackAutomationExpandedForTest(trackB));

    headerA->getFoldArrow().onClick();
    EXPECT_FALSE(panel.isTrackAutomationExpandedForTest(trackA));
    EXPECT_TRUE(panel.isTrackAutomationExpandedForTest(trackB));
}

// ============================================================================
// 3. MainComponent integration — right-click-any-knob's headless hook.
// ============================================================================

namespace {
class MockProviderTL : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockTL"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

// automateParameter()'s toggle path persists "bottomDockVisible" to the SAME on-disk
// properties file every MainComponent instance reads at construction (MainComponentTests.cpp's
// own MainComponentTest fixture guards against exactly this cross-test leak) — reset the one key
// this file touches before and after, so this test's outcome never depends on execution order.
void resetBottomDockVisibleKey() {
    juce::PropertiesFile::Options opts = synth::userSettingsOptions();

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings()) {
        s->setValue("bottomDockVisible", "0");
        s->saveIfNeeded();
    }
}
} // namespace

class AutomationEditorMainComponentTest : public ::testing::Test {
protected:
    void SetUp() override { resetBottomDockVisibleKey(); }
    void TearDown() override { resetBottomDockVisibleKey(); }
};

TEST_F(AutomationEditorMainComponentTest, KnobAutomateHookCreatesLaneOnAutomationTrack) {
    MainComponent mc(std::make_unique<MockProviderTL>());

    auto& graph = mc.getAudioEngine().getGraph();
    auto node = graph.addNode(synth::AIStateMapper::createModule("Filter"));
    ASSERT_NE(node, nullptr);

    mc.automateParameter(node->nodeID, "cutoff");

    auto& doc = mc.getTimelineDoc();
    synth::TrackId autoTrackId;
    int automationTrackCount = 0;
    for (const auto& track : doc.getTracks()) {
        if (track.kind == synth::TrackKind::Automation) {
            ++automationTrackCount;
            autoTrackId = track.id;
        }
    }
    ASSERT_EQ(automationTrackCount, 1) << "created once";

    const juce::String uuid = node->properties["uuid"].toString();
    ASSERT_TRUE(uuid.isNotEmpty()) << "ensure-uuid must have run";
    // The lane pointer is invalidated by the NEXT doc mutation (TimelineDoc's own contract) — save
    // its id and re-resolve from here on, never hold the pointer across automateParameter() below.
    const synth::LaneId cutoffLaneId = doc.getLaneForParam(uuid, "cutoff")->id;

    juce::RangedAudioParameter* cutoffParam = nullptr;
    for (auto* p : node->getProcessor()->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
            ranged != nullptr && ranged->paramID == "cutoff")
            cutoffParam = ranged;
    ASSERT_NE(cutoffParam, nullptr);
    {
        const auto* lane = doc.getLane(cutoffLaneId);
        ASSERT_NE(lane, nullptr);
        ASSERT_NE(doc.getTrackForLane(lane->id), nullptr);
        EXPECT_EQ(doc.getTrackForLane(lane->id)->id, autoTrackId);
        EXPECT_FLOAT_EQ(lane->range.minValue, cutoffParam->getNormalisableRange().start);
        EXPECT_FLOAT_EQ(lane->range.maxValue, cutoffParam->getNormalisableRange().end);
    }

    EXPECT_TRUE(mc.isBottomDockConfiguredVisible());
    EXPECT_TRUE(mc.getTimelinePanel().isTrackAutomationExpandedForTest(autoTrackId));
    EXPECT_EQ(mc.getTimelinePanel().getSelectedAutomationLane(), cutoffLaneId);

    // A second parameter on the SAME node reuses the same Automation track (not a new one).
    mc.automateParameter(node->nodeID, "resonance");
    automationTrackCount = 0;
    for (const auto& track : doc.getTracks())
        if (track.kind == synth::TrackKind::Automation)
            ++automationTrackCount;
    EXPECT_EQ(automationTrackCount, 1);
    EXPECT_EQ(doc.getTrack(autoTrackId)->lanes.size(), 2u);

    // A duplicate call for the same parameter binds no second lane.
    mc.automateParameter(node->nodeID, "cutoff");
    EXPECT_EQ(doc.getTrack(autoTrackId)->lanes.size(), 2u);
    EXPECT_EQ(doc.getLaneForParam(uuid, "cutoff")->id, cutoffLaneId);
}

// Automate chosen while the Mixer tab is showing (the mixer-column menu fires this same
// wired GraphEditor callback) must switch the dock to the Timeline tab and select the new lane.
TEST_F(AutomationEditorMainComponentTest, AutomateFromMixerTabSwitchesDockToTimelineAndSelectsLane) {
    using Tab = synth::ui::BottomDockComponent::Tab;
    MainComponent mc(std::make_unique<MockProviderTL>());
    auto node = mc.getAudioEngine().getGraph().addNode(synth::AIStateMapper::createModule("Filter"));
    ASSERT_NE(node, nullptr);

    mc.getBottomDock().setActiveTab(Tab::Mixer);
    ASSERT_EQ(mc.getBottomDock().getActiveTab(), Tab::Mixer);
    ASSERT_TRUE(mc.getGraphEditor().onAutomateParameterRequested);

    mc.getGraphEditor().onAutomateParameterRequested(node->nodeID, "cutoff");

    EXPECT_EQ(mc.getBottomDock().getActiveTab(), Tab::Timeline);
    EXPECT_TRUE(mc.isBottomDockConfiguredVisible());
    const auto* lane = mc.getTimelineDoc().getLaneForParam(node->properties["uuid"].toString(), "cutoff");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(mc.getTimelinePanel().getSelectedAutomationLane(), lane->id);
}

// Show in timeline (the mod dot's source-row button) while a MIDI clip is open in the panel: the clip editor
// covers the lanes, so it has to close for the focused lane to be on screen. With no clip open nothing changes.
TEST_F(AutomationEditorMainComponentTest, RevealModulatorClosesAnOpenClipEditorSoTheLaneShows) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    auto node = mc.getAudioEngine().getGraph().addNode(synth::AIStateMapper::createModule("Filter"));
    ASSERT_NE(node, nullptr);
    auto& doc = mc.getTimelineDoc();
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Keys");
    const auto clip = doc.addClip(track, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip.isValid());
    auto& panel = mc.getTimelinePanel();
    auto& reveal = mc.getGraphEditor().getModDot().host.revealModulator;
    ASSERT_TRUE(reveal);

    synth::ui::ModulatorInfo info;
    mc.automateParameter(node->nodeID, "cutoff"); // gives the node its uuid and the lane
    info.targetUuid = node->properties["uuid"].toString();
    info.paramId = "cutoff";

    // No clip open: the lanes are already showing and the reveal leaves that alone.
    ASSERT_FALSE(panel.isPianoRollOpen());
    reveal(info);
    EXPECT_FALSE(panel.isPianoRollOpen());

    panel.openPianoRoll(clip);
    ASSERT_TRUE(panel.isPianoRollOpen());
    reveal(info);

    const auto* lane = doc.getLaneForParam(info.targetUuid, "cutoff");
    ASSERT_NE(lane, nullptr);
    EXPECT_FALSE(panel.isPianoRollOpen());
    EXPECT_FALSE(panel.getPianoRoll().isVisible());
    EXPECT_EQ(panel.getSelectedAutomationLane(), lane->id);
}

// ============================================================================
// 4. Which track a lane lands on (docs/timeline/automation.md#which-track-a-lane-lands-on).
// ============================================================================

namespace {

// Track 1 (Track In -> Oscillator -> Filter) built the way the app's own gestures leave it; the filter's
// node is returned so a test can automate it.
struct OwnedChain {
    juce::AudioProcessorGraph::Node::Ptr filter;
    juce::String filterUuid;
};

OwnedChain buildTrackChain(MainComponent& mc) {
    mc.simulateAddMidiTrackClick();
    auto& graph = mc.getAudioEngine().getGraph();
    OwnedChain chain;
    const juce::String trackInUuid = mc.getTimelineDoc().getTracks().front().bindingUuid;
    juce::AudioProcessorGraph::Node::Ptr trackIn;
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == trackInUuid)
            trackIn = node;
    auto osc = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    chain.filter = graph.addNode(synth::AIStateMapper::createModule("Filter"));
    EXPECT_TRUE(trackIn != nullptr && osc != nullptr && chain.filter != nullptr);
    EXPECT_TRUE(graph.addConnection({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                     {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_TRUE(graph.addConnection({{osc->nodeID, 0}, {chain.filter->nodeID, 0}}));
    chain.filterUuid = juce::Uuid().toDashedString();
    chain.filter->properties.set("uuid", chain.filterUuid);
    if (auto* module = dynamic_cast<ModuleBase*>(chain.filter->getProcessor()))
        module->setNodeUuid(chain.filterUuid);
    return chain;
}

int automationTrackCountOf(const synth::TimelineDoc& doc) {
    int n = 0;
    for (const auto& track : doc.getTracks())
        n += track.kind == synth::TrackKind::Automation ? 1 : 0;
    return n;
}

} // namespace

TEST_F(AutomationEditorMainComponentTest, KnobAutomateOnAModuleInATrackChainPutsTheLaneOnThatTrack) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    const auto chain = buildTrackChain(mc);
    auto& doc = mc.getTimelineDoc();
    const synth::TrackId track1 = doc.getTracks().front().id;

    mc.automateParameter(chain.filter->nodeID, "cutoff");

    const auto* lane = doc.getLaneForParam(chain.filterUuid, "cutoff");
    ASSERT_NE(lane, nullptr);
    ASSERT_NE(doc.getTrackForLane(lane->id), nullptr);
    EXPECT_EQ(doc.getTrackForLane(lane->id)->id, track1);
    EXPECT_EQ(automationTrackCountOf(doc), 0) << "no shared Automation track is created for an owned module";
    EXPECT_EQ(mc.getTimelinePanel().getSelectedAutomationLane(), lane->id);
}

TEST_F(AutomationEditorMainComponentTest, KnobAutomateOnAModuleNoTrackPlaysFallsBackToTheAutomationTrack) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    buildTrackChain(mc);
    auto loose = mc.getAudioEngine().getGraph().addNode(synth::AIStateMapper::createModule("Filter"));
    ASSERT_NE(loose, nullptr);

    mc.automateParameter(loose->nodeID, "cutoff");

    auto& doc = mc.getTimelineDoc();
    ASSERT_EQ(automationTrackCountOf(doc), 1);
    const auto* lane = doc.getLaneForParam(loose->properties["uuid"].toString(), "cutoff");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(doc.getTrackForLane(lane->id)->kind, synth::TrackKind::Automation);
}

// A project saved before the rule has its lanes on the Automation track: opening it moves the owned ones
// onto their track and drops the Automation track once nothing is left on it.
TEST_F(AutomationEditorMainComponentTest, OpeningAProjectMovesAutomationTrackLanesToTheirOwningTrack) {
    const juce::File scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("agentsynth-fro436-" + juce::Uuid().toString());
    ASSERT_TRUE(scratch.createDirectory());
    const juce::File bundle = scratch.getChildFile("Project.agsproj");
    {
        MainComponent mc(std::make_unique<MockProviderTL>());
        const auto chain = buildTrackChain(mc);
        auto& doc = mc.getTimelineDoc();
        auto loose = mc.getAudioEngine().getGraph().addNode(synth::AIStateMapper::createModule("Filter"));
        ASSERT_NE(loose, nullptr);
        mc.automateParameter(loose->nodeID, "cutoff"); // lands on a new Automation track (unowned)
        const juce::String looseUuid = loose->properties["uuid"].toString();

        // The legacy layout: the owned module's lane sits on the Automation track too.
        const synth::TrackId autoTrack = [&] {
            for (const auto& t : doc.getTracks())
                if (t.kind == synth::TrackKind::Automation)
                    return t.id;
            return synth::TrackId{};
        }();
        ASSERT_TRUE(autoTrack.isValid());
        const auto legacy = doc.addLane(autoTrack, chain.filterUuid, "resonance", {0.0f, 1.0f, 0.5f});
        ASSERT_TRUE(legacy.isValid());
        ASSERT_TRUE(doc.addBreakpoint(legacy, 1.0, 0.25));
        ASSERT_TRUE(doc.setLaneRecordMode(legacy, static_cast<int>(synth::LaneRecordMode::Touch)));
        ASSERT_TRUE(mc.saveProjectForTest(bundle));

        ASSERT_TRUE(mc.openProjectForTest(bundle));

        const synth::TrackId track1 = doc.getTracks().front().id;
        const auto* moved = doc.getLaneForParam(chain.filterUuid, "resonance");
        ASSERT_NE(moved, nullptr);
        EXPECT_EQ(doc.getTrackForLane(moved->id)->id, track1);
        ASSERT_EQ(moved->points.size(), 1u);
        EXPECT_EQ(moved->recordMode, static_cast<int>(synth::LaneRecordMode::Touch));
        // The unowned module's lane stays, so the Automation track stays with it.
        const auto* stayed = doc.getLaneForParam(looseUuid, "cutoff");
        ASSERT_NE(stayed, nullptr);
        EXPECT_EQ(doc.getTrackForLane(stayed->id)->kind, synth::TrackKind::Automation);
        EXPECT_EQ(automationTrackCountOf(doc), 1);
    }
    scratch.deleteRecursively();
}

TEST_F(AutomationEditorMainComponentTest, OpeningAProjectRemovesAnAutomationTrackItsMoveEmptied) {
    const juce::File scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("agentsynth-fro436-" + juce::Uuid().toString());
    ASSERT_TRUE(scratch.createDirectory());
    const juce::File bundle = scratch.getChildFile("Project.agsproj");
    {
        MainComponent mc(std::make_unique<MockProviderTL>());
        const auto chain = buildTrackChain(mc);
        auto& doc = mc.getTimelineDoc();
        const auto autoTrack = doc.addTrack(synth::TrackKind::Automation, "Automation");
        ASSERT_TRUE(doc.addLane(autoTrack, chain.filterUuid, "cutoff", {20.0f, 20000.0f, 1000.0f}).isValid());
        ASSERT_TRUE(mc.saveProjectForTest(bundle));
        ASSERT_TRUE(mc.openProjectForTest(bundle));

        EXPECT_EQ(automationTrackCountOf(doc), 0);
        const auto* lane = doc.getLaneForParam(chain.filterUuid, "cutoff");
        ASSERT_NE(lane, nullptr);
        EXPECT_EQ(doc.getTrackForLane(lane->id)->id, doc.getTracks().front().id);
    }
    scratch.deleteRecursively();
}
