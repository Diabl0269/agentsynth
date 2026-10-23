// AutomationLanesPointSelectionTests.cpp -- selecting automation points like clips and notes: click, Shift-click,
// a box on empty space, Escape, moving and deleting the selection as one undo step, arrow nudging, the keyboard
// cursor, copy/paste and how the selection follows the doc. Real events on the real panel's editor.

#include "../../Layout/KeyGlide/KeyGlideTestHost.h"
#include "AutomationLanesTestFixture.h"
#include "UI/Layout/PointEditorDuplicate.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;

namespace {

// Three points at beats 1, 2 and 3 (values 20, 60, 40) on an open lane; snap is a quarter note (one beat).
struct SelectLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    SelectLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        editor->valueToText = [](double v) { return juce::String(v, 1) + " units"; };
        doc.addBreakpoint(lane, 1.0, 20.0);
        doc.addBreakpoint(lane, 2.0, 60.0);
        doc.addBreakpoint(lane, 3.0, 40.0);
    }

    const synth::AutomationLane& theLane() const { return *doc.getLane(lane); }
    juce::Point<float> at(double beat, double value) {
        return {(float)panel.getViewState().beatToX(beat), (float)editor->valueToY(value)};
    }
    juce::Point<float> top(double beat) { return {(float)panel.getViewState().beatToX(beat), 2.0f}; }

    void click(juce::Point<float> p, int mods = 0) {
        editor->mouseDown(makeClickEvent(*editor, p, leftButton(mods)));
        editor->mouseUp(makeClickEvent(*editor, p, leftButton(mods)));
    }
    void drag(juce::Point<float> from, juce::Point<float> to, int mods = 0) {
        dragAcross(*editor, from, to, 6, leftButton(mods));
    }
    bool key(const juce::KeyPress& k) { return editor->keyPressed(k); }
    std::vector<double> selected() const { return editor->getPointSelection().getSelected(); }
    std::vector<double> beats() const {
        std::vector<double> out;
        for (const auto& p : theLane().points)
            out.push_back(p.beat);
        return out;
    }
    std::vector<double> values() const {
        std::vector<double> out;
        for (const auto& p : theLane().points)
            out.push_back(p.value);
        return out;
    }
};

using Beats = std::vector<double>;

struct ReducedMotionGuard {
    ReducedMotionGuard() { synth::ui::setReducedMotionForTest(true); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

TEST(AutomationLanesPointSelectionTest, ClickingAPointSelectsItAndReplacesTheSelection) {
    SelectLane f;
    ASSERT_NE(f.editor, nullptr);
    EXPECT_TRUE(f.selected().empty());
    const auto rev = f.doc.getRevision();

    f.click(f.at(1.0, 20.0));
    EXPECT_EQ(f.selected(), Beats({1.0}));
    f.click(f.at(2.0, 60.0));
    EXPECT_EQ(f.selected(), Beats({2.0})) << "a plain click on another point replaces the selection";
    EXPECT_EQ(f.doc.getRevision(), rev) << "selecting never touches the doc";
}

TEST(AutomationLanesPointSelectionTest, AClickOnAnAlreadySelectedPointKeepsTheGroup) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0});
    f.click(f.at(2.0, 60.0));
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0})) << "so the group can be dragged together";
}

TEST(AutomationLanesPointSelectionTest, ShiftClickAddsAndTogglesWithoutDragging) {
    SelectLane f;
    const auto rev = f.doc.getRevision();
    f.click(f.at(1.0, 20.0));
    f.click(f.at(2.0, 60.0), juce::ModifierKeys::shiftModifier);
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0}));
    EXPECT_FALSE(f.editor->isDragActiveForTest()) << "a modifier-click never starts a drag";
    f.click(f.at(1.0, 20.0), juce::ModifierKeys::shiftModifier);
    EXPECT_EQ(f.selected(), Beats({2.0})) << "a second Shift-click removes it";
    f.click(f.at(3.0, 40.0), juce::ModifierKeys::commandModifier);
    EXPECT_EQ(f.selected(), Beats({2.0, 3.0})) << "Cmd toggles too, like clips and notes";
    EXPECT_EQ(f.doc.getRevision(), rev);
}

TEST(AutomationLanesPointSelectionTest, DraggingABoxOnEmptySpaceSelectsThePointsInside) {
    SelectLane f;
    f.click(f.at(3.0, 40.0));
    ASSERT_EQ(f.selected(), Beats({3.0}));

    f.drag(f.top(0.5), f.at(2.5, 10.0));
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0})) << "a plain box replaces the selection";

    f.drag(f.top(2.8), f.at(3.4, 10.0), juce::ModifierKeys::shiftModifier);
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0, 3.0})) << "a Shift box adds to it";
    EXPECT_FALSE(f.editor->getPointSelection().isBoxActive()) << "the release ends the box";
}

TEST(AutomationLanesPointSelectionTest, APressOnEmptySpaceThatNeverDragsDeselectsAndDoubleClickStillAdds) {
    SelectLane f;
    f.click(f.at(1.0, 20.0));
    f.click(f.top(5.0));
    EXPECT_TRUE(f.selected().empty());

    const auto count = f.theLane().points.size();
    f.editor->mouseDoubleClick(makeClickEvent(*f.editor, f.top(5.0), leftButton()));
    EXPECT_EQ(f.theLane().points.size(), count + 1) << "double-click on empty space still adds a point";
}

TEST(AutomationLanesPointSelectionTest, AFarPressBetweenTwoPointsBoxSelectsInsteadOfScrubbingTension) {
    SelectLane f;
    // Beat 1.5 sits on the segment 20 -> 60: a press well above the line is empty space, not a tension scrub.
    f.editor->mouseDown(makeClickEvent(*f.editor, f.top(1.5), leftButton()));
    EXPECT_FALSE(f.editor->isDragActiveForTest());
    f.editor->mouseUp(makeClickEvent(*f.editor, f.top(1.5), leftButton()));

    // A press on the line itself still scrubs the segment's tension.
    f.editor->mouseDown(makeClickEvent(*f.editor, f.at(1.5, 40.0), leftButton()));
    EXPECT_TRUE(f.editor->isDragActiveForTest());
    f.editor->mouseUp(makeClickEvent(*f.editor, f.at(1.5, 40.0), leftButton()));
}

