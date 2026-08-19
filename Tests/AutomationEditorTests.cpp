// AutomationEditorTests.cpp
//
// The automation lane editor — pointer/pencil/line/eraser tools, tension drag, per-segment
// curve toggle, lane record-mode selector, right-click-any-knob "Show automation lane".
//
// Three groups:
//   1. synth::ui::AutomationLaneEditor in isolation — pointer/pencil/line/eraser gestures, tension
//      scrub, curve-toggle hook, double-click-adds-point, the publish-discipline pin (no mutation
//      during mouseDrag, exactly one revision bump on commit) and a paint smoke test. None of this
//      is #if SYNTH_ENABLE_TIMELINE-gated — the component compiles and runs unconditionally, same
//      as TimelineClipLaneArea/PianoRollComponent (only MainComponent's use of it is gated).
//   2. synth::ui::TimelinePanelComponent's automation strip — opens/closes with a lane selection,
//      shrinks the clip-lane area, and the record-mode selector's headless hook. Also ungated.
//   3. MainComponent integration — right-click-any-knob's headless hook
//      (MainComponent::automateParameter), gated #if SYNTH_ENABLE_TIMELINE since the wiring compiles
//      out entirely with the flag off.

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/AppUndoManager.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/AutomationLaneEditor.h"
#include "../Source/UI/TimelinePanelComponent.h"
#include "../Source/UI/TimelineViewState.h"
#include "MainComponent.h"
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
    EXPECT_NEAR(lane->points[0].value, 70.0, 1e-6);

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

    // Clamp: a huge upward drag pins at +1.0, still one step.
    const auto hugeDrag = juce::Point<float>(anchor.x, anchor.y - 500.0f);
    f.editor.mouseDown(leftClick(f.editor, anchor));
    f.editor.mouseDrag(leftDrag(f.editor, hugeDrag, anchor));
    f.editor.mouseUp(leftDrag(f.editor, hugeDrag, anchor));
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
    EXPECT_LT(insideSpan, 5) << "the dense original span must have been thinned";
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
    EXPECT_NEAR(lane->points[1].value, 20.0, 1e-6);
    EXPECT_DOUBLE_EQ(lane->points[2].beat, 5.0);
    EXPECT_NEAR(lane->points[2].value, 80.0, 1e-6);
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
    EXPECT_NEAR(lane->points[0].value, 37.0, 1e-6);

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
// 2. synth::ui::TimelinePanelComponent — automation strip
// ============================================================================

TEST(TimelinePanelAutomationStripTest, StripOpensWithLaneSelectionAndCloseRestores) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    TimelineDoc doc;
    panel.setTimelineDoc(&doc);

    const auto trackId = doc.addTrack(TrackKind::Automation, "Automation");
    AutomationLane::RangeSnapshot range;
    range.minValue = 0.0f;
    range.maxValue = 1.0f;
    const auto laneId = doc.addLane(trackId, "node-uuid-2", "cutoff", range);
    ASSERT_TRUE(laneId.isValid());

    const int heightBefore = panel.getClipLaneArea().getHeight();
    EXPECT_FALSE(panel.isAutomationStripVisible());

    panel.showAutomationLane(laneId);
    EXPECT_TRUE(panel.isAutomationStripVisible());
    EXPECT_EQ(panel.getSelectedAutomationLane(), laneId);
    EXPECT_FALSE(panel.getAutomationStripBounds().isEmpty());
    EXPECT_LT(panel.getClipLaneArea().getHeight(), heightBefore) << "the clip-lane area must shrink";
    EXPECT_EQ(panel.getAutomationLaneEditor().getActiveLane(), laneId);

    panel.closeAutomationStrip();
    EXPECT_FALSE(panel.isAutomationStripVisible());
    EXPECT_TRUE(panel.getAutomationStripBounds().isEmpty());
    EXPECT_EQ(panel.getClipLaneArea().getHeight(), heightBefore) << "closing restores full height";
}

