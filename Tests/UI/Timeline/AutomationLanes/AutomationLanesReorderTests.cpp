// AutomationLanesReorderTests.cpp -- reordering lanes within their track: a real drag on a lane header through its own
// mouse handlers (the other lanes slide aside, one undo step on the drop, Esc puts everything back, a click is still a
// click), a lane never leaving its track, and Cmd+Alt+Up/Down through the focused header's and editor's key handlers.
// Headless, so the glide lands at once (the owner is not showing); the glide itself is the shared
// ReorderDragAnimator's.

#include "AutomationLanesMenuFixture.h"
#include "ShortcutManager/ShortcutManager.h"
#include <algorithm>

using namespace lane_menu_test;
using namespace automation_lanes_test;
using synth::ui::AutomationLaneHeaderComponent;

namespace {

// Track "Bass" with three open lanes (cutoff, res, detune) and track "Lead" with one, all expanded.
struct ReorderPanel : LanesPanel {
    PickHost host;
    synth::TrackId bass, lead;
    synth::LaneId cutoff, res, detune, leadLane;

    ReorderPanel() {
        bass = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lead = doc.addTrack(synth::TrackKind::Midi, "Lead");
        cutoff = addLane(bass, "cutoff");
        res = addLane(bass, "res");
        detune = addLane(bass, "detune");
        leadLane = addLane(lead, "pitch");
        host.offer("Filter 1", "Cutoff", "cutoff", {0.0f, 100.0f, 50.0f});
        panel.setTrackHeaderHost(&host);
        panel.setTrackAutomationExpanded(bass, true);
        panel.setTrackAutomationExpanded(lead, true);
    }
    ~ReorderPanel() { panel.setTrackHeaderHost(nullptr); }

    AutomationLaneHeaderComponent& header(synth::LaneId id) { return *panel.laneHeaderForTest(id); }
    synth::ui::AutomationLaneEditor& editor(synth::LaneId id) { return *panel.laneEditorForTest(id); }

    std::vector<synth::LaneId> order(synth::TrackId track) const {
        std::vector<synth::LaneId> ids;
        for (const auto& lane : doc.getTrack(track)->lanes)
            ids.push_back(lane.id);
        return ids;
    }

    // The lanes of `track` from the top of the screen down, by where their headers actually stand.
    std::vector<synth::LaneId> onScreenOrder(synth::TrackId track) {
        auto ids = order(track);
        std::stable_sort(ids.begin(), ids.end(),
                         [this](synth::LaneId x, synth::LaneId y) { return header(x).getY() < header(y).getY(); });
        return ids;
    }
};

int block(ReorderPanel& f) { return f.header(f.cutoff).getHeight(); }

// A press on `header`, then drags to each screen y in `ys`; the release is left to the caller.
struct HeaderDrag {
    AutomationLaneHeaderComponent& header;
    juce::Point<float> pressLocal;
    juce::Point<int> pressScreen;

    explicit HeaderDrag(AutomationLaneHeaderComponent& h)
        : header(h)
        , pressLocal(40.0f, (float)h.getHeight() * 0.5f)
        , pressScreen(h.localPointToGlobal(pressLocal.toInt())) {
        header.mouseDown(makeClickEvent(header, pressLocal, leftButton()));
    }
    juce::MouseEvent eventAtScreenY(int y) {
        const auto local = header.getLocalPoint(nullptr, juce::Point<int>(pressScreen.x, y)).toFloat();
        return makeDragEvent(header, local, pressLocal, leftButton());
    }
    void moveBy(int dy, int steps = 6) {
        const int from = pressScreen.y + current;
        for (int i = 1; i <= steps; ++i)
            header.mouseDrag(eventAtScreenY(from + dy * i / steps));
        current += dy;
    }
    void release() { header.mouseUp(eventAtScreenY(pressScreen.y + current)); }
    int current = 0;
};
} // namespace