TEST(AutomationLanesPointSelectionTest, EscapeClearsTheSelectionAndOnlyConsumesTheKeyWhenSomethingWasSelected) {
    SelectLane f;
    EXPECT_FALSE(f.key(juce::KeyPress(juce::KeyPress::escapeKey))) << "nothing selected: the key falls through";
    f.click(f.at(2.0, 60.0));
    ASSERT_FALSE(f.selected().empty());
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(f.selected().empty());
    EXPECT_FALSE(f.key(juce::KeyPress(juce::KeyPress::escapeKey)));
}

TEST(AutomationLanesPointSelectionTest, SelectAllTakesEveryPointOfTheLane) {
    SelectLane f;
    EXPECT_TRUE(f.editor->selectAllPoints());
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0, 3.0}));
    f.doc.removeBreakpoint(f.lane, 1.0);
    f.doc.removeBreakpoint(f.lane, 2.0);
    f.doc.removeBreakpoint(f.lane, 3.0);
    EXPECT_FALSE(f.editor->selectAllPoints()) << "an empty lane has nothing to select";
}

TEST(AutomationLanesPointSelectionTest, DraggingASelectedPointMovesTheWholeSelectionAsOneUndoStep) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
    const auto rev = f.doc.getRevision();

    // The middle point goes one beat right and 20 units up; the others follow by the same deltas.
    f.drag(f.at(2.0, 60.0), f.at(3.0, 80.0));
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one doc mutation for the whole gesture";
    EXPECT_EQ(f.beats(), Beats({2.0, 3.0, 4.0}));
    const auto values = f.values();
    ASSERT_EQ(values.size(), 3u);
    EXPECT_NEAR(values[0], 40.0, 1.0);
    EXPECT_NEAR(values[1], 80.0, 1.0);
    EXPECT_NEAR(values[2], 60.0, 1.0);
    EXPECT_EQ(f.selected(), Beats({2.0, 3.0, 4.0})) << "the moved points stay selected";

    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0})) << "one Cmd+Z restores all of them";
    EXPECT_NEAR(f.values()[0], 20.0, 1e-6);
    EXPECT_NEAR(f.values()[1], 60.0, 1e-6);
    EXPECT_NEAR(f.values()[2], 40.0, 1e-6);
}

TEST(AutomationLanesPointSelectionTest, AMovedSelectionClampsEachValueAndStopsAtBeatZeroAsABlock) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
    // Grab the lowest-valued-after-move point and throw it far up and far left: the block stops with its first
    // point at beat 0, and every value rides the same delta, pinned at the top on its own.
    f.drag(f.at(3.0, 40.0), {f.at(-5.0, 100.0).x, -50.0f});
    EXPECT_EQ(f.beats(), Beats({0.0, 1.0, 2.0})) << "same delta for all, order and spacing kept";
    EXPECT_NEAR(f.values()[0], 80.0, 1e-6) << "20 + 60";
    EXPECT_NEAR(f.values()[1], 100.0, 1e-6) << "60 + 60 would be 120: clamped";
    EXPECT_NEAR(f.values()[2], 100.0, 1e-6);
}

TEST(AutomationLanesPointSelectionTest, DraggingAnUnselectedPointSelectsItAndMovesOnlyIt) {
    SelectLane f;
    f.click(f.at(1.0, 20.0));
    f.drag(f.at(3.0, 40.0), f.at(4.0, 40.0));
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 4.0}));
    EXPECT_EQ(f.selected(), Beats({4.0}));
}

TEST(AutomationLanesPointSelectionTest, DeleteAndBackspaceRemoveTheSelectionAsOneUndoStep) {
    for (int keyCode : {juce::KeyPress::deleteKey, juce::KeyPress::backspaceKey}) {
        SelectLane f;
        EXPECT_FALSE(f.key(juce::KeyPress(keyCode))) << "nothing selected: not consumed";
        f.editor->getPointSelection().setSelection({1.0, 3.0});
        const auto rev = f.doc.getRevision();

        EXPECT_TRUE(f.key(juce::KeyPress(keyCode)));
        EXPECT_EQ(f.doc.getRevision(), rev + 1);
        EXPECT_EQ(f.beats(), Beats({2.0}));
        EXPECT_TRUE(f.selected().empty());

        f.undo.undo();
        EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0})) << "one undo brings both back";
    }
}

TEST(AutomationLanesPointSelectionTest, ArrowKeysNudgeTheSelectionByAGridStepAndAFractionOfTheRange) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({2.0});
    const auto rev = f.doc.getRevision();
    const auto alt = juce::ModifierKeys::altModifier;

    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, alt, 0)));
    EXPECT_EQ(f.beats(), Beats({1.0, 3.0})) << "the nudged point lands on the existing point's beat and replaces it";
    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0}));

    f.editor->getPointSelection().setSelection({2.0});
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_NEAR(f.theLane().points[1].value, 61.0, 1e-6) << "1% of the 0..100 range";
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(f.theLane().points[1].value, 51.0, 1e-6) << "Shift is ten times as far";
    EXPECT_EQ(f.selected(), Beats({2.0}));
    EXPECT_GT(f.doc.getRevision(), rev);
}

TEST(AutomationLanesPointSelectionTest, ANudgeStopsTheWholeBlockAtBeatZero) {
    SelectLane f;
    f.doc.addBreakpoint(f.lane, 0.5, 10.0);
    f.editor->getPointSelection().setSelection({0.5, 1.0});
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0)));
    EXPECT_EQ(f.beats(), Beats({0.0, 0.5, 2.0, 3.0})) << "both move by the 0.5 the first one had left, not 1";
}

