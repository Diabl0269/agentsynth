// The piano roll's velocity / CC lane strip (PianoRollControllerLanes): layout and collapse, lane
// selection, every gesture, and the one-undo-step-per-gesture contract. See
// docs/timeline/piano-roll-lanes.md.

#include "PianoRollTestHelpers.h"
#include "Timeline/TimelineSnapshot.h"
#include "UI/PianoRoll/PianoRollControllerLanes/ControllerLaneEdits.h"
#include "UI/PianoRoll/PianoRollControllerLanes/PianoRollControllerLanes.h"

using synth::ControllerPoint;
using synth::ui::PianoRollControllerLanes;

namespace {

struct LaneFixture : PianoRollFixture {
    ClipId clip;
    std::vector<NoteId> notes;

    LaneFixture() {
        roll.setSize(900, 400);
        const auto track = doc.addTrack(TrackKind::Midi, "T");
        clip = doc.addClip(track, 0.0, 8.0, "c");
        for (int i = 0; i < 4; ++i)
            notes.push_back(doc.addNote(clip, makeNote((double)i, 60 + i)));
        open(clip);
        setSnap(*this, TimelineViewState::Snap::Quarter);
        roll.setControllerLanesVisible(true);
    }

    PianoRollControllerLanes& lanes() { return roll.getControllerLanes(); }

    // A strip-local point at an ABSOLUTE beat and a 0..127 value.
    juce::Point<float> at(double absBeat, double value) {
        return {(float)roll.beatToX(absBeat), (float)lanes().yForValue(value)};
    }

    void drag(std::initializer_list<juce::Point<float>> path, int mods = 0) {
        auto& l = lanes();
        const auto first = *path.begin();
        l.mouseDown(leftClick(l, first, mods));
        juce::Point<float> last = first;
        for (auto it = path.begin() + 1; it != path.end(); ++it) {
            l.mouseDrag(leftDrag(l, *it, first, mods));
            last = *it;
        }
        l.mouseUp(leftDrag(l, last, first, mods));
    }

    void click(juce::Point<float> pos, int mods = 0) {
        auto& l = lanes();
        l.mouseDown(leftClick(l, pos, mods));
        l.mouseUp(leftClick(l, pos, mods));
    }

    int velocity(int i) { return doc.getNote(notes[(size_t)i])->velocity; }

    // Undo exactly once and report whether anything is still undoable (i.e. was the gesture ONE step).
    bool undoOnceLeavesNothing() {
        if (!undo.canUndo())
            return false;
        undo.undo();
        return !undo.canUndo();
    }
};

} // namespace

// ---- Layout / collapse ---------------------------------------------------------------------------

TEST(PianoRollControllerLanesTest, CollapsedLayoutIsExactlyTheOldOne) {
    PianoRollFixture f;
    f.roll.setSize(900, 400);
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    f.open(f.doc.addClip(track, 0.0, 4.0, "c"));
    ASSERT_FALSE(f.roll.areControllerLanesVisible()) << "collapsed by default";
    const auto grid = f.roll.getNoteGridBounds();
    const auto keys = f.roll.getKeysColumnBounds();
    EXPECT_EQ(f.roll.canvasBottom(), 400);

    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getLanesButtonBounds())));
    ASSERT_TRUE(f.roll.areControllerLanesVisible()) << "the Lanes chip toggles the strip";
    const auto strip = f.roll.getControllerLanes().getBounds();
    EXPECT_EQ(strip, juce::Rectangle<int>(0, 400 - PianoRollControllerLanes::kStripHeight, 900,
                                          PianoRollControllerLanes::kStripHeight));
    EXPECT_EQ(f.roll.getNoteGridBounds().getBottom(), strip.getY());
    EXPECT_EQ(f.roll.canvasBottom(), strip.getY());
    EXPECT_EQ(f.roll.getControllerLanes().getSelectorBounds().getWidth(), f.roll.leftGutterWidth());

    f.roll.toggleControllerLanes();
    EXPECT_FALSE(f.roll.areControllerLanesVisible());
    EXPECT_EQ(f.roll.getNoteGridBounds(), grid);
    EXPECT_EQ(f.roll.getKeysColumnBounds(), keys);
    EXPECT_FALSE(f.undo.canUndo()) << "showing / hiding the strip is view state, never an edit";
}

