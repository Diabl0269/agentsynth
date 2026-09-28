// TrackAutomationLanesTests.cpp
//
// Per-track automation lane rows (docs/timeline/track-automation.md), at the panel and clip-lane
// level: the header's A expands/collapses rows, both columns read one TrackRowLayout, clip hit
// testing ignores lane rows, revealAutomationLane focuses a row (or opens the strip for a global
// lane), the timeline tool drives the lane editors (incl. the Draw tool's curve selector), the
// strip lists only global lanes, the toolbar's global toggle, and "automation follows events".

#include "TimelineClipLane/TimelineClipLaneTestFixture.h"

#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TrackAutomationLanes/LaneToolMapping.h"
#include "UI/Timeline/TrackAutomationLanes/TrackAutomationLanes.h"

using synth::AutomationLane;
using synth::LaneId;
using synth::ui::AutomationLaneEditor;
using synth::ui::EditTool;
using synth::ui::LaneCurve;
using synth::ui::TimelinePanelComponent;

namespace {

// Doc/undo BEFORE the panel: ~TimelinePanelComponent de-registers from the doc.
struct LanesPanelRig {
    TimelineDoc doc;
    AppUndoManager undo;
    TimelinePanelComponent panel;
    synth::TrackId midi, audio;
    LaneId cutoff, resonance;

    LanesPanelRig() {
        panel.setSize(1200, 500);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        midi = doc.addTrack(TrackKind::Midi, "Lead");
        audio = doc.addTrack(TrackKind::Audio, "Drums");
        AutomationLane::RangeSnapshot range;
        range.minValue = 0.0f;
        range.maxValue = 1.0f;
        cutoff = doc.addLane(midi, "synth-uuid", "cutoff", range);
        resonance = doc.addLane(midi, "synth-uuid", "resonance", range);
    }
    synth::ui::TrackAutomationLanes& lanes() { return panel.getTrackAutomationLanes(); }
    const synth::ui::TrackRowLayout& layout() { return panel.getClipLaneArea().getRowLayout(); }
};

} // namespace

TEST(TrackAutomationLanesTest, HeaderAButtonExpandsAndCollapsesTheTracksLaneRows) {
    LanesPanelRig rig;
    auto* header = rig.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    const int rowHeight = rig.layout().getTrackRowHeight();
    EXPECT_EQ(rig.layout().trackRowTop(1), rowHeight);

    header->getAutomationButton().onClick();
    EXPECT_TRUE(rig.lanes().isExpanded(rig.midi));
    EXPECT_TRUE(header->isAutomationLanesExpanded());
    EXPECT_TRUE(header->getAutomationButton().getToggleState()) << "A is lit while expanded";
    EXPECT_EQ(rig.layout().getLaneRowCount(), 2);
    EXPECT_EQ(rig.lanes().getVisibleLaneHeaderCount(), 2);
    ASSERT_NE(rig.lanes().getLaneHeaderForLane(rig.cutoff), nullptr);
    const int laneHeight = rig.layout().getLaneRowHeight();
    EXPECT_EQ(rig.layout().trackRowTop(1), rowHeight + 2 * laneHeight);
    EXPECT_EQ(rig.panel.getTrackHeaderAt(1)->getY(), rig.layout().trackRowTop(1))
        << "the header column reads the SAME layout the clip lanes do";
    EXPECT_EQ(rig.lanes().getLaneHeaderForLane(rig.resonance)->getY(), rowHeight + laneHeight);
    EXPECT_GE(rig.lanes().getVisibleEditorCount(), 1);

    header->getAutomationButton().onClick();
    EXPECT_FALSE(rig.lanes().isExpanded(rig.midi));
    EXPECT_EQ(rig.layout().getLaneRowCount(), 0);
    EXPECT_EQ(rig.lanes().getVisibleLaneHeaderCount(), 0);
    EXPECT_EQ(rig.lanes().getVisibleEditorCount(), 0);
}