TEST(AutomationLanesPointSelectionTest, TheKeyboardCursorStepsFromPointToPointAndSelectsIt) {
    SelectLane f;
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey)))
        << "with nothing selected the first press picks the first point";
    EXPECT_EQ(f.selected(), Beats({1.0}));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(f.selected(), Beats({2.0}));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(f.selected(), Beats({3.0})) << "it stays on the last point";
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(f.selected(), Beats({2.0}));
    EXPECT_EQ(f.editor->getPointSelection().getCursor(), std::optional<double>(2.0));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.beats(), Beats({1.0, 3.0})) << "Delete removes the cursor point";
}

TEST(AutomationLanesPointSelectionTest, ShiftRightAndLeftExtendTheSelectionOnePointAtATime) {
    SelectLane f;
    const auto shift = juce::ModifierKeys::shiftModifier;
    f.click(f.at(2.0, 60.0));
    ASSERT_EQ(f.selected(), Beats({2.0}));
    const auto rev = f.doc.getRevision();

    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, shift, 0)));
    EXPECT_EQ(f.selected(), Beats({2.0, 3.0}));
    EXPECT_EQ(f.editor->getPointSelection().getCursor(), std::optional<double>(3.0));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, shift, 0))) << "consumed at the last point too";
    EXPECT_EQ(f.selected(), Beats({2.0, 3.0}));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::leftKey, shift, 0)));
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0, 3.0})) << "it reaches past the selection's left end";
    EXPECT_EQ(f.editor->getPointSelection().getCursor(), std::optional<double>(1.0));
    EXPECT_EQ(f.doc.getRevision(), rev) << "selecting never touches the doc";
}

TEST(AutomationLanesPointSelectionTest, ShiftRightWithNothingSelectedPicksTheFirstPoint) {
    SelectLane f;
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.selected(), Beats({1.0}));
}

TEST(AutomationLanesPointSelectionTest, DuplicateCopiesTheSelectionPastItsLastPointAsOneUndoStep) {
    SelectLane f;
    EXPECT_FALSE(f.editor->duplicateSelectedPoints()) << "nothing selected, nothing duplicated";
    f.editor->getPointSelection().setSelection({2.0, 3.0});
    const auto rev = f.doc.getRevision();
    const double tick = synth::ui::tickBeats();

    ASSERT_TRUE(f.editor->duplicateSelectedPoints());
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0, 3.0 + tick, 4.0 + tick})) << "right after the last point, spacing kept";
    EXPECT_EQ(f.values(), (std::vector<double>{20.0, 60.0, 40.0, 60.0, 40.0}));
    EXPECT_EQ(f.selected(), Beats({3.0 + tick, 4.0 + tick})) << "the copies become the selection";

    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0})) << "one undo takes both copies back";
}

//==============================================================================
// Duplicate points preference: where Cmd+D puts the copies

namespace {
using synth::ui::DuplicatePlacement;

struct PlacementGuard {
    explicit PlacementGuard(DuplicatePlacement placement) { synth::ui::setDuplicatePlacement(placement); }
    ~PlacementGuard() { synth::ui::setDuplicatePlacement(DuplicatePlacement::afterLast); }
};

// The three points of SelectLane, all selected (spaced one beat, 1..3), duplicated; the beats of the copies.
Beats duplicatedBeats(SelectLane& f, DuplicatePlacement placement, bool snapOn,
                      synth::ui::TimelineViewState::Snap snap = synth::ui::TimelineViewState::Snap::Quarter) {
    PlacementGuard guard(placement);
    f.panel.getViewState().snapEnabled = snapOn;
    f.panel.getViewState().snap = snap;
    f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
    const auto rev = f.doc.getRevision();
    EXPECT_TRUE(f.editor->duplicateSelectedPoints());
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one undo step";
    const auto all = f.beats();
    EXPECT_EQ(all.size(), 6u);
    EXPECT_EQ(f.selected(), Beats(all.begin() + 3, all.end())) << "the copies are the selection";
    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0}));
    return Beats(all.begin() + 3, all.end());
}
} // namespace

TEST(AutomationLanesDuplicatePlacement, RightAfterTheLastPointIsTheDefaultAndStartsOneTickPastIt) {
    EXPECT_EQ(synth::ui::duplicatePlacement(), DuplicatePlacement::afterLast);
    const double t = synth::ui::tickBeats();
    SelectLane snapped, free;
    EXPECT_EQ(duplicatedBeats(snapped, DuplicatePlacement::afterLast, true), Beats({3.0 + t, 4.0 + t, 5.0 + t}));
    EXPECT_EQ(duplicatedBeats(free, DuplicatePlacement::afterLast, false), Beats({3.0 + t, 4.0 + t, 5.0 + t}))
        << "snap does not change this one";
}

TEST(AutomationLanesDuplicatePlacement, OnePointSpacingStartsOneGapPastTheLastPointSoRepeatsRunEvenly) {
    SelectLane f;
    EXPECT_EQ(duplicatedBeats(f, DuplicatePlacement::onePointSpacing, true), Beats({4.0, 5.0, 6.0}));
    SelectLane g;
    g.doc.addBreakpoint(g.lane, 3.5, 10.0);
    PlacementGuard guard(DuplicatePlacement::onePointSpacing);
    g.editor->getPointSelection().setSelection({2.0, 3.0, 3.5});
    ASSERT_TRUE(g.editor->duplicateSelectedPoints());
    EXPECT_EQ(g.selected(), Beats({4.0, 5.0, 5.5})) << "the gap between the last two (half a beat) after the last";
}

TEST(AutomationLanesDuplicatePlacement, TheNextGridLineMovesTheCopiesByWholeGridStepsPastTheSelection) {
    SelectLane quarter, eighth, free;
    EXPECT_EQ(duplicatedBeats(quarter, DuplicatePlacement::nextGridLine, true), Beats({4.0, 5.0, 6.0}))
        << "a span of two whole steps still clears the last point";
    EXPECT_EQ(
        duplicatedBeats(eighth, DuplicatePlacement::nextGridLine, true, synth::ui::TimelineViewState::Snap::Eighth),
        Beats({3.5, 4.5, 5.5}));
    const double t = synth::ui::tickBeats();
    EXPECT_EQ(duplicatedBeats(free, DuplicatePlacement::nextGridLine, false), Beats({3.0 + t, 4.0 + t, 5.0 + t}))
        << "with snap off it is the default placement";
}

