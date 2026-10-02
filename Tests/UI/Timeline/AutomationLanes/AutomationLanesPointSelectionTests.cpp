// AutomationLanesPointSelectionTests.cpp -- selecting automation points like clips and notes: click, Shift-click,
// a box on empty space, Escape, moving and deleting the selection as one undo step, arrow nudging, the keyboard
// cursor, copy/paste and how the selection follows the doc. Real events on the real panel's editor.

#include "AutomationLanesTestFixture.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

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

    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey)));
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
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(f.beats(), Beats({0.0, 0.5, 2.0, 3.0})) << "both move by the 0.5 the first one had left, not 1";
}

TEST(AutomationLanesPointSelectionTest, TheKeyboardCursorStepsFromPointToPointAndSelectsIt) {
    SelectLane f;
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey)))
        << "with nothing selected the first press picks the first point";
    EXPECT_EQ(f.selected(), Beats({1.0}));
    const auto alt = juce::ModifierKeys::altModifier;
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, alt, 0)));
    EXPECT_EQ(f.selected(), Beats({2.0}));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, alt, 0)));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::rightKey, alt, 0)));
    EXPECT_EQ(f.selected(), Beats({3.0})) << "it stays on the last point";
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::leftKey, alt, 0)));
    EXPECT_EQ(f.selected(), Beats({2.0}));
    EXPECT_EQ(f.editor->getPointSelection().getCursor(), std::optional<double>(2.0));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.beats(), Beats({1.0, 3.0})) << "Delete removes the cursor point";
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