TEST(TrackAutomationLanesTest, AddingALaneToAnExpandedTrackAddsARowWithoutARebuild) {
    LanesPanelRig rig;
    rig.lanes().setExpanded(rig.midi, true);
    AutomationLane::RangeSnapshot range;
    rig.doc.addLane(rig.midi, "synth-uuid", "drive", range);
    EXPECT_EQ(rig.layout().getLaneRowCount(), 3);
    EXPECT_EQ(rig.lanes().getVisibleLaneHeaderCount(), 3);
    rig.doc.removeLane(rig.cutoff);
    EXPECT_EQ(rig.lanes().getVisibleLaneHeaderCount(), 2);
}

TEST(TrackAutomationLanesTest, TheAButtonIsAlwaysOfferedOnMidiAndAudioTracks) {
    LanesPanelRig rig;
    auto* audioHeader = rig.panel.getTrackHeaderAt(1);
    ASSERT_NE(audioHeader, nullptr);
    EXPECT_TRUE(audioHeader->getAutomationButton().isVisible()) << "even with no lanes yet";
    EXPECT_TRUE(audioHeader->getAutomationButton().getTooltip().contains("0 lanes"));
    EXPECT_TRUE(rig.panel.getTrackHeaderAt(0)->getAutomationButton().getTooltip().contains("2 lanes"));
}

TEST(TrackAutomationLanesTest, ContextMenuShowHideItemTogglesExpansion) {
    LanesPanelRig rig;
    auto* header = rig.panel.getTrackHeaderAt(0);
    juce::String itemText;
    header->setShowContextMenuHookForTest([&itemText](juce::PopupMenu& menu) {
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
            if (it.getItem().itemID == synth::ui::TimelineTrackHeaderComponent::kToggleAutomationLanesMenuId)
                itemText = it.getItem().text;
    });
    auto menu = header->buildContextMenu();
    juce::ignoreUnused(menu);
    header->applyContextMenuChoice(synth::ui::TimelineTrackHeaderComponent::kToggleAutomationLanesMenuId);
    EXPECT_TRUE(rig.lanes().isExpanded(rig.midi));
    bool sawHide = false;
    const auto expandedMenu = header->buildContextMenu();
    for (juce::PopupMenu::MenuItemIterator it(expandedMenu); it.next();)
        sawHide = sawHide || it.getItem().text == "Hide automation";
    EXPECT_TRUE(sawHide);
}

TEST(TrackAutomationLanesTest, RevealExpandsScrollsAndFocusesATrackLane) {
    LanesPanelRig rig;
    // Push the Lead track's lanes far below the fold.
    for (int i = 0; i < 20; ++i)
        rig.doc.addTrack(TrackKind::Midi, "Filler");
    ASSERT_TRUE(rig.doc.moveTrack(rig.midi, 21));

    rig.panel.revealAutomationLane(rig.resonance);
    EXPECT_TRUE(rig.lanes().isExpanded(rig.midi));
    EXPECT_EQ(rig.lanes().getFocusedLane(), rig.resonance);
    auto* laneHeader = rig.lanes().getLaneHeaderForLane(rig.resonance);
    ASSERT_NE(laneHeader, nullptr);
    EXPECT_TRUE(laneHeader->isFocusedLane());
    EXPECT_GT(rig.panel.getViewState().trackScrollY, 0.0) << "scrolled so the row is on screen";
    auto* editor = rig.lanes().getEditorForLane(rig.resonance);
    ASSERT_NE(editor, nullptr) << "a visible row has an editor";
    EXPECT_TRUE(editor->isHighlighted());
    EXPECT_GE(editor->getY(), 0);
    EXPECT_LE(editor->getBottom(), rig.panel.getClipLaneArea().getHeight());
    EXPECT_FALSE(rig.panel.isAutomationStripVisible()) << "a track lane is not shown in the strip";
}

TEST(TrackAutomationLanesTest, RevealingAGlobalLaneOpensTheStrip) {
    LanesPanelRig rig;
    const auto automation = rig.doc.addTrack(TrackKind::Automation, "Automation");
    AutomationLane::RangeSnapshot range;
    const auto global = rig.doc.addLane(automation, "lfo-uuid", "rate", range);
    rig.panel.revealAutomationLane(global);
    EXPECT_TRUE(rig.panel.isAutomationStripVisible());
    EXPECT_EQ(rig.panel.getSelectedAutomationLane(), global);
    EXPECT_FALSE(rig.lanes().getFocusedLane().isValid());
}