TEST(AutomationLanesDuplicatePlacement, ASinglePointIsCopiedOneGridStepOnWhateverTheChoice) {
    for (const auto placement :
         {DuplicatePlacement::afterLast, DuplicatePlacement::onePointSpacing, DuplicatePlacement::nextGridLine}) {
        PlacementGuard guard(placement);
        SelectLane snapped;
        snapped.editor->getPointSelection().setSelection({2.0});
        ASSERT_TRUE(snapped.editor->duplicateSelectedPoints());
        EXPECT_EQ(snapped.selected(), Beats({3.0})) << "a grid step (a beat here)";
        EXPECT_EQ(snapped.beats(), Beats({1.0, 2.0, 3.0})) << "it landed on the point at beat 3 and replaced it";

        SelectLane free;
        free.panel.getViewState().snapEnabled = false;
        free.editor->getPointSelection().setSelection({2.0});
        ASSERT_TRUE(free.editor->duplicateSelectedPoints());
        EXPECT_EQ(free.selected(), Beats({2.25})) << "a quarter of a beat with snap off";
    }
}

TEST(AutomationLanesPointSelectionTest, ArrowKeysFallThroughOnALaneWithNoPoints) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
    f.key(juce::KeyPress(juce::KeyPress::deleteKey));
    ASSERT_TRUE(f.theLane().points.empty());
    EXPECT_FALSE(f.key(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_FALSE(f.key(juce::KeyPress(juce::KeyPress::upKey)));
}

TEST(AutomationLanesPointSelectionTest, CopyAndPasteRoundTripsThePointsAtThePastePosition) {
    SelectLane f;
    EXPECT_FALSE(f.editor->canPastePoints());
    EXPECT_FALSE(f.editor->copySelectedPoints()) << "nothing selected, nothing copied";
    f.editor->getPointSelection().setSelection({1.0, 2.0});
    ASSERT_TRUE(f.editor->copySelectedPoints());
    EXPECT_TRUE(f.editor->canPastePoints());
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0})) << "copy leaves the doc alone";

    ASSERT_TRUE(f.editor->pasteAtBeat(5.0));
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0, 5.0, 6.0}));
    EXPECT_NEAR(f.theLane().points[3].value, 20.0, 1e-6);
    EXPECT_NEAR(f.theLane().points[4].value, 60.0, 1e-6);
    EXPECT_EQ(f.selected(), Beats({5.0, 6.0})) << "the pasted points become the selection";

    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0})) << "one undo takes the paste back";
}

TEST(AutomationLanesPointSelectionTest, CutCopiesThenRemovesAndPasteWorksFromAnotherEditor) {
    SelectLane f;
    const auto other = f.addLane(f.bass, "resonance");
    auto* second = f.panel.laneEditorForTest(other);
    ASSERT_NE(second, nullptr);

    f.editor->getPointSelection().setSelection({2.0});
    ASSERT_TRUE(f.editor->cutSelectedPoints());
    EXPECT_EQ(f.beats(), Beats({1.0, 3.0}));
    EXPECT_TRUE(second->canPastePoints()) << "the clipboard belongs to the lane pool, not one editor";
    ASSERT_TRUE(second->pasteAtBeat(2.0));
    ASSERT_EQ(f.doc.getLane(other)->points.size(), 1u);
    EXPECT_NEAR(f.doc.getLane(other)->points[0].value, 60.0, 1e-6);
}

TEST(AutomationLanesPointSelectionTest, ThePasteLandsOnTheTransportPosition) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({3.0});
    ASSERT_TRUE(f.editor->copySelectedPoints());
    // No transport on the bare panel: the paste lands at the start.
    ASSERT_TRUE(f.editor->pasteAtPlayhead());
    EXPECT_EQ(f.beats(), Beats({0.0, 1.0, 2.0, 3.0}));
}

TEST(AutomationLanesPointSelectionTest, TheSelectionFollowsTheDocAndClearsWhenAPointOrTheLaneGoes) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0});

    f.doc.addBreakpoint(f.lane, 4.0, 10.0); // an edit that keeps the lane and the points
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0})) << "survives a notification that keeps them";

    f.doc.removeBreakpoint(f.lane, 2.0);
    EXPECT_EQ(f.selected(), Beats({1.0})) << "a point that disappears leaves the selection";

    f.editor->setActiveLane(synth::LaneId{});
    EXPECT_TRUE(f.selected().empty()) << "a different lane starts with nothing selected";
    EXPECT_FALSE(f.editor->getPointSelection().getCursor().has_value());
}

TEST(AutomationLanesPointSelectionTest, TheBubbleStillFollowsTheHoveredPointWhileItIsSelected) {
    ReducedMotionGuard noMotion;
    SelectLane f;
    f.click(f.at(2.0, 60.0));
    f.editor->mouseMove(makeClickEvent(*f.editor, f.at(2.0, 60.0)));
    EXPECT_TRUE(f.editor->getPointBubbleForTest().isShown());
    EXPECT_EQ(f.editor->getPointBubbleForTest().getText(), "60.0 units");
}