TEST(PianoRollControllerLanesTest, StripNeverTakesKeyboardFocus) {
    LaneFixture f;
    EXPECT_FALSE(f.lanes().getWantsKeyboardFocus()) << "J / Q / Delete stay with the roll";
    EXPECT_FALSE(f.lanes().getMouseClickGrabsKeyboardFocus());
}

// ---- Lane selection ------------------------------------------------------------------------------

TEST(PianoRollControllerLanesTest, LaneMenuOffersTheNamedControllersAndExistingLanes) {
    LaneFixture f;
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 74, {{0.0, 10.0, 1}}));
    const auto menu = f.lanes().buildLaneMenu();
    std::set<int> ids;
    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
        ids.insert(it.getItem().itemID);
    for (const int lane : {PianoRollControllerLanes::kVelocityLane, 1, 2, 11, 64, 74, 0, 127})
        EXPECT_EQ(ids.count(PianoRollControllerLanes::laneMenuItemId(lane)), 1u) << "lane " << lane;

    EXPECT_EQ(f.lanes().getSelectedLane(), PianoRollControllerLanes::kVelocityLane) << "velocity is the default";
    f.lanes().handleLaneMenuResult(PianoRollControllerLanes::laneMenuItemId(11));
    EXPECT_EQ(f.lanes().getSelectedLane(), 11);
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 11), nullptr) << "picking a lane creates nothing until a stroke";
    f.lanes().selectLane(200);
    EXPECT_EQ(f.lanes().getSelectedLane(), 11) << "an out-of-range id is ignored";
    EXPECT_EQ(synth::ui::lanes::laneName(64), juce::String("CC64 Sustain"));
}

// ---- Velocity gestures ---------------------------------------------------------------------------

TEST(PianoRollControllerLanesTest, BarDragSetsThatNoteOnlyInOneUndoStep) {
    LaneFixture f;
    f.drag({f.at(1.0, 100.0), f.at(1.0, 80.0), f.at(1.0, 40.0)});
    EXPECT_NEAR(f.velocity(1), 40, 1);
    EXPECT_EQ(f.velocity(0), 100);
    EXPECT_EQ(f.velocity(2), 100);
    EXPECT_TRUE(f.undoOnceLeavesNothing());
    EXPECT_EQ(f.velocity(1), 100);
}

TEST(PianoRollControllerLanesTest, PreviewDoesNotTouchTheDocUntilMouseUp) {
    LaneFixture f;
    auto& l = f.lanes();
    const auto from = f.at(2.0, 100.0);
    l.mouseDown(leftClick(l, from));
    l.mouseDrag(leftDrag(l, f.at(2.0, 20.0), from));
    EXPECT_TRUE(l.isGestureActiveForTest());
    EXPECT_NEAR(l.displayedVelocityForTest(f.notes[2]), 20, 1);
    EXPECT_EQ(f.velocity(2), 100) << "preview only";
    EXPECT_FALSE(f.undo.canUndo());
    l.mouseUp(leftDrag(l, f.at(2.0, 20.0), from));
    EXPECT_NEAR(f.velocity(2), 20, 1);
}

TEST(PianoRollControllerLanesTest, ShiftLineRampsSelectedNotesAsOneUndoStepAndPlaysBack) {
    LaneFixture f;
    f.roll.getSelectionForTest().setSelection({f.notes[1], f.notes[2], f.notes[3]});
    // Ramp from 10 at beat 0 to 100 at beat 3: note 0 is outside the selection and must not move.
    f.drag({f.at(0.0, 10.0), f.at(1.5, 50.0), f.at(3.0, 100.0)}, juce::ModifierKeys::shiftModifier);
    EXPECT_EQ(f.velocity(0), 100);
    EXPECT_NEAR(f.velocity(1), 40, 2);
    EXPECT_NEAR(f.velocity(2), 70, 2);
    EXPECT_NEAR(f.velocity(3), 100, 2);

    // Playback: the flattened snapshot (what Track In plays) carries the new velocities.
    const auto snapshot = synth::TimelineSnapshot::buildFrom(f.doc);
    ASSERT_EQ(snapshot->notes.size(), 4u);
    EXPECT_EQ(snapshot->notes[1].velocity, f.velocity(1));
    EXPECT_EQ(snapshot->notes[3].velocity, f.velocity(3));

    EXPECT_TRUE(f.undoOnceLeavesNothing()) << "the whole ramp is ONE undo step";
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(f.velocity(i), 100);
}