TEST(TrackAutomationLanesTest, OnlyVisibleRowsGetEditors) {
    LanesPanelRig rig;
    AutomationLane::RangeSnapshot range;
    for (int i = 0; i < 30; ++i)
        rig.doc.addLane(rig.midi, "synth-uuid", "p" + juce::String(i), range);
    rig.lanes().setExpanded(rig.midi, true);
    EXPECT_EQ(rig.lanes().getVisibleLaneHeaderCount(), 32);
    const int visible = rig.lanes().getVisibleEditorCount();
    EXPECT_GT(visible, 0);
    EXPECT_LT(visible, 32) << "editors are created for rows inside the visible window only";
}

TEST(TrackAutomationLanesTest, ClipLaneHitTestingIgnoresLaneRows) {
    LanesPanelRig rig;
    rig.panel.getViewState().pixelsPerBeat = 40.0;
    const auto clipB = rig.doc.addClip(rig.midi, 0.0, 4.0, "Clip");
    const auto second = rig.doc.addTrack(TrackKind::Midi, "Bass");
    const auto clipOnSecond = rig.doc.addClip(second, 0.0, 4.0, "Bass clip");
    rig.lanes().setExpanded(rig.midi, true);

    auto& lane = rig.panel.getClipLaneArea();
    const auto rect = lane.getClipRect(clipOnSecond);
    EXPECT_EQ(rect.getY(), rig.layout().trackRowTop(2)) << "clip rows sit below the expanded lane rows";

    // The Draw tool pressed on a lane row authors nothing.
    rig.panel.setActiveTool(EditTool::Draw);
    const int laneRowY = rig.layout().getTrackRowHeight() + 3;
    const auto clipsBefore = rig.doc.getTrack(rig.midi)->clips.size();
    lane.mouseDown(leftClick(lane, {400.0f, (float)laneRowY}));
    lane.mouseUp(leftClick(lane, {400.0f, (float)laneRowY}));
    EXPECT_EQ(rig.doc.getTrack(rig.midi)->clips.size(), clipsBefore);

    // A clip on Bass dragged up over Lead's lane rows targets Lead (the parent track).
    rig.panel.setActiveTool(EditTool::Select);
    const auto from = centreOf(lane.getClipRect(clipOnSecond));
    lane.mouseDown(leftClick(lane, from));
    const juce::Point<float> to{from.x, (float)(laneRowY + 2)};
    lane.mouseDrag(leftDrag(lane, to, from));
    EXPECT_EQ(lane.getPreviewRowDeltaForTest(), -2) << "Bass(2) -> Lead(0): the pointer is over Lead's lane rows";
    lane.mouseUp(leftDrag(lane, to, from));
    EXPECT_EQ(rig.doc.getTrackForClip(clipOnSecond)->id, rig.midi) << "landed on the TRACK row, not a lane row";
    juce::ignoreUnused(clipB);
}

// ---- tools ---------------------------------------------------------------------------------