TEST(AutomationLanesPointSelectionTest, ASelectedPointIsAFilledAccentDotAndAnUnselectedOneIsNot) {
    ReducedMotionGuard noMotion;
    SelectLane f;
    auto render = [&] {
        juce::Image image(juce::Image::ARGB, f.editor->getWidth(), f.editor->getHeight(), true,
                          juce::SoftwareImageType());
        juce::Graphics g(image);
        f.editor->paint(g);
        return image;
    };
    juce::Colour accent = juce::Colours::yellow;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&f.editor->getLookAndFeel()))
        accent = lf->getTheme().colors.accent;
    const auto centre = [&](double beat, double value) {
        const auto p = f.at(beat, value);
        return juce::Point<int>((int)std::lround(p.x), (int)std::lround(p.y));
    };

    const auto before = render();
    f.click(f.at(2.0, 60.0));
    const auto after = render();
    const auto selected = centre(2.0, 60.0);
    const auto other = centre(1.0, 20.0);
    EXPECT_EQ(after.getPixelAt(selected.x, selected.y), accent) << "filled with the accent colour";
    EXPECT_NE(before.getPixelAt(selected.x, selected.y), accent);
    EXPECT_EQ(after.getPixelAt(other.x, other.y), before.getPixelAt(other.x, other.y)) << "others look as before";
    EXPECT_NE(after.getPixelAt(other.x, other.y), accent);
}

TEST(AutomationLanesPointSelectionTest, TheScreenReaderDescriptionStatesTheSelection) {
    SelectLane f;
    EXPECT_TRUE(f.editor->getDescription().contains("3 points")) << f.editor->getDescription();
    EXPECT_TRUE(f.editor->getDescription().contains("None selected"));
    f.click(f.at(2.0, 60.0));
    EXPECT_TRUE(f.editor->getDescription().contains("Selected: bar 1 beat 3")) << f.editor->getDescription();
    EXPECT_TRUE(f.editor->getDescription().contains("60.0 units"));
    f.editor->selectAllPoints();
    EXPECT_TRUE(f.editor->getDescription().contains("3 selected")) << f.editor->getDescription();
    EXPECT_FALSE(f.editor->getTooltip().isEmpty());
    EXPECT_TRUE(f.editor->getTooltip().contains("Cmd+A"));
}

TEST(AutomationLanesPointSelectionTest, ChangingTheSelectionFiresTheHookOnceAndOnlyWhenItChanged) {
    SelectLane f;
    int fired = 0;
    f.editor->onSelectionChanged = [&] { ++fired; };
    f.click(f.at(2.0, 60.0));
    const int afterClick = fired;
    EXPECT_GE(afterClick, 1);
    f.click(f.at(2.0, 60.0));
    EXPECT_EQ(fired, afterClick) << "clicking the selected point again changes nothing";
}

// Regression test for FRO1055: plain Left/Right moved the points in time and Alt+Left/Right picked them, the reverse
// of the curve editor.
TEST(AutomationLanesPointSelectionTest, PlainRightPicksTheNextPointAndMovesNothingInTime) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({2.0});
    f.editor->getPointSelection().setCursor(2.0);
    const auto rev = f.doc.getRevision();
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(f.selected(), Beats({3.0}));
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0}));
    EXPECT_EQ(f.doc.getRevision(), rev) << "picking edits nothing";
}

TEST(AutomationLanesPointSelectionTest, AltRightMovesTheSelectionInTimeAndAltUpChangesItsValueAsOneUndoStepEach) {
    SelectLane f;
    f.doc.addBreakpoint(f.lane, 5.0, 10.0);
    f.editor->getPointSelection().setSelection({2.0});
    f.editor->getPointSelection().setCursor(2.0);
    const auto alt = juce::ModifierKeys::altModifier;
    const auto rev = f.doc.getRevision();

    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, alt, 0)));
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    EXPECT_EQ(f.selected(), Beats({3.0})) << "the selection follows the point";
    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0, 5.0}));

    f.editor->getPointSelection().setSelection({2.0});
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::upKey, alt, 0)));
    EXPECT_NEAR(f.theLane().points[1].value, 61.0, 1e-6);
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0, 5.0})) << "Alt+Up changes only the value";
    f.undo.undo();
    EXPECT_NEAR(f.theLane().points[1].value, 60.0, 1e-6);

    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::upKey, alt | juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(f.theLane().points[1].value, 60.25, 1e-6) << "Alt+Shift is the fine move: a quarter of the step";
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, alt | juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(f.beats()[1], 2.25, 1e-9);
}

// Regression test for FRO1055: a point could only be added with the mouse.
TEST(AutomationLanesPointSelectionTest, CtrlTInsertsAPointHalfwayAfterTheCursorAsOneUndoStep) {
    SelectLane f;
    const auto ctrl = juce::ModifierKeys::ctrlModifier;
    EXPECT_FALSE(f.key(juce::KeyPress('T', ctrl, 0))) << "no cursor point: nothing to insert after";
    f.editor->getPointSelection().setSelection({2.0});
    f.editor->getPointSelection().setCursor(2.0);
    const auto rev = f.doc.getRevision();

    EXPECT_TRUE(f.key(juce::KeyPress('T', ctrl, 0)));
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 2.5, 3.0}));
    EXPECT_NEAR(f.theLane().points[2].value, 50.0, 1e-9) << "the linear midpoint of 60 and 40";
    EXPECT_EQ(f.selected(), Beats({2.5})) << "the new point is selected";
    EXPECT_EQ(f.editor->getPointSelection().getCursor(), std::optional<double>(2.5));
    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0}));

    f.editor->getPointSelection().setSelection({3.0});
    f.editor->getPointSelection().setCursor(3.0);
    EXPECT_FALSE(f.key(juce::KeyPress('T', ctrl, 0))) << "nothing follows the last point";
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0}));
}