TEST(AutomationLanesReorderTest, DraggingALaneHeaderDownPastItsNeighbourMovesItThereAsOneUndoStep) {
    ReorderPanel f;
    const int block = f.header(f.cutoff).getHeight();
    const auto before = f.order(f.bass);
    ASSERT_EQ(before, (std::vector<synth::LaneId>{f.cutoff, f.res, f.detune}));

    HeaderDrag drag(f.header(f.cutoff));
    drag.moveBy(block * 3 / 2); // the centre passes the middle of the next lane's slot
    EXPECT_TRUE(f.panel.getAutomationLanes().isLaneReorderActive());
    EXPECT_EQ(f.panel.getAutomationLanes().liftedLane(), f.cutoff);
    EXPECT_EQ(f.order(f.bass), before) << "nothing is committed during the drag";
    EXPECT_LT(f.header(f.res).getY(), f.header(f.cutoff).getY()) << "the neighbour slid up into the gap";
    drag.release();

    EXPECT_EQ(f.order(f.bass), (std::vector<synth::LaneId>{f.res, f.cutoff, f.detune}));
    EXPECT_EQ(f.onScreenOrder(f.bass), f.order(f.bass)) << "the headers follow the new order";
    EXPECT_FALSE(f.panel.getAutomationLanes().isLaneReorderActive());
    EXPECT_EQ(f.panel.getAutomationLanes().liftedLane(), synth::LaneId());

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.order(f.bass), before) << "ONE Cmd+Z puts it back";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(AutomationLanesReorderTest, DraggingAllTheWayDownOrUpTakesTheEndSlotAndNeverLeavesTheTrack) {
    ReorderPanel f;
    const int block = f.header(f.cutoff).getHeight();
    const auto leadBefore = f.order(f.lead);

    HeaderDrag down(f.header(f.cutoff));
    down.moveBy(block * 8); // far past Bass's last lane and into Lead's rows
    down.release();
    EXPECT_EQ(f.order(f.bass), (std::vector<synth::LaneId>{f.res, f.detune, f.cutoff}));
    EXPECT_EQ(f.order(f.lead), leadBefore) << "the other track is untouched";
    EXPECT_EQ(f.doc.getTrackForLane(f.cutoff)->id, f.bass);

    HeaderDrag up(f.header(f.cutoff));
    up.moveBy(-block * 8);
    up.release();
    EXPECT_EQ(f.order(f.bass), (std::vector<synth::LaneId>{f.cutoff, f.res, f.detune}));
}