TEST(TrackAutomationLanesTest, ToolMappingCoversEveryToolAndCurve) {
    using Tool = AutomationLaneEditor::Tool;
    EXPECT_EQ(synth::ui::laneEditorToolFor(EditTool::Select, LaneCurve::Sine).tool, Tool::Pointer);
    EXPECT_EQ(synth::ui::laneEditorToolFor(EditTool::Erase, LaneCurve::Sine).tool, Tool::Eraser);
    for (auto clipOnly : {EditTool::Split, EditTool::Glue, EditTool::Mute})
        EXPECT_EQ(synth::ui::laneEditorToolFor(clipOnly, LaneCurve::Line).tool, Tool::Pointer);
    EXPECT_EQ(synth::ui::laneEditorToolFor(EditTool::Draw, LaneCurve::Freehand).tool, Tool::Pencil);
    EXPECT_EQ(synth::ui::laneEditorToolFor(EditTool::Draw, LaneCurve::Line).tool, Tool::Line);
    const std::pair<LaneCurve, synth::ShapeKind> shapes[] = {
        {LaneCurve::Sine, synth::ShapeKind::Sine},       {LaneCurve::Triangle, synth::ShapeKind::Triangle},
        {LaneCurve::Square, synth::ShapeKind::Square},   {LaneCurve::SawUp, synth::ShapeKind::SawUp},
        {LaneCurve::SawDown, synth::ShapeKind::SawDown}, {LaneCurve::Random, synth::ShapeKind::Random}};
    for (const auto& [curve, kind] : shapes) {
        const auto mapped = synth::ui::laneEditorToolFor(EditTool::Draw, curve);
        EXPECT_EQ(mapped.tool, Tool::Shape);
        EXPECT_EQ(mapped.shape, kind);
    }
}

TEST(TrackAutomationLanesTest, TheTimelineToolAndCurveSelectorDriveLaneEditors) {
    LanesPanelRig rig;
    rig.lanes().setExpanded(rig.midi, true);
    auto* editor = rig.lanes().getEditorForLane(rig.cutoff);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->getTool(), AutomationLaneEditor::Tool::Pointer);
    EXPECT_FALSE(rig.lanes().getCurveButton().isVisible()) << "curve selector only with Draw";

    rig.panel.setActiveTool(EditTool::Draw);
    EXPECT_TRUE(rig.lanes().getCurveButton().isVisible());
    EXPECT_EQ(editor->getTool(), AutomationLaneEditor::Tool::Pencil) << "Freehand by default";

    rig.lanes().applyCurveMenuChoice(4); // 1-based: Freehand, Line, Sine, Triangle
    EXPECT_EQ(rig.lanes().getCurve(), LaneCurve::Triangle);
    EXPECT_EQ(editor->getTool(), AutomationLaneEditor::Tool::Shape);
    EXPECT_EQ(editor->getShapeKind(), synth::ShapeKind::Triangle);

    rig.panel.setActiveTool(EditTool::Erase);
    EXPECT_EQ(editor->getTool(), AutomationLaneEditor::Tool::Eraser);
    EXPECT_FALSE(rig.lanes().getCurveButton().isVisible());
    EXPECT_EQ(rig.panel.getAutomationLaneEditor().getTool(), AutomationLaneEditor::Tool::Pointer)
        << "the bottom strip keeps its own tool row";
}

TEST(TrackAutomationLanesTest, LaneHeaderRecordModeAndRemoveAreUndoable) {
    LanesPanelRig rig;
    rig.lanes().setExpanded(rig.midi, true);
    auto* header = rig.lanes().getLaneHeaderForLane(rig.cutoff);
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(header->getLabelText().contains("cutoff")) << "falls back to the raw paramId with no host";

    header->applyRecordModeChoice(5); // Write
    EXPECT_EQ(rig.doc.getLane(rig.cutoff)->recordMode, (int)synth::LaneRecordMode::Write);
    rig.undo.undo();
    EXPECT_EQ(rig.doc.getLane(rig.cutoff)->recordMode, (int)synth::LaneRecordMode::Read);

    rig.lanes()
        .getLaneHeaderForLane(rig.cutoff)
        ->applyContextMenuChoice(synth::ui::TrackLaneHeaderComponent::kRemoveLaneMenuId);
    EXPECT_EQ(rig.doc.getLane(rig.cutoff), nullptr);
    EXPECT_EQ(rig.lanes().getVisibleLaneHeaderCount(), 1);
    rig.undo.undo();
    EXPECT_NE(rig.doc.getLane(rig.cutoff), nullptr);
}

// ---- the bottom strip is for global lanes -------------------------------------------------