// One key script, the same result: the curve editor and an automation lane follow one key map
// (docs/control/point-editing.md). Cmd+D reaches a lane editor as the app's edit command, not as a key event.
TEST(AutomationLanesPointSelectionTest, TheSameKeyScriptGivesTheSameSelectionCountsAndEditsOnACurveAndOnALane) {
    using synth::ui::CurveEditorComponent;
    using synth::ui::CurveMode;
    using synth::ui::CurveModel;
    using synth::ui::CurveNode;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const auto alt = juce::ModifierKeys::altModifier;

    // Three loose points each (the curve's ends are fixed and cannot be picked).
    CurveEditorComponent curve;
    curve.setBounds(0, 0, 400, 160);
    CurveModel model(CurveMode::Free);
    model.setNodes({CurveNode{0.0, 0.5f, false, false}, CurveNode{0.25, 0.5f, true, true},
                    CurveNode{0.5, 0.5f, true, true}, CurveNode{0.75, 0.5f, true, true},
                    CurveNode{1.0, 0.5f, false, false}});
    curve.setModel(std::move(model));
    SelectLane lane;

    auto press = [&](int code, int mods = 0) {
        const juce::KeyPress k(code, juce::ModifierKeys(mods), 0);
        EXPECT_TRUE(curve.keyPressed(k)) << code << "/" << mods;
        EXPECT_TRUE(lane.key(k)) << code << "/" << mods;
    };
    auto points = [&] { return std::make_pair(curve.getModel().getNumNodes() - 2, (int)lane.beats().size()); };
    auto selection = [&] { return std::make_pair(curve.getSelectedIndices().size(), lane.selected().size()); };

    press(juce::KeyPress::rightKey);
    press(juce::KeyPress::rightKey);
    press(juce::KeyPress::rightKey, shift);
    EXPECT_EQ(selection(), std::make_pair(std::size_t(2), std::size_t(2)));

    const float curveBefore = curve.getModel().getNode(2).y;
    const double laneBefore = lane.values()[1];
    press(juce::KeyPress::upKey, alt);
    EXPECT_GT(curve.getModel().getNode(2).y, curveBefore);
    EXPECT_GT(lane.values()[1], laneBefore);

    ASSERT_TRUE(curve.keyPressed(juce::KeyPress('D', juce::ModifierKeys::commandModifier, 0)));
    ASSERT_TRUE(lane.editor->duplicateSelectedPoints());
    EXPECT_EQ(points(), std::make_pair(5, 5));
    EXPECT_EQ(selection(), std::make_pair(std::size_t(2), std::size_t(2))) << "the copies are the selection";

    press(juce::KeyPress::backspaceKey);
    EXPECT_EQ(points(), std::make_pair(3, 3));
    lane.undo.undo();
    EXPECT_EQ(lane.beats().size(), 5u) << "one Cmd+Z brings the copies back";
}

//==============================================================================
// Holding an arrow: a keyboard drag of the selected points (docs/timeline/automation.md "Keyboard moves")

namespace {

const int kAltMod = juce::ModifierKeys::altModifier;

// A lane whose Alt+Right / Up is physically held: the clock and the keys are the test's.
struct HeldKeys : SelectLane {
    key_glide_test::FakeKeys keys;

    HeldKeys() { editor->getKeyHoldForTest().host = keys.host(); }

    void hold(int code, int mods = 0) {
        keys.down = {code};
        keys.alt = (mods & kAltMod) != 0;
        keys.shift = (mods & juce::ModifierKeys::shiftModifier) != 0;
        EXPECT_TRUE(key(juce::KeyPress(code, juce::ModifierKeys(mods), 0)));
    }
    void frame(double glideMs) {
        keys.now = 1000.0 + synth::ui::lane_key::kTapMs + glideMs;
        editor->getKeyHoldForTest().tick();
    }
    void release() {
        keys.down.clear();
        editor->getKeyHoldForTest().checkKeyUp();
    }
    double previewBeat() const { return editor->getPreviewBeatForTest(); }
};

} // namespace

TEST(AutomationLanesKeyboardDrag, WithSnapOffATapIsFourPixelsNotASixteenth) {
    HeldKeys f;
    f.panel.getViewState().snapEnabled = false;
    f.editor->getPointSelection().setSelection({2.0});
    const auto rev = f.doc.getRevision();

    f.hold(juce::KeyPress::rightKey, kAltMod);
    const double tap = f.previewBeat() - 2.0;
    EXPECT_NEAR(tap, 4.0 / f.panel.getViewState().pixelsPerBeat, 1e-9);
    EXPECT_LT(tap, 0.25) << "less than the old sixteenth at the default zoom";
    EXPECT_EQ(f.doc.getRevision(), rev) << "nothing is committed while the key is down";
    f.release();
    EXPECT_NEAR(f.beats()[1], 2.0 + tap, 1e-9);
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
}

TEST(AutomationLanesKeyboardDrag, AHoldMovesContinuouslyAcceleratesAndIsOneUndoStepIncludingTheTap) {
    HeldKeys f;
    f.panel.getViewState().snapEnabled = false;
    f.editor->getPointSelection().setSelection({2.0});
    f.hold(juce::KeyPress::rightKey, kAltMod);
    const double afterTap = f.previewBeat();

    f.frame(0.0);
    f.frame(1000.0);
    const double afterOneSecond = f.previewBeat();
    f.frame(2000.0);
    const double afterTwoSeconds = f.previewBeat();
    EXPECT_GT(afterOneSecond, afterTap);
    EXPECT_GT(afterTwoSeconds - afterOneSecond, afterOneSecond - afterTap) << "the second second covers more";
    EXPECT_NE(std::fmod(afterOneSecond, 0.25), 0.0) << "not stepping in sixteenths";

    const auto preview = f.editor->getDragMovedPointsForTest();
    const auto rev = f.doc.getRevision();
    f.release();
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "release commits once";
    ASSERT_EQ(f.theLane().points.size(), 3u);
    EXPECT_DOUBLE_EQ(f.beats().back(), preview.front().beat)
        << "the committed point is the last preview (it passed beat 3)";
    EXPECT_EQ(f.selected(), Beats({preview.front().beat}));
    EXPECT_FALSE(f.editor->isDragActiveForTest());

    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0})) << "one undo takes the tap and the hold back";
}