TEST(PianoRollControllerLanesTest, FreehandSetsEveryNoteItPassesEvenOnAFastDrag) {
    LaneFixture f;
    // Start between bars (so it is not a bar grab) and jump straight across all four starts.
    f.drag({f.at(0.5, 30.0), f.at(3.5, 30.0)});
    EXPECT_EQ(f.velocity(0), 100) << "beat 0 lies before the stroke";
    for (int i = 1; i < 4; ++i)
        EXPECT_NEAR(f.velocity(i), 30, 1) << "note " << i;
    EXPECT_TRUE(f.undoOnceLeavesNothing());
}

TEST(PianoRollControllerLanesTest, VelocityIsClampedToOneAtTheFloor) {
    LaneFixture f;
    const auto area = f.lanes().getValueArea();
    const juce::Point<float> below{(float)f.roll.beatToX(1.0), (float)area.getBottom() + 3.0f};
    f.drag({f.at(1.0, 100.0), below});
    EXPECT_EQ(f.velocity(1), 1) << "0 is not a note-on";
}

TEST(PianoRollControllerLanesTest, ResetVelocitiesActsOnTheSelection) {
    LaneFixture f;
    ASSERT_TRUE(f.doc.setNoteVelocities({{f.notes[0], 5}, {f.notes[1], 5}}));
    f.roll.getSelectionForTest().setSelection({f.notes[1]});
    f.lanes().performContextAction(PianoRollControllerLanes::ResetVelocities, juce::Point<int>{});
    EXPECT_EQ(f.velocity(0), 5);
    EXPECT_EQ(f.velocity(1), 100);
}

// ---- CC gestures ---------------------------------------------------------------------------------

TEST(PianoRollControllerLanesTest, CcClickAddsOneSnappedPointAndCreatesTheLane) {
    LaneFixture f;
    f.lanes().selectLane(1);
    f.click(f.at(2.1, 64.0)); // snaps to the quarter grid -> beat 2
    const auto* lane = f.doc.getControllerLane(f.clip, 1);
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 1u);
    EXPECT_DOUBLE_EQ(lane->points[0].beat, 2.0);
    EXPECT_NEAR(lane->points[0].value, 64.0, 1.5);
    EXPECT_TRUE(f.undoOnceLeavesNothing()) << "lane creation + point = one undo step";
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 1), nullptr);
}

TEST(PianoRollControllerLanesTest, CcFreehandIsThinnedAndReplacesTheSpan) {
    LaneFixture f;
    f.lanes().selectLane(11);
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 11, {{1.5, 10.0, 1}, {6.0, 90.0, 1}}));
    // A straight rising stroke from beat 1 to beat 3: thinning keeps its two ends only.
    f.drag({f.at(1.0, 0.0), f.at(1.5, 25.0), f.at(2.0, 50.0), f.at(2.5, 75.0), f.at(3.0, 100.0)});
    const auto& points = f.doc.getControllerLane(f.clip, 11)->points;
    ASSERT_EQ(points.size(), 3u) << "stroke ends + the untouched point at beat 6";
    EXPECT_NEAR(points[0].beat, 1.0, 0.05);
    EXPECT_NEAR(points[1].beat, 3.0, 0.05);
    EXPECT_DOUBLE_EQ(points[2].beat, 6.0);
    EXPECT_TRUE(f.undo.canUndo());
}