TEST(TrackAutomationLanesTest, StripListsOnlyGlobalLanesAndTheToolbarToggleDrivesIt) {
    LanesPanelRig rig;
    EXPECT_TRUE(rig.panel.collectAutomationLaneOptions().empty()) << "track lanes are not global";
    EXPECT_EQ(rig.panel.getGlobalAutomationLaneCount(), 0);
    auto& button = rig.lanes().getGlobalAutomationButton();
    EXPECT_TRUE(button.getBadgeText().isEmpty());

    // No global lane yet: the toggle opens the strip EMPTY on its picker.
    button.onClick();
    EXPECT_TRUE(rig.panel.isAutomationStripVisible());
    EXPECT_FALSE(rig.panel.getSelectedAutomationLane().isValid());
    EXPECT_TRUE(button.getToggleState());
    button.onClick();
    EXPECT_FALSE(rig.panel.isAutomationStripVisible());

    const auto automation = rig.doc.addTrack(TrackKind::Automation, "Automation");
    AutomationLane::RangeSnapshot range;
    const auto global = rig.doc.addLane(automation, "lfo-uuid", "rate", range);
    EXPECT_EQ(button.getBadgeText(), "1");
    const auto options = rig.panel.collectAutomationLaneOptions();
    ASSERT_EQ(options.size(), 1u);
    EXPECT_EQ(options.front().id, global);

    button.onClick();
    EXPECT_TRUE(rig.panel.isAutomationStripVisible());
    EXPECT_EQ(rig.panel.getSelectedAutomationLane(), global);
}

// ---- automation follows events -------------------------------------------------------------

namespace {

struct FollowsRig : ClipLaneFixture {
    synth::TrackId track;
    synth::LaneId laneId;
    ClipId clip;

    FollowsRig() {
        track = doc.addTrack(TrackKind::Midi, "Lead");
        AutomationLane::RangeSnapshot range;
        range.maxValue = 1.0f;
        laneId = doc.addLane(track, "synth-uuid", "cutoff", range);
        clip = doc.addClip(track, 0.0, 4.0, "Clip");
        doc.addBreakpoint(laneId, 1.0, 0.5);
        doc.addBreakpoint(laneId, 3.0, 0.75);
    }
    std::vector<double> beats() const {
        std::vector<double> out;
        for (const auto& p : doc.getLane(laneId)->points)
            out.push_back(p.beat);
        return out;
    }
    void dragClipByBeats(double deltaBeats) {
        const auto from = centreOf(lane.getClipRect(clip));
        const juce::Point<float> to{from.x + (float)(deltaBeats * state.pixelsPerBeat), from.y};
        lane.mouseDown(leftClick(lane, from));
        lane.mouseDrag(leftDrag(lane, to, from));
        lane.mouseUp(leftDrag(lane, to, from));
    }
};

} // namespace

TEST(AutomationFollowsClipsTest, OffByDefaultAClipMoveLeavesAutomationAlone) {
    FollowsRig rig;
    EXPECT_FALSE(rig.lane.isAutomationFollowsClips());
    rig.dragClipByBeats(4.0);
    EXPECT_DOUBLE_EQ(rig.doc.getClip(rig.clip)->startBeat, 4.0);
    EXPECT_EQ(rig.beats(), (std::vector<double>{1.0, 3.0}));
}

TEST(AutomationFollowsClipsTest, OnAClipMoveCarriesItsAutomationInOneUndoStep) {
    FollowsRig rig;
    rig.lane.setAutomationFollowsClips(true);
    rig.dragClipByBeats(4.0);
    EXPECT_DOUBLE_EQ(rig.doc.getClip(rig.clip)->startBeat, 4.0);
    EXPECT_EQ(rig.beats(), (std::vector<double>{5.0, 7.0}));

    ASSERT_TRUE(rig.undo.canUndo());
    rig.undo.undo();
    EXPECT_DOUBLE_EQ(rig.doc.getClip(rig.clip)->startBeat, 0.0) << "one step restores the clip...";
    EXPECT_EQ(rig.beats(), (std::vector<double>{1.0, 3.0})) << "...and its automation together";
    EXPECT_FALSE(rig.undo.canUndo());
}