TEST(AutomationLanesKeyboardDrag, ASnappedHoldLandsOnGridLinesAndATapIsOneGridStep) {
    HeldKeys f;
    f.editor->getPointSelection().setSelection({3.0});
    f.hold(juce::KeyPress::rightKey, kAltMod);
    EXPECT_DOUBLE_EQ(f.previewBeat(), 4.0) << "the tap is one grid step (a beat here)";
    f.frame(2500.0);
    const double beat = f.previewBeat();
    EXPECT_GT(beat, 4.0);
    EXPECT_NEAR(beat, std::round(beat), 1e-9) << "on a grid line";
    f.frame(3000.0);
    EXPECT_NEAR(f.previewBeat(), std::round(f.previewBeat()), 1e-9);
    const double last = f.previewBeat();
    f.release();
    EXPECT_DOUBLE_EQ(f.beats().back(), last);
}

TEST(AutomationLanesKeyboardDrag, UpHoldsTheValueAndShiftIsThreeTimesAsFast) {
    HeldKeys plain, shifted;
    plain.editor->getPointSelection().setSelection({2.0});
    shifted.editor->getPointSelection().setSelection({2.0});
    plain.hold(juce::KeyPress::upKey);
    shifted.hold(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier);
    const double tapPlain = plain.editor->getPreviewValueForTest() - 60.0;
    const double tapShifted = shifted.editor->getPreviewValueForTest() - 60.0;
    EXPECT_NEAR(tapPlain, 1.0, 1e-9);
    EXPECT_NEAR(tapShifted, 10.0, 1e-9);

    plain.frame(1000.0);
    shifted.frame(1000.0);
    const double glidePlain = plain.editor->getPreviewValueForTest() - 60.0 - tapPlain;
    const double glideShifted = shifted.editor->getPreviewValueForTest() - 60.0 - tapShifted;
    EXPECT_GT(glidePlain, 0.0);
    EXPECT_NEAR(glideShifted / glidePlain, 3.0, 1e-6);
    plain.release();
    EXPECT_GT(plain.theLane().points[1].value, 61.0);
    EXPECT_EQ(plain.beats(), Beats({1.0, 2.0, 3.0})) << "a value hold leaves the beats alone";
    shifted.release();
}

TEST(AutomationLanesKeyboardDrag, ATapReleasedBeforeTheGlideStartsCommitsTheOneStepAndAHoldAtTheEdgeCommitsNothing) {
    HeldKeys f;
    f.editor->getPointSelection().setSelection({2.0});
    f.hold(juce::KeyPress::rightKey, kAltMod);
    f.release();
    EXPECT_EQ(f.beats(), Beats({1.0, 3.0})) << "one grid step: it landed on the point at beat 3 and replaced it";

    HeldKeys edge;
    edge.doc.addBreakpoint(edge.lane, 0.0, 10.0);
    edge.editor->getPointSelection().setSelection({0.0});
    const auto rev = edge.doc.getRevision();
    edge.hold(juce::KeyPress::leftKey, kAltMod);
    edge.frame(1000.0);
    edge.release();
    EXPECT_EQ(edge.doc.getRevision(), rev) << "a hold that never moved reaches no undo step";
}

TEST(AutomationLanesKeyboardDrag, ASynthesisedPressWithNoKeyDownIsOneCommittedStep) {
    HeldKeys f;
    f.panel.getViewState().snapEnabled = false;
    f.editor->getPointSelection().setSelection({2.0});
    const auto rev = f.doc.getRevision();
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAltMod), 0)));
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    EXPECT_FALSE(f.editor->getKeyHoldForTest().active());
    EXPECT_NEAR(f.beats()[1], 2.0 + 4.0 / f.panel.getViewState().pixelsPerBeat, 1e-9);
}

TEST(AutomationLanesKeyboardDrag, AutoRepeatKeepsTheRunningHoldAndEscapeCancelsItWithoutAnEdit) {
    HeldKeys f;
    f.editor->getPointSelection().setSelection({3.0});
    f.hold(juce::KeyPress::rightKey, kAltMod);
    const double beat = f.previewBeat();
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAltMod), 0)))
        << "the OS repeating the key";
    EXPECT_DOUBLE_EQ(f.previewBeat(), beat) << "does not step again";

    const auto rev = f.doc.getRevision();
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(f.doc.getRevision(), rev);
    EXPECT_EQ(f.beats(), Beats({1.0, 2.0, 3.0}));
    EXPECT_FALSE(f.editor->isDragActiveForTest());
}

TEST(AutomationLanesKeyboardDrag, ATapEasesTheDrawnPointAndPlainlyLandsUnderReduceMotion) {
    HeldKeys f;
    f.editor->forceGlideAnimateForTest(true);
    f.editor->getPointSelection().setSelection({2.0});
    const auto key2 = synth::ui::lane_key::tweenKey(2.0);

    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAltMod), 0)));
    const auto key3 = synth::ui::lane_key::tweenKey(3.0);
    auto& tween = f.editor->getPointTweenForTest();
    ASSERT_TRUE(tween.isAnimating());
    const float start = tween.offsetFor(key3).x;
    EXPECT_NEAR(start, -(float)f.panel.getViewState().pixelsPerBeat, 1e-3f) << "drawn at the old place first";
    tween.stepForTest(40.0);
    EXPECT_GT(tween.offsetFor(key3).x, start);
    EXPECT_LT(tween.offsetFor(key3).x, 0.0f);
    (void)key2;
    tween.stepForTest(200.0);
    EXPECT_FALSE(tween.isAnimating());

    ReducedMotionGuard reduced;
    f.editor->getPointSelection().setSelection({3.0});
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAltMod), 0)));
    EXPECT_FALSE(tween.isAnimating()) << "no ease under Reduce Motion";
}

//==============================================================================
// The selection box through a move (docs/timeline/automation.md "Stretching a selection")