TEST(PianoRollControllerLanesTest, CcShiftLineMoveAndAltEraseAreEachOneUndoStep) {
    LaneFixture f;
    f.lanes().selectLane(1);
    f.drag({f.at(1.0, 0.0), f.at(4.0, 127.0)}, juce::ModifierKeys::shiftModifier);
    auto points = f.doc.getControllerLane(f.clip, 1)->points;
    ASSERT_EQ(points.size(), 2u);
    EXPECT_DOUBLE_EQ(points[0].beat, 1.0);
    EXPECT_DOUBLE_EQ(points[1].beat, 4.0);

    // Drag the right handle to beat 5.
    f.drag({f.at(4.0, 127.0), f.at(5.0, 60.0)});
    points = f.doc.getControllerLane(f.clip, 1)->points;
    ASSERT_EQ(points.size(), 2u);
    EXPECT_DOUBLE_EQ(points[1].beat, 5.0);
    EXPECT_NEAR(points[1].value, 60.0, 1.5);

    // Alt+click erases the left handle.
    f.click(f.at(1.0, 0.0), juce::ModifierKeys::altModifier);
    points = f.doc.getControllerLane(f.clip, 1)->points;
    ASSERT_EQ(points.size(), 1u);
    EXPECT_DOUBLE_EQ(points[0].beat, 5.0);

    // Three gestures, three undo steps.
    f.undo.undo();
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 1)->points.size(), 2u);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getControllerLane(f.clip, 1)->points[1].beat, 4.0);
    f.undo.undo();
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 1), nullptr);
}

TEST(PianoRollControllerLanesTest, SustainPointsHoldAndContextMenuEditsTheLane) {
    LaneFixture f;
    f.lanes().selectLane(64);
    f.click(f.at(1.0, 127.0));
    ASSERT_NE(f.doc.getControllerLane(f.clip, 64), nullptr);
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 64)->points[0].curve, static_cast<int>(synth::BreakpointCurve::Hold));

    const auto handle = f.at(1.0, 127.0).toInt();
    const auto menu = f.lanes().buildContextMenu(handle);
    EXPECT_GE(menu.getNumItems(), 5) << "point actions + clear + remove";
    f.lanes().performContextAction(PianoRollControllerLanes::CurveLinear, handle);
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 64)->points[0].curve, static_cast<int>(synth::BreakpointCurve::Linear));
    f.lanes().performContextAction(PianoRollControllerLanes::ClearLane, handle);
    ASSERT_NE(f.doc.getControllerLane(f.clip, 64), nullptr);
    EXPECT_TRUE(f.doc.getControllerLane(f.clip, 64)->points.empty());
    f.lanes().performContextAction(PianoRollControllerLanes::RemoveLane, handle);
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 64), nullptr);
}

TEST(PianoRollControllerLanesTest, ClosingTheRollCancelsALiveGesture) {
    LaneFixture f;
    auto& l = f.lanes();
    const auto from = f.at(1.0, 100.0);
    l.mouseDown(leftClick(l, from));
    ASSERT_TRUE(l.isGestureActiveForTest());
    f.roll.closeRoll();
    EXPECT_FALSE(l.isGestureActiveForTest());
    l.mouseUp(leftDrag(l, f.at(1.0, 10.0), from));
    EXPECT_FALSE(f.undo.canUndo()) << "nothing is committed into a clip that is no longer open";
}

// ---- Pure edit maths -----------------------------------------------------------------------------

TEST(ControllerLaneEditsTest, ReplaceSpanAndVelocityLine) {
    const std::vector<ControllerPoint> existing = {{0.0, 1.0, 1}, {1.0, 2.0, 1}, {2.0, 3.0, 1}, {3.0, 4.0, 1}};
    const auto out = synth::ui::lanes::replaceSpan(existing, 1.0, 2.0, {{1.5, 99.0, 0}});
    ASSERT_EQ(out.size(), 3u);
    EXPECT_DOUBLE_EQ(out[1].beat, 1.5);
    EXPECT_EQ(out[1].curve, 0);

    synth::Clip clip;
    clip.lengthBeats = 4.0;
    for (int i = 0; i < 5; ++i) {
        synth::MidiNote n;
        n.id = NoteId{i + 1};
        n.startBeat = i; // the note at beat 4 is past the clip end
        clip.notes.push_back(n);
    }
    const auto ramp = synth::ui::lanes::velocityLine(clip, {}, 0.0, 0.0, 4.0, 200.0);
    ASSERT_EQ(ramp.size(), 4u) << "notes at or past the clip end are skipped";
    EXPECT_EQ(ramp[0].second, 1) << "clamped to 1";
    EXPECT_EQ(ramp[2].second, 100);
    EXPECT_EQ(ramp[3].second, 127) << "clamped to 127";
}