TEST(TimelinePanelAutomationStripTest, RecordModeSelectorWritesDoc) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    TimelineDoc doc;
    AppUndoManager undo;
    panel.setTimelineDoc(&doc);
    panel.setUndoManager(&undo);

    const auto trackId = doc.addTrack(TrackKind::Automation, "Automation");
    AutomationLane::RangeSnapshot range;
    const auto laneId = doc.addLane(trackId, "node-uuid-3", "resonance", range);
    panel.showAutomationLane(laneId);

    EXPECT_EQ(doc.getLane(laneId)->recordMode, static_cast<int>(synth::LaneRecordMode::Read)) << "default";

    panel.applyAutomationRecordModeChoice(4); // 1-based combo id 4 -> LaneRecordMode::Latch (3)
    EXPECT_EQ(doc.getLane(laneId)->recordMode, static_cast<int>(synth::LaneRecordMode::Latch));
    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_EQ(doc.getLane(laneId)->recordMode, static_cast<int>(synth::LaneRecordMode::Read));
}

TEST(TimelinePanelAutomationStripTest, TrackHeaderAutomationButtonTogglesTheStripForThatTrack) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    TimelineDoc doc;
    panel.setTimelineDoc(&doc);

    // Two Automation tracks, each with one lane — syncTrackHeaders() (driven by setTimelineDoc's
    // initial refresh, then this addTrack/addLane notification) builds a header per track and wires
    // its onAutomationToggleRequested straight into TimelinePanelComponent::toggleAutomationForTrack.
    const auto trackA = doc.addTrack(TrackKind::Automation, "Automation A");
    AutomationLane::RangeSnapshot range;
    const auto laneA = doc.addLane(trackA, "node-uuid-a", "cutoff", range);
    const auto trackB = doc.addTrack(TrackKind::Automation, "Automation B");
    const auto laneB = doc.addLane(trackB, "node-uuid-b", "resonance", range);
    ASSERT_TRUE(laneA.isValid());
    ASSERT_TRUE(laneB.isValid());

    ASSERT_EQ(panel.getTrackHeaderCount(), 2);
    auto* headerA = panel.getTrackHeaderAt(0);
    auto* headerB = panel.getTrackHeaderAt(1);
    ASSERT_NE(headerA, nullptr);
    ASSERT_NE(headerB, nullptr);

    // First click on track A's button: strip was closed -> opens on track A's (only) lane.
    headerA->getAutomationButton().onClick();
    EXPECT_TRUE(panel.isAutomationStripVisible());
    EXPECT_EQ(panel.getSelectedAutomationLane(), laneA);

    // Second click on the SAME track's button: strip is already open on this track -> closes.
    headerA->getAutomationButton().onClick();
    EXPECT_FALSE(panel.isAutomationStripVisible());

    // A different track's button while closed: opens on that track's lane.
    headerB->getAutomationButton().onClick();
    EXPECT_TRUE(panel.isAutomationStripVisible());
    EXPECT_EQ(panel.getSelectedAutomationLane(), laneB);

    // Track A's button while the strip shows track B's lane: switches the strip, doesn't close it.
    headerA->getAutomationButton().onClick();
    EXPECT_TRUE(panel.isAutomationStripVisible());
    EXPECT_EQ(panel.getSelectedAutomationLane(), laneA);
}

// ============================================================================
// 3. MainComponent integration — right-click-any-knob's headless hook.
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

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

private:
    juce::String model = "MockModel";
};

// automateParameter()'s toggle path persists "timelinePanelVisible" to the SAME on-disk
// properties file every MainComponent instance reads at construction (MainComponentTests.cpp's
// own MainComponentTest fixture guards against exactly this cross-test leak) — reset the one key
// this file touches before and after, so this test's outcome never depends on execution order.
void resetTimelinePanelVisibleKey() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings()) {
        s->setValue("timelinePanelVisible", "0");
        s->saveIfNeeded();
    }
}
} // namespace

class AutomationEditorMainComponentTest : public ::testing::Test {
protected:
    void SetUp() override { resetTimelinePanelVisibleKey(); }
    void TearDown() override { resetTimelinePanelVisibleKey(); }
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

    EXPECT_TRUE(mc.isTimelineConfiguredVisible());
    EXPECT_TRUE(mc.getTimelinePanel().isAutomationStripVisible());
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

#endif // SYNTH_ENABLE_TIMELINE