TEST(AutomationLanesReorderTest, DroppingBackInTheSameSlotChangesNothingAndRecordsNothing) {
    ReorderPanel f;
    const auto before = f.order(f.bass);
    const auto revision = f.doc.getRevision();

    HeaderDrag drag(f.header(f.res));
    drag.moveBy(block(f) / 4); // past the threshold, short of the neighbour's middle
    drag.release();

    EXPECT_EQ(f.order(f.bass), before);
    EXPECT_EQ(f.doc.getRevision(), revision);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(AutomationLanesReorderTest, EscapeMidDragPutsEverythingBackAndTheReleaseCommitsNothing) {
    ReorderPanel f;
    const auto before = f.order(f.bass);
    const auto revision = f.doc.getRevision();
    const int startY = f.header(f.cutoff).getY();

    HeaderDrag drag(f.header(f.cutoff));
    drag.moveBy(block(f) * 2);
    ASSERT_TRUE(f.panel.getAutomationLanes().isLaneReorderActive());
    EXPECT_TRUE(f.panel.getAutomationLanes().sendLaneDragEscapeForTest());
    drag.release();

    EXPECT_FALSE(f.panel.getAutomationLanes().isLaneReorderActive());
    EXPECT_EQ(f.order(f.bass), before);
    EXPECT_EQ(f.doc.getRevision(), revision);
    EXPECT_EQ(f.header(f.cutoff).getY(), startY) << "back in its own slot";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(AutomationLanesReorderTest, APressThatNeverBecomesADragIsStillAClick) {
    ReorderPanel f;
    PickerCapture capture;
    auto& header = f.header(f.cutoff);
    f.host.offer("Filter 1", "Resonance", "res2", {0.0f, 1.0f, 0.5f});
    const auto name = header.getNameAreaForTest().getCentre().toFloat();

    header.mouseDown(makeClickEvent(header, name, leftButton()));
    header.mouseDrag(makeDragEvent(header, name + juce::Point<float>(0.0f, 2.0f), name, leftButton()));
    header.mouseUp(makeClickEvent(header, name, leftButton()));

    EXPECT_NE(capture.picker, nullptr) << "under the 4 px threshold the press is the name click";
    EXPECT_FALSE(f.panel.getAutomationLanes().isLaneReorderActive());
}

TEST(AutomationLanesReorderTest, ADragDoesNotOpenTheNamePickerOnRelease) {
    ReorderPanel f;
    PickerCapture capture;
    auto& header = f.header(f.cutoff);

    HeaderDrag drag(header);
    drag.moveBy(block(f) * 3 / 2);
    drag.release();

    EXPECT_EQ(capture.picker, nullptr);
}

TEST(AutomationLanesReorderTest, CmdAltDownAndUpOnTheFocusedHeaderMoveTheLaneOneSlotEachAsOneUndoStep) {
    ReorderPanel f;
    const juce::ModifierKeys commandAlt(juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier);
    const juce::KeyPress down(juce::KeyPress::downKey, commandAlt, 0);
    const juce::KeyPress up(juce::KeyPress::upKey, commandAlt, 0);
    auto& header = f.header(f.cutoff);
    const auto title = header.getRecordModeCombo().getTitle();

    // Through the key listener the record-mode combo consults before its own Up/Down handling.
    EXPECT_TRUE(header.keyPressed(down, &header.getRecordModeCombo()));
    EXPECT_EQ(f.order(f.bass), (std::vector<synth::LaneId>{f.res, f.cutoff, f.detune}));
    EXPECT_EQ(f.onScreenOrder(f.bass), f.order(f.bass));
    EXPECT_EQ(header.getRecordModeCombo().getTitle(), title) << "the lane keeps its accessible name";
    EXPECT_EQ(f.panel.laneHeaderForTest(f.cutoff), &header) << "the same header, not a rebuilt one";

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.order(f.bass), (std::vector<synth::LaneId>{f.cutoff, f.res, f.detune})) << "one step";
    EXPECT_FALSE(f.undo.canUndo());

    EXPECT_TRUE(header.keyPressed(up, &header.getRecordModeCombo()))
        << "consumed at the top, so the combo never sees it";
    EXPECT_EQ(f.order(f.bass), (std::vector<synth::LaneId>{f.cutoff, f.res, f.detune}));
    EXPECT_FALSE(f.undo.canUndo()) << "nothing moved, nothing recorded";
}

TEST(AutomationLanesReorderTest, CmdAltDownOnTheFocusedEditorMovesTheLaneAndKeepsPointKeysForThePoints) {
    ReorderPanel f;
    const juce::ModifierKeys commandAlt(juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier);
    auto& editor = f.editor(f.res);

    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::downKey, commandAlt, 0)));
    EXPECT_EQ(f.order(f.bass), (std::vector<synth::LaneId>{f.cutoff, f.detune, f.res}));
    EXPECT_EQ(f.onScreenOrder(f.bass), f.order(f.bass));

    EXPECT_FALSE(editor.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(), 0)))
        << "a bare arrow with no points selected is not the lane's";
}

TEST(AutomationLanesReorderTest, ARebindOfMoveLaneUpIsHonoured) {
    ReorderPanel f;
    ShortcutManager manager;
    f.panel.setShortcutManager(&manager);
    manager.setBinding("timelineMoveLaneUp", juce::KeyPress('k', juce::ModifierKeys::altModifier, 0));
    auto& header = f.header(f.res);

    EXPECT_TRUE(
        header.keyPressed(juce::KeyPress('k', juce::ModifierKeys::altModifier, 0), &header.getRecordModeCombo()));
    EXPECT_EQ(f.order(f.bass), (std::vector<synth::LaneId>{f.res, f.cutoff, f.detune}));
    EXPECT_FALSE(header.keyPressed(
        juce::KeyPress(juce::KeyPress::upKey,
                       juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier), 0),
        &header.getRecordModeCombo()))
        << "the old key is free once it is rebound";
    f.panel.setShortcutManager(nullptr);
}