// ---- Visual feedback -----------------------------------------------------------------------------

TEST(PianoRollControllerLanesTest, VelocityDragRecoloursTheNoteLive) {
    LaneFixture f;
    auto& l = f.lanes();
    const auto before = f.roll.notePaintFor(*f.doc.getNote(f.notes[2])).fill;
    const auto from = f.at(2.0, 100.0);
    l.mouseDown(leftClick(l, from));
    l.mouseDrag(leftDrag(l, f.at(2.0, 10.0), from));
    // The roll's note body is coloured by the PREVIEW, through the same resolver as every note.
    ASSERT_TRUE(l.previewVelocityFor(f.notes[2]).has_value());
    auto previewed = *f.doc.getNote(f.notes[2]);
    previewed.velocity = *l.previewVelocityFor(f.notes[2]);
    EXPECT_NE(f.roll.notePaintFor(previewed).fill, before);
    EXPECT_FALSE(l.previewVelocityFor(f.notes[1]).has_value()) << "untouched notes keep their own velocity";
    EXPECT_EQ(l.getReadoutTextForTest(), juce::String(*l.previewVelocityFor(f.notes[2])))
        << "the readout shows the value under the pointer";
    l.mouseUp(leftDrag(l, f.at(2.0, 10.0), from));
    EXPECT_FALSE(l.previewVelocityFor(f.notes[2]).has_value());
    EXPECT_TRUE(l.getReadoutTextForTest().isEmpty());
}

TEST(PianoRollControllerLanesTest, HoverTracksBarsAndHandlesWithoutAGesture) {
    LaneFixture f;
    auto& l = f.lanes();
    l.mouseMove(hover(l, f.at(3.0, 50.0)));
    ASSERT_TRUE(l.getHoveredBarBeatForTest().has_value());
    EXPECT_DOUBLE_EQ(*l.getHoveredBarBeatForTest(), 3.0);
    l.mouseExit(hover(l, {-50.0f, -50.0f}));
    EXPECT_FALSE(l.getHoveredBarBeatForTest().has_value());

    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 1, {{2.0, 64.0, 1}}));
    l.selectLane(1);
    l.mouseMove(hover(l, f.at(2.0, 64.0)));
    ASSERT_TRUE(l.getHoveredHandleForTest().has_value());
    EXPECT_EQ(*l.getHoveredHandleForTest(), 0u);
    l.mouseMove(hover(l, f.at(5.0, 10.0)));
    EXPECT_FALSE(l.getHoveredHandleForTest().has_value());
}

TEST(PianoRollControllerLanesTest, AStaleContextTargetDoesNothing) {
    // Review regression: the async menu answer used to re-hit-test the pointer, so after an undo it
    // could act on a DIFFERENT point.
    LaneFixture f;
    f.lanes().selectLane(1);
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 1, {{2.0, 64.0, 1}}));
    const auto target = f.lanes().contextTargetAt(f.at(2.0, 64.0).toInt());
    ASSERT_TRUE(target.pointBeat.has_value());
    // Before the answer arrives the point moves (another edit / an undo).
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 1, {{2.0 + 1.0e-3, 64.0, 1}}));
    f.lanes().performContextAction(PianoRollControllerLanes::DeletePoint, target);
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 1)->points.size(), 1u) << "the captured point is gone: no-op";

    const auto fresh = f.lanes().contextTargetAt(f.at(2.0, 64.0).toInt());
    f.lanes().selectLane(11);
    f.lanes().performContextAction(PianoRollControllerLanes::ClearLane, fresh);
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 1)->points.size(), 1u) << "a different lane is shown now: no-op";
    f.lanes().selectLane(1);
    f.lanes().performContextAction(PianoRollControllerLanes::DeletePoint, fresh);
    EXPECT_TRUE(f.doc.getControllerLane(f.clip, 1)->points.empty()) << "a still-valid target acts";
}