namespace {

// Records the box's fade level every time the selection reports a change: a box that blinks out and back in during a
// commit shows a 0 here.
struct AlphaLog {
    std::vector<float> seen;
    explicit AlphaLog(AutomationLaneEditor& editor) {
        editor.onSelectionChanged = [this, &editor] { seen.push_back(editor.getStretchForTest().boxAlpha()); };
    }
    bool neverHidden() const {
        return std::all_of(seen.begin(), seen.end(), [](float a) { return a == 1.0f; });
    }
};

juce::Rectangle<float> boxAround(AutomationLaneEditor& editor, const std::vector<synth::ui::LaneBreakpoint>& points,
                                 AutomationLaneEditor& mapperOwner, synth::ui::TimelineViewState& view) {
    const auto bounds = synth::ui::pointsBounds(points, [&](double beat, double value) {
        return juce::Point<float>((float)view.beatToX(beat), (float)mapperOwner.valueToY(value));
    });
    return synth::ui::stretchBoxAround(bounds).getIntersection(editor.getLocalBounds().toFloat().reduced(0.5f));
}

} // namespace

// Regression test for FRO1064: the notification an own commit fired pruned the selection, so the box faded out and
// back in around every nudge and mouse drop.
TEST(AutomationLanesSelectionBox, TheBoxStaysFullyShownAcrossANudgeAndAfterAMouseDrop) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0});
    ASSERT_FLOAT_EQ(f.editor->getStretchForTest().boxAlpha(), 1.0f);
    AlphaLog log(*f.editor);

    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAltMod), 0)));
    EXPECT_FLOAT_EQ(f.editor->getStretchForTest().boxAlpha(), 1.0f);
    EXPECT_TRUE(log.neverHidden()) << "a nudge never hides the box on the way";
    EXPECT_EQ(f.selected(), Beats({2.0, 3.0}));

    f.drag(f.at(2.0, 60.0), f.at(4.0, 60.0));
    EXPECT_EQ(f.beats(), Beats({4.0, 5.0}));
    EXPECT_FLOAT_EQ(f.editor->getStretchForTest().boxAlpha(), 1.0f) << "after the drop the box is still there";
    EXPECT_TRUE(log.neverHidden()) << "and was never hidden by the commit";
    EXPECT_FALSE(f.editor->getStretchBoxForTest().isEmpty());
}

TEST(AutomationLanesSelectionBox, DuringAMouseDragTheBoxIsTheBoundsOfThePreviewPoints) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0});
    const auto from = f.at(2.0, 60.0);
    const auto to = f.at(3.0, 90.0);
    f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
    for (int i = 1; i <= 4; ++i) {
        f.editor->mouseDrag(makeDragEvent(*f.editor, from + (to - from) * ((float)i / 4.0f), from, leftButton()));
        const auto preview = f.editor->getDragMovedPointsForTest();
        EXPECT_EQ(f.editor->getStretchBoxForTest(), boxAround(*f.editor, preview, *f.editor, f.panel.getViewState()))
            << "step " << i;
        EXPECT_EQ(f.editor->getShownStretchBoxForTest(), f.editor->getStretchBoxForTest()) << "no lag, no ease";
    }
    f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));
    ASSERT_EQ(f.selected().size(), 2u);
    EXPECT_EQ(f.editor->getStretchBoxForTest(),
              boxAround(*f.editor, f.theLane().points, *f.editor, f.panel.getViewState()))
        << "and the dropped box is the committed points' box";
}

TEST(AutomationLanesSelectionBox, GrowingTheSelectionEasesTheBoxThroughIntermediateSizes) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0});
    auto& stretch = f.editor->getStretchForTest();
    const auto before = f.editor->getStretchBoxForTest();
    synth::ui::setReducedMotionForTest(false);
    stretch.forceAnimateForTest(true);

    EXPECT_TRUE(
        f.key(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), 0)));
    ASSERT_EQ(f.selected(), Beats({1.0, 2.0, 3.0}));
    const auto after = f.editor->getStretchBoxForTest();
    ASSERT_GT(after.getWidth(), before.getWidth());
    EXPECT_EQ(f.editor->getShownStretchBoxForTest().getWidth(), before.getWidth()) << "starts at the old size";

    stretch.stepForTest(40.0);
    const float mid = f.editor->getShownStretchBoxForTest().getWidth();
    EXPECT_GT(mid, before.getWidth());
    EXPECT_LT(mid, after.getWidth());
    stretch.stepForTest(300.0);
    EXPECT_EQ(f.editor->getShownStretchBoxForTest(), after);
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST(AutomationLanesSelectionBox, ATapEasesTheBoxAndReduceMotionMovesItAtOnce) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0});
    auto& stretch = f.editor->getStretchForTest();
    synth::ui::setReducedMotionForTest(false);
    stretch.forceAnimateForTest(true);
    const auto before = f.editor->getStretchBoxForTest();

    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAltMod), 0)));
    const auto after = f.editor->getStretchBoxForTest();
    ASSERT_GT(after.getX(), before.getX());
    EXPECT_EQ(f.editor->getShownStretchBoxForTest().getX(), before.getX());
    stretch.stepForTest(40.0);
    EXPECT_GT(f.editor->getShownStretchBoxForTest().getX(), before.getX());
    EXPECT_LT(f.editor->getShownStretchBoxForTest().getX(), after.getX());
    stretch.stepForTest(300.0);

    synth::ui::setReducedMotionForTest(true);
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAltMod), 0)));
    EXPECT_EQ(f.editor->getShownStretchBoxForTest(), f.editor->getStretchBoxForTest());
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST(AutomationLanesSelectionBox, DuringAKeyHoldTheBoxFollowsThePreviewAndEscapeStillFadesItOut) {
    HeldKeys f;
    f.panel.getViewState().snapEnabled = false;
    f.editor->getPointSelection().setSelection({1.0, 2.0});
    f.hold(juce::KeyPress::rightKey, kAltMod);
    f.frame(0.0);
    f.frame(800.0);
    const auto preview = f.editor->getDragMovedPointsForTest();
    EXPECT_EQ(f.editor->getStretchBoxForTest(), boxAround(*f.editor, preview, *f.editor, f.panel.getViewState()));
    EXPECT_EQ(f.editor->getShownStretchBoxForTest(), f.editor->getStretchBoxForTest());
    f.release();

    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FLOAT_EQ(f.editor->getStretchForTest().boxAlpha(), 0.0f);
}