TEST(AutomationFollowsClipsTest, OnDeleteAndDuplicateCarryAutomationToo) {
    FollowsRig rig;
    rig.lane.setAutomationFollowsClips(true);

    rig.lane.applyClipContextChoice(rig.clip, TimelineClipLaneArea::ClipContextChoice::Duplicate, 0.0);
    EXPECT_EQ(rig.beats(), (std::vector<double>{1.0, 3.0, 5.0, 7.0})) << "the copy lands right after";
    rig.undo.undo();
    EXPECT_EQ(rig.beats(), (std::vector<double>{1.0, 3.0}));

    rig.selection.setSelection({rig.clip});
    EXPECT_TRUE(rig.lane.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(rig.doc.getClip(rig.clip), nullptr);
    EXPECT_TRUE(rig.doc.getLane(rig.laneId)->points.empty());
    rig.undo.undo();
    EXPECT_NE(rig.doc.getClip(rig.clip), nullptr);
    EXPECT_EQ(rig.beats(), (std::vector<double>{1.0, 3.0}));
}

TEST(AutomationFollowsClipsTest, PanelToggleIsPersistedAndReachesTheLanes) {
    TimelineDoc doc;
    TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    EXPECT_FALSE(panel.isAutomationFollowsClips());
    panel.getTrackAutomationLanes().getFollowsClipsButton().onClick();
    EXPECT_TRUE(panel.isAutomationFollowsClips());
    EXPECT_TRUE(panel.getClipLaneArea().isAutomationFollowsClips());
    EXPECT_TRUE(panel.getTrackAutomationLanes().getFollowsClipsButton().getToggleState());
}

TEST(AutomationFollowsClipsTest, ClipboardVerbsCarryAutomationInOneUndoStepEach) {
    TimelineDoc doc;
    AppUndoManager undo;
    TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setUndoManager(&undo);
    panel.setAutomationFollowsClips(true);
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    AutomationLane::RangeSnapshot range;
    range.maxValue = 1.0f;
    const auto laneId = doc.addLane(track, "synth-uuid", "cutoff", range);
    const auto clip = doc.addClip(track, 0.0, 4.0, "Clip");
    doc.addBreakpoint(laneId, 1.0, 0.5);
    doc.addBreakpoint(laneId, 3.0, 0.75);
    auto beats = [&doc, laneId] {
        std::vector<double> out;
        for (const auto& p : doc.getLane(laneId)->points)
            out.push_back(p.beat);
        return out;
    };

    panel.getClipSelection().setSelection({clip});
    ASSERT_TRUE(panel.duplicateSelectedClips());
    EXPECT_EQ(beats(), (std::vector<double>{1.0, 3.0, 5.0, 7.0}));
    undo.undo();
    EXPECT_EQ(beats(), (std::vector<double>{1.0, 3.0}));

    panel.getClipSelection().setSelection({clip});
    ASSERT_TRUE(panel.repeatSelectedClips(2));
    EXPECT_EQ(beats(), (std::vector<double>{1.0, 3.0, 5.0, 7.0, 9.0, 11.0}));
    undo.undo();

    // Copy captures the span's points; paste writes them back wherever the clip lands (here beat 0,
    // no transport), even after the lane was cleared in between.
    panel.getClipSelection().setSelection({clip});
    ASSERT_TRUE(panel.copySelectedClips());
    doc.editBreakpoints(laneId, {1.0, 3.0}, {});
    ASSERT_TRUE(doc.getLane(laneId)->points.empty());
    ASSERT_TRUE(panel.pasteClipsAtPlayhead());
    EXPECT_EQ(beats(), (std::vector<double>{1.0, 3.0}));
    undo.undo();
    EXPECT_TRUE(doc.getLane(laneId)->points.empty()) << "clip and automation came in one step";

    doc.editBreakpoints(laneId, {}, {{1.0, 0.5, 0.0f, 1}});
    panel.getClipSelection().setSelection({clip});
    ASSERT_TRUE(panel.cutSelectedClips());
    EXPECT_TRUE(doc.getLane(laneId)->points.empty());
    undo.undo();
    EXPECT_EQ(beats(), (std::vector<double>{1.0}));
}
