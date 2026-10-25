// Per-drum length and swing in the drum machine UI: the ghost repeats past a drum's end, the end edge and its drag
// (preview equals commit, one undo step), the row-name menu, the four Alt+arrow actions through the shortcut table, the
// accessible names, and the motion rules. The model is in Timeline/TimelineDoc; playback is the snapshot's.

#include "DrumMachineGrooveFixture.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Transport/TransportService.h"
#include "UI/DrumMachine/DrumMachineHost.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"

using drumtest::AnimationModeGuard;
using drumtest::GrooveFixture;
using synth::ui::AnimationMode;

namespace {

constexpr int kHat = 42;   // Closed hat
constexpr int kSteps = 32; // an 8 beat clip at 16 steps per bar
constexpr double kStep = 0.25;

// The tests drive the menu through this hook; a real run shows the menu.
struct MenuCapture {
    int shown = 0;
    int pitch = -1;
    juce::PopupMenu menu;
    std::function<void(int)> choose;
    MenuCapture() {
        synth::ui::test_hooks::drumRowMenuHookForTest() = [this](int p, juce::PopupMenu m, std::function<void(int)> c) {
            ++shown;
            pitch = p;
            menu = std::move(m);
            choose = std::move(c);
        };
    }
    ~MenuCapture() { synth::ui::test_hooks::drumRowMenuHookForTest() = nullptr; }
};

struct Item {
    bool found = false;
    int id = 0;
    bool ticked = false;
    bool enabled = false;
};

Item findItem(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == text)
            return {true, it.getItem().itemID, it.getItem().isTicked, it.getItem().isEnabled};
    return {};
}

void rightClickName(GrooveFixture& f, int pitch) {
    juce::Component& name = *f.grid.getRowLabels().getNameButton(pitch);
    const auto p = centreOf(name.getLocalBounds());
    const auto e = makeRollMouseEvent(name, p, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), false, p);
    name.mouseDown(e);
    name.mouseUp(e);
}

// What the window does with a key: the focused component, then each parent in turn.
bool dispatchKey(juce::Component& focused, const juce::KeyPress& key) {
    for (auto* c = &focused; c != nullptr; c = c->getParentComponent())
        if (c->keyPressed(key))
            return true;
    return false;
}

juce::KeyPress altKey(int code, bool shift = false) {
    return juce::KeyPress(
        code, juce::ModifierKeys(juce::ModifierKeys::altModifier | (shift ? juce::ModifierKeys::shiftModifier : 0)), 0);
}

struct RowFixture : GrooveFixture {
    RowFixture() {
        addHits(kHat, {0, 4, 14}); // step 14 sits past a 12 step drum
        grid.setFocusCell({grid.rowOfPitch(kHat), 0});
    }
    int row() { return grid.rowOfPitch(kHat); }
    int storedLength() {
        for (const auto& r : c().drumRows)
            if (r.pitch == kHat)
                return r.lengthSteps;
        return 0;
    }
    juce::Point<float> boundary(int steps, float dy = 0.0f) {
        return {static_cast<float>(grid.beatToX(steps * kStep)),
                static_cast<float>(grid.cellBounds(row(), 0).getCentreY()) + dy};
    }
    void setLength(int steps) {
        ASSERT_TRUE(doc.setClipRowLength(clip, kHat, steps));
        grid.refreshFromDoc();
        undo.clearUndoHistory();
    }
};

} // namespace

//==============================================================================
// Ghost repeats

TEST(DrumMachineRowLengthTest, CellsPastADrumsEndRepeatTheCellAtStepModLength) {
    RowFixture f;
    f.setLength(12);
    EXPECT_EQ(f.grid.getRowLength(kHat), 12);
    for (int step = 0; step < 12; ++step)
        EXPECT_EQ(f.grid.ghostSourceStep(f.row(), step), -1) << step;
    EXPECT_EQ(f.grid.ghostSourceStep(f.row(), 12), 0);
    EXPECT_EQ(f.grid.ghostSourceStep(f.row(), 16), 4);
    EXPECT_EQ(f.grid.ghostSourceStep(f.row(), 24), 0);
    EXPECT_EQ(f.grid.ghostSourceStep(f.row(), 31), 7);
    EXPECT_EQ(f.grid.ghostSourceStep(f.grid.rowOfPitch(38), 20), -1) << "another drum is not affected";
}

TEST(DrumMachineRowLengthTest, ALengthAtOrPastTheClipEndIsNoLength) {
    RowFixture f;
    f.setLength(kSteps);
    EXPECT_EQ(f.grid.getRowLength(kHat), 0);
    EXPECT_EQ(f.grid.ghostSourceStep(f.row(), 20), -1);
}

TEST(DrumMachineRowLengthTest, AHiddenStoredNoteIsNotAHitAndAGhostHoldsNoVelocity) {
    RowFixture f;
    EXPECT_EQ(f.grid.getCellVelocity(f.row(), 14), 100);
    f.setLength(12);
    EXPECT_EQ(f.grid.getCellVelocity(f.row(), 14), 0) << "the note stored at step 14 is hidden behind a ghost";
    EXPECT_EQ(f.grid.getCellVelocity(f.row(), 12), 0) << "a ghost is a picture of step 0, not a hit";
    EXPECT_EQ(f.grid.getCellVelocity(f.row(), 4), 100);
    f.setLength(0);
    EXPECT_EQ(f.grid.getCellVelocity(f.row(), 14), 100) << "lengthening brings it back";
}

TEST(DrumMachineRowLengthTest, GhostsDrawTheRepeatedHitAtThirtyPercentOfTheRealOne) {
    RowFixture f;
    f.setLength(12);
    f.grid.finishMotionsForTest();
    juce::Image image(juce::Image::ARGB, f.grid.getWidth(), f.grid.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    f.grid.paintEntireComponent(g, false);
    const auto lowPixel = [&](int step) {
        const auto r = f.grid.cellBounds(f.row(), step);
        return image.getPixelAt(r.getCentreX(), r.getBottom() - 3);
    };
    const double real = rgbDistance(lowPixel(0), lowPixel(1));    // a hit against an empty cell of the same shade
    const double ghost = rgbDistance(lowPixel(12), lowPixel(13)); // its repeat (step 12 copies 0, step 13 copies 1)
    EXPECT_GT(ghost, 6.0) << "the repeat is visible";
    EXPECT_LT(ghost, real * 0.6) << "and much fainter than a real hit";
    const double hidden = rgbDistance(lowPixel(14), lowPixel(13));
    EXPECT_LT(hidden, 3.0) << "step 14 copies step 2 (empty): the stored note under it is not drawn";
}

TEST(DrumMachineRowLengthTest, AGhostCarriesTheSourceHitsMarks) {
    RowFixture f;
    for (const auto& n : f.c().notes)
        if (n.pitch == kHat && n.startBeat == 0.0)
            ASSERT_TRUE(f.doc.setNoteChance(n.id, 50));
    f.setLength(12);
    EXPECT_TRUE(f.grid.getCellMarks(f.row(), 0).dimmed);
    EXPECT_TRUE(f.grid.getCellMarks(f.row(), 12).dimmed) << "the repeat of a 50% hit shows the % mark too";
    EXPECT_FALSE(f.grid.getCellMarks(f.row(), 13).any());
}

TEST(DrumMachineRowLengthTest, ClickingAGhostOrAHiddenNoteAddsAndRemovesNothing) {
    RowFixture f;
    f.setLength(12);
    const auto before = f.c().notes.size();
    for (const int step : {12, 13, 14}) {
        const auto p = centreOf(f.grid.cellBounds(f.row(), step));
        f.grid.mouseDown(leftClick(f.grid, p));
        f.grid.mouseUp(leftClick(f.grid, p));
    }
    EXPECT_EQ(f.c().notes.size(), before) << "a click past the end is ignored";
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_EQ(f.grid.getFocusCell().step, 14) << "but the cell still takes the focus";
    for (const auto& key :
         {juce::KeyPress(juce::KeyPress::returnKey), juce::KeyPress(juce::KeyPress::deleteKey),
          juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), 0)}) {
        f.grid.keyPressed(key);
        EXPECT_EQ(f.c().notes.size(), before);
    }
    EXPECT_FALSE(f.undo.canUndo());
    // The cell before the end still edits.
    const auto p = centreOf(f.grid.cellBounds(f.row(), 11));
    f.grid.mouseDown(leftClick(f.grid, p));
    f.grid.mouseUp(leftClick(f.grid, p));
    EXPECT_EQ(f.c().notes.size(), before + 1);
}

TEST(DrumMachineRowLengthTest, TheScreenReaderReadsAGhostAsARepeatOfItsSource) {
    RowFixture f;
    f.setLength(12);
    f.grid.setFocusCell({f.row(), 12});
    EXPECT_EQ(f.grid.getAccessibilityValueText(), "Closed hat, step 13, repeats step 1, hit, loudness 100");
    f.grid.setFocusCell({f.row(), 13});
    EXPECT_EQ(f.grid.getAccessibilityValueText(), "Closed hat, step 14, repeats step 2, empty");
    f.grid.setFocusCell({f.row(), 4});
    EXPECT_EQ(f.grid.getAccessibilityValueText(), "Closed hat, step 5, hit, loudness 100");
}

TEST(DrumMachineRowLengthTest, TheLanesSkipHitsStoredPastTheEnd) {
    RowFixture f;
    f.grid.setSelectedPitch(kHat);
    f.showLane(DrumLaneKind::Chance);
    EXPECT_EQ(f.grid.getLane(DrumLaneKind::Chance).getHits().size(), 3u);
    f.setLength(12);
    EXPECT_EQ(f.grid.getLane(DrumLaneKind::Chance).getHits().size(), 2u);
}

TEST(DrumMachineRowLengthTest, ARowSwingMovesTheTickWhateverTheClipSwingIs) {
    RowFixture f;
    f.addHits(kHat, {1});
    EXPECT_FALSE(f.grid.getCellMarks(f.row(), 1).any());
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, kHat, 0.75));
    f.grid.refreshFromDoc();
    EXPECT_TRUE(f.grid.getCellMarks(f.row(), 1).playsOffStart);
    EXPECT_NEAR(f.grid.getCellMarks(f.row(), 1).playOffset, 0.5, 1.0e-9);
    EXPECT_DOUBLE_EQ(f.grid.getRowSwing(kHat), 0.75);
    EXPECT_DOUBLE_EQ(f.grid.getRowSwing(38), 0.5) << "another drum follows the clip";
}

//==============================================================================
// The end edge

TEST(DrumMachineRowLengthTest, TheHandleShowsOnHoverWithAResizeCursorAndTheTooltip) {
    RowFixture f;
    const auto p = centreOf(f.grid.edgeHandleBounds(f.row()));
    EXPECT_EQ(f.grid.getHoverEdgeRow(), -1);
    EXPECT_EQ(f.grid.getTooltip(), f.grid.SettableTooltipClient::getTooltip());
    f.grid.mouseMove(hover(f.grid, p));
    EXPECT_EQ(f.grid.getHoverEdgeRow(), f.row());
    EXPECT_TRUE(f.grid.getMouseCursor() == juce::MouseCursor(juce::MouseCursor::LeftRightResizeCursor));
    EXPECT_TRUE(f.grid.getTooltip().startsWith("Drag to change how long this drum's pattern is"))
        << f.grid.getTooltip();
    EXPECT_TRUE(f.grid.getTooltip().contains("Alt")) << "the tooltip names the keys";
    f.grid.mouseMove(hover(f.grid, centreOf(f.grid.cellBounds(f.row(), 5))));
    EXPECT_EQ(f.grid.getHoverEdgeRow(), -1);
    EXPECT_TRUE(f.grid.getMouseCursor() == juce::MouseCursor(juce::MouseCursor::NormalCursor));
}

TEST(DrumMachineRowLengthTest, EachRowsHandleSitsAtItsOwnEndOrTheClipEnd) {
    RowFixture f;
    f.setLength(12);
    const auto own = f.grid.edgeHandleBounds(f.row());
    const auto other = f.grid.edgeHandleBounds(f.grid.rowOfPitch(38));
    EXPECT_EQ(own.getCentreX(), static_cast<int>(std::lround(f.grid.beatToX(12 * kStep))));
    EXPECT_TRUE(other.contains(static_cast<int>(std::lround(f.grid.beatToX(kSteps * kStep))) - 1, other.getCentreY()))
        << "the clip end is clipped to the grid's edge";
    EXPECT_EQ(f.grid.edgeHandleRowAt(own.getCentre()), f.row());
}

TEST(DrumMachineEdgeDragTest, DraggingPreviewsTheGhostsAndCommitsExactlyThePreviewAsOneUndoStep) {
    RowFixture f;
    const auto start = centreOf(f.grid.edgeHandleBounds(f.row()));
    f.grid.mouseMove(hover(f.grid, start));
    f.grid.mouseDown(leftClick(f.grid, start));
    ASSERT_TRUE(f.grid.isEdgeDragging());

    f.grid.mouseDrag(leftDrag(f.grid, f.boundary(20), start));
    EXPECT_EQ(f.grid.getRowLength(kHat), 20);
    f.grid.mouseDrag(leftDrag(f.grid, f.boundary(12, 2.0f), start));
    EXPECT_EQ(f.grid.getRowLength(kHat), 12) << "snaps to the step boundary";
    EXPECT_EQ(f.grid.ghostSourceStep(f.row(), 12), 0) << "the ghosts follow the drag";
    EXPECT_TRUE(f.c().drumRows.empty()) << "nothing is written while dragging";
    EXPECT_FALSE(f.undo.canUndo());

    f.grid.mouseDrag(leftDrag(f.grid, {f.boundary(9).x + 5.0f, f.boundary(9).y}, start));
    const int previewed = f.grid.getRowLength(kHat);
    EXPECT_EQ(previewed, 9);
    f.grid.mouseUp(leftClick(f.grid, f.boundary(9)));
    EXPECT_FALSE(f.grid.isEdgeDragging());
    EXPECT_EQ(f.storedLength(), previewed) << "committed exactly what was previewed";
    EXPECT_EQ(f.grid.getRowLength(kHat), previewed);
    f.undo.undo();
    EXPECT_TRUE(f.c().drumRows.empty()) << "the whole drag was one undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineEdgeDragTest, ADragNeverPicksALengthBelowOneStep) {
    RowFixture f;
    const auto start = centreOf(f.grid.edgeHandleBounds(f.row()));
    f.grid.mouseDown(leftClick(f.grid, start));
    f.grid.mouseDrag(leftDrag(f.grid, {-50.0f, start.y}, start));
    EXPECT_EQ(f.grid.getRowLength(kHat), 1);
    f.grid.mouseUp(leftClick(f.grid, start));
    EXPECT_EQ(f.storedLength(), 1);
}

TEST(DrumMachineEdgeDragTest, DraggingToOrPastTheClipEndClearsTheLengthInOneUndoStep) {
    RowFixture f;
    f.setLength(12);
    const auto start = centreOf(f.grid.edgeHandleBounds(f.row()));
    f.grid.mouseDown(leftClick(f.grid, start));
    f.grid.mouseDrag(leftDrag(f.grid, f.boundary(kSteps), start));
    EXPECT_EQ(f.grid.getRowLength(kHat), 0) << "the clip's own end means no length";
    EXPECT_EQ(f.grid.ghostSourceStep(f.row(), 20), -1);
    f.grid.mouseDrag(leftDrag(f.grid, {static_cast<float>(f.grid.getWidth()) + 400.0f, start.y}, start));
    f.grid.mouseUp(leftClick(f.grid, start));
    EXPECT_TRUE(f.c().drumRows.empty());
    f.undo.undo();
    EXPECT_EQ(f.storedLength(), 12);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineEdgeDragTest, TheClipEndHandleOfAnyRowStartsALength) {
    RowFixture f;
    const int snareRow = f.grid.rowOfPitch(38);
    const auto start = centreOf(f.grid.edgeHandleBounds(snareRow));
    f.grid.mouseDown(leftClick(f.grid, start));
    f.grid.mouseDrag(leftDrag(f.grid, {static_cast<float>(f.grid.beatToX(6 * kStep)), start.y}, start));
    f.grid.mouseUp(leftClick(f.grid, start));
    ASSERT_EQ(f.c().drumRows.size(), 1u);
    EXPECT_EQ(f.c().drumRows[0].pitch, 38);
    EXPECT_EQ(f.c().drumRows[0].lengthSteps, 6);
}

TEST(DrumMachineEdgeDragTest, EscapeDuringADragDropsItWithoutAnEdit) {
    RowFixture f;
    f.setLength(12);
    const auto start = centreOf(f.grid.edgeHandleBounds(f.row()));
    f.grid.mouseDown(leftClick(f.grid, start));
    f.grid.mouseDrag(leftDrag(f.grid, f.boundary(5), start));
    EXPECT_EQ(f.grid.getRowLength(kHat), 5);
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(f.grid.getRowLength(kHat), 12);
    EXPECT_EQ(f.storedLength(), 12);
    EXPECT_FALSE(f.undo.canUndo());
    f.grid.mouseUp(leftClick(f.grid, start));
    EXPECT_FALSE(f.undo.canUndo());
}

//==============================================================================
// The row menu

TEST(DrumMachineRowMenuTest, RightClickOnTheNameOpensTheMenuForThatRowWithoutSelectingIt) {
    RowFixture f;
    MenuCapture capture;
    ASSERT_NE(f.grid.getSelectedPitch(), kHat);
    rightClickName(f, kHat);
    EXPECT_EQ(capture.shown, 1);
    EXPECT_EQ(capture.pitch, kHat);
    EXPECT_NE(f.grid.getSelectedPitch(), kHat) << "a right-click is not a press";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineRowMenuTest, TheMenuListsLengthSwingAndResetWithTheCurrentValuesTicked) {
    RowFixture f;
    MenuCapture capture;
    rightClickName(f, kHat);
    EXPECT_TRUE(findItem(capture.menu, "Length").found);
    EXPECT_TRUE(findItem(capture.menu, "Swing").found);
    EXPECT_FALSE(findItem(capture.menu, "Reset").enabled) << "nothing to reset yet";
    EXPECT_TRUE(findItem(capture.menu, "Clip length").ticked);
    EXPECT_TRUE(findItem(capture.menu, "Clip swing").ticked);
    EXPECT_TRUE(findItem(capture.menu, "1 step").found);
    EXPECT_TRUE(findItem(capture.menu, "32 steps").found) << "every step count up to the clip's";
    EXPECT_FALSE(findItem(capture.menu, "33 steps").found);
    for (const int percent : {0, 10, 50, 100})
        EXPECT_TRUE(findItem(capture.menu, juce::String(percent) + "%").found) << percent;
    EXPECT_FALSE(findItem(capture.menu, "5%").found);

    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, kHat, 12));
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, kHat, 0.6));
    f.grid.refreshFromDoc();
    rightClickName(f, kHat);
    EXPECT_TRUE(findItem(capture.menu, "12 steps").ticked);
    EXPECT_FALSE(findItem(capture.menu, "Clip length").ticked);
    EXPECT_TRUE(findItem(capture.menu, "40%").ticked);
    EXPECT_FALSE(findItem(capture.menu, "Clip swing").ticked);
    EXPECT_TRUE(findItem(capture.menu, "Reset").enabled);
}

TEST(DrumMachineRowMenuTest, ChoosingALengthOrASwingIsOneUndoStepEach) {
    RowFixture f;
    MenuCapture capture;
    rightClickName(f, kHat);
    capture.choose(findItem(capture.menu, "12 steps").id);
    EXPECT_EQ(f.storedLength(), 12);
    EXPECT_EQ(f.grid.getLastAnnouncement(), "Closed hat length 12 steps");
    f.undo.undo();
    EXPECT_TRUE(f.c().drumRows.empty());
    EXPECT_FALSE(f.undo.canUndo());

    capture.choose(findItem(capture.menu, "40%").id);
    ASSERT_EQ(f.c().drumRows.size(), 1u);
    EXPECT_DOUBLE_EQ(f.c().drumRows[0].swing, DrumMachineComponent::percentToSwing(40.0));
    EXPECT_EQ(f.grid.getLastAnnouncement(), "Closed hat swing 40%");
    f.undo.undo();
    EXPECT_TRUE(f.c().drumRows.empty());
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineRowMenuTest, ClipLengthClipSwingAndTheClipsOwnStepCountFollowTheClip) {
    RowFixture f;
    MenuCapture capture;
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, kHat, 12));
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, kHat, 0.7));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    rightClickName(f, kHat);
    capture.choose(findItem(capture.menu, "Clip length").id);
    EXPECT_EQ(f.storedLength(), 0);
    capture.choose(findItem(capture.menu, "Clip swing").id);
    EXPECT_TRUE(f.c().drumRows.empty());
    f.undo.undo();
    f.undo.undo();
    ASSERT_EQ(f.c().drumRows.size(), 1u);
    EXPECT_EQ(f.c().drumRows[0].lengthSteps, 12);
    capture.choose(findItem(capture.menu, "32 steps").id);
    EXPECT_EQ(f.storedLength(), 0) << "as long as the clip is no length of its own";
}

TEST(DrumMachineRowMenuTest, ResetClearsBothInOneUndoStep) {
    RowFixture f;
    MenuCapture capture;
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, kHat, 12));
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, kHat, 0.7));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    rightClickName(f, kHat);
    capture.choose(findItem(capture.menu, "Reset").id);
    EXPECT_TRUE(f.c().drumRows.empty());
    EXPECT_EQ(f.grid.getLastAnnouncement(), "Reset Closed hat length and swing");
    f.undo.undo();
    ASSERT_EQ(f.c().drumRows.size(), 1u);
    EXPECT_EQ(f.c().drumRows[0].lengthSteps, 12);
    EXPECT_DOUBLE_EQ(f.c().drumRows[0].swing, 0.7);
    EXPECT_FALSE(f.undo.canUndo()) << "length and swing came back together";
}

TEST(DrumMachineRowMenuTest, ShiftF10OnTheGridOpensTheMenuForTheFocusedRow) {
    RowFixture f;
    MenuCapture capture;
    f.grid.setFocusCell({f.grid.rowOfPitch(38), 3});
    EXPECT_TRUE(synth::ui::openContextMenuForFocusedComponent(&f.grid));
    EXPECT_EQ(capture.shown, 1);
    EXPECT_EQ(capture.pitch, 38);
    EXPECT_EQ(synth::ui::openContextMenuForFocusedComponent(f.grid.getRowLabels().getNameButton(kHat)), true)
        << "a focused name button reaches the same provider through its parents";
}

TEST(DrumMachineRowMenuTest, NoMenuWithoutAnOpenClip) {
    RowFixture f;
    MenuCapture capture;
    f.grid.closeGrid();
    EXPECT_FALSE(f.grid.showContextMenuForKeyboardFocus());
    EXPECT_EQ(capture.shown, 0);
}

//==============================================================================
// The Alt+arrow actions

TEST(DrumMachineRowKeysTest, TheFourActionsAreRegisteredInTheDrumMachineCategoryWithTheirDefaults) {
    ShortcutManager manager;
    const juce::ModifierKeys alt(juce::ModifierKeys::altModifier);
    const juce::ModifierKeys altShift(juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier);
    EXPECT_EQ(manager.getBinding("drumMachineRowShorter"), juce::KeyPress(juce::KeyPress::leftKey, alt, 0));
    EXPECT_EQ(manager.getBinding("drumMachineRowLonger"), juce::KeyPress(juce::KeyPress::rightKey, alt, 0));
    EXPECT_EQ(manager.getBinding("drumMachineRowShorterBar"), juce::KeyPress(juce::KeyPress::leftKey, altShift, 0));
    EXPECT_EQ(manager.getBinding("drumMachineRowLongerBar"), juce::KeyPress(juce::KeyPress::rightKey, altShift, 0));
    for (const char* id :
         {"drumMachineRowShorter", "drumMachineRowLonger", "drumMachineRowShorterBar", "drumMachineRowLongerBar"}) {
        EXPECT_EQ(ShortcutManager::getCategory(id), ShortcutCategory::DrumMachine) << id;
        EXPECT_TRUE(ShortcutManager::getActionDescription(id).isNotEmpty()) << id;
    }
}

TEST(DrumMachineRowKeysTest, AltArrowsChangeTheFocusedRowByAStepAndAltShiftByABarOneUndoStepEach) {
    RowFixture f;
    ShortcutManager manager;
    f.grid.setShortcutManager(&manager);
    const auto press = [&](int code, bool shift) { return dispatchKey(f.grid, altKey(code, shift)); };

    EXPECT_TRUE(press(juce::KeyPress::rightKey, false)) << "consumed even when there is nothing to lengthen";
    EXPECT_TRUE(f.c().drumRows.empty());
    EXPECT_FALSE(f.undo.canUndo());

    EXPECT_TRUE(press(juce::KeyPress::leftKey, false));
    EXPECT_EQ(f.storedLength(), 31);
    EXPECT_TRUE(press(juce::KeyPress::leftKey, false));
    EXPECT_EQ(f.storedLength(), 30);
    EXPECT_EQ(f.grid.getLastAnnouncement(), "Closed hat length 30 steps");
    EXPECT_TRUE(press(juce::KeyPress::leftKey, true));
    EXPECT_EQ(f.storedLength(), 14) << "a bar is 16 steps";
    EXPECT_TRUE(press(juce::KeyPress::rightKey, false));
    EXPECT_EQ(f.storedLength(), 15);
    EXPECT_TRUE(press(juce::KeyPress::rightKey, true));
    EXPECT_EQ(f.storedLength(), 31);
    EXPECT_TRUE(press(juce::KeyPress::rightKey, false));
    EXPECT_TRUE(f.c().drumRows.empty()) << "reaching the clip's end clears the length";

    for (int i = 0; i < 6; ++i)
        f.undo.undo();
    EXPECT_FALSE(f.undo.canUndo()) << "six presses, six undo steps";
    EXPECT_TRUE(f.c().drumRows.empty());
}

TEST(DrumMachineRowKeysTest, ShorteningStopsAtOneStepAndMakesNoUndoStepThere) {
    RowFixture f;
    ShortcutManager manager;
    f.grid.setShortcutManager(&manager);
    f.setLength(2);
    EXPECT_TRUE(f.grid.keyPressed(altKey(juce::KeyPress::leftKey)));
    EXPECT_EQ(f.storedLength(), 1);
    f.undo.clearUndoHistory();
    EXPECT_TRUE(f.grid.keyPressed(altKey(juce::KeyPress::leftKey)));
    EXPECT_TRUE(f.grid.keyPressed(altKey(juce::KeyPress::leftKey, true)));
    EXPECT_EQ(f.storedLength(), 1);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineRowKeysTest, TheKeysFollowARebindAndTheOldChordsFallThrough) {
    RowFixture f;
    ShortcutManager manager;
    f.grid.setShortcutManager(&manager);
    manager.setBinding("drumMachineRowShorter", juce::KeyPress('q'));
    EXPECT_FALSE(f.grid.keyPressed(altKey(juce::KeyPress::leftKey))) << "the old chord is free for the app again";
    EXPECT_TRUE(f.c().drumRows.empty());
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress('q')));
    EXPECT_EQ(f.storedLength(), 31);
    EXPECT_TRUE(f.grid.getTooltip().isEmpty() == false);
}

TEST(DrumMachineRowKeysTest, WithoutAManagerTheDefaultChordsStillWork) {
    RowFixture f;
    EXPECT_TRUE(f.grid.keyPressed(altKey(juce::KeyPress::leftKey)));
    EXPECT_EQ(f.storedLength(), 31);
}

TEST(DrumMachineRowKeysTest, AltArrowsFromARowNameButtonReachTheGridAndActOnThatRow) {
    RowFixture f;
    ShortcutManager manager;
    f.grid.setShortcutManager(&manager);
    juce::Component& name = *f.grid.getRowLabels().getNameButton(kHat);
    EXPECT_TRUE(dispatchKey(name, altKey(juce::KeyPress::leftKey)));
    EXPECT_EQ(f.storedLength(), 31);
}

TEST(DrumMachineRowKeysTest, AltArrowsReachTheGridInsideTheTimelinePanelBeforeTheClipMoveChords) {
    TimelineDoc doc;
    AppUndoManager undo;
    AuditionRecordingHost host;
    synth::TransportService transport;
    synth::ui::TimelinePanelComponent panel;
    ShortcutManager manager;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
    panel.setSize(1200, 360);
    panel.setTrackHeaderHost(&host);
    panel.setTransport(&transport);
    panel.setTimelineDoc(&doc);
    panel.setUndoManager(&undo);
    panel.setShortcutManager(&manager);
    panel.getDrumMachine().setShortcutManager(&manager);
    const auto track = doc.addTrack(TrackKind::Midi, "Drums");
    const auto clip = doc.addClip(track, 4.0, 4.0, "Beat 1");
    panel.openDrumMachine(clip);
    auto& grid = panel.getDrumMachine();
    ASSERT_TRUE(grid.isOpen());
    const double startBeat = doc.getClip(clip)->startBeat;

    EXPECT_TRUE(dispatchKey(grid, altKey(juce::KeyPress::leftKey)));
    ASSERT_EQ(doc.getClip(clip)->drumRows.size(), 1u);
    EXPECT_EQ(doc.getClip(clip)->drumRows[0].lengthSteps, 15);
    EXPECT_DOUBLE_EQ(doc.getClip(clip)->startBeat, startBeat) << "the clip itself did not move";
    panel.setTrackHeaderHost(nullptr);
}

//==============================================================================
// Names

TEST(DrumMachineRowLabelsTest, TheNameButtonsTitleAndTooltipSayWhatIsSet) {
    RowFixture f;
    auto* name = f.grid.getRowLabels().getNameButton(kHat);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->getTitle(), "Closed hat row");
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, kHat, 12));
    f.grid.refreshFromDoc();
    EXPECT_EQ(name->getTitle(), "Closed hat row, length, 12 steps");
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, kHat, 0.6));
    f.grid.refreshFromDoc();
    EXPECT_EQ(name->getTitle(), "Closed hat row, length, 12 steps, swing 40%");
    EXPECT_TRUE(name->getTooltip().contains("Right-click")) << name->getTooltip();
    EXPECT_TRUE(name->getTooltip().contains("Shift+F10"));
    EXPECT_EQ(f.grid.getRowLabels().getNameButton(38)->getTitle(), "Snare row");
}

//==============================================================================
// Motion

TEST(DrumMachineRowMotionTest, ShorteningFadesTheNewGhostsInAndSlidesTheEdge) {
    AnimationModeGuard mode(AnimationMode::full);
    {
        RowFixture f;
        DrumMachineComponent::setAnimateOffScreenForTest(true);
        f.grid.finishMotionsForTest();
        ASSERT_TRUE(f.doc.setClipRowLength(f.clip, kHat, 12));
        f.grid.refreshFromDoc();
        EXPECT_TRUE(f.grid.isGhostFadeActive(f.row(), 12));
        EXPECT_TRUE(f.grid.isGhostFadeActive(f.row(), 31));
        EXPECT_FALSE(f.grid.isGhostFadeActive(f.row(), 11)) << "only the cells that became ghosts";
        EXPECT_DOUBLE_EQ(f.grid.getEdgePosition(kHat), 32.0) << "the edge starts where the clip ended";
        f.grid.finishMotionsForTest();
        EXPECT_FALSE(f.grid.isGhostFadeActive(f.row(), 12));
        EXPECT_DOUBLE_EQ(f.grid.getEdgePosition(kHat), 12.0);
    }
    DrumMachineComponent::setAnimateOffScreenForTest(false);
}

TEST(DrumMachineRowMotionTest, AnimationsOffLandsTheGhostsAndTheEdgeAtOnce) {
    AnimationModeGuard mode(AnimationMode::off);
    {
        RowFixture f;
        DrumMachineComponent::setAnimateOffScreenForTest(true);
        ASSERT_TRUE(f.doc.setClipRowLength(f.clip, kHat, 12));
        f.grid.refreshFromDoc();
        EXPECT_FALSE(f.grid.isGhostFadeActive(f.row(), 12));
        EXPECT_DOUBLE_EQ(f.grid.getEdgePosition(kHat), 12.0);
    }
    DrumMachineComponent::setAnimateOffScreenForTest(false);
}

TEST(DrumMachineRowMotionTest, ReduceMotionFadesTheGhostsAndJumpsTheEdge) {
    AnimationModeGuard mode(AnimationMode::reduced);
    {
        RowFixture f;
        DrumMachineComponent::setAnimateOffScreenForTest(true);
        f.grid.finishMotionsForTest();
        ASSERT_TRUE(f.doc.setClipRowLength(f.clip, kHat, 12));
        f.grid.refreshFromDoc();
        EXPECT_TRUE(f.grid.isGhostFadeActive(f.row(), 12)) << "the 80 ms fade stays";
        EXPECT_DOUBLE_EQ(f.grid.getEdgePosition(kHat), 12.0) << "the edge does not slide";
    }
    DrumMachineComponent::setAnimateOffScreenForTest(false);
}

TEST(DrumMachineRowMotionTest, ADragMovesTheEdgeWithThePointerAndNeverSlidesIt) {
    AnimationModeGuard mode(AnimationMode::full);
    {
        RowFixture f;
        DrumMachineComponent::setAnimateOffScreenForTest(true);
        f.grid.finishMotionsForTest();
        const auto start = centreOf(f.grid.edgeHandleBounds(f.row()));
        f.grid.mouseDown(leftClick(f.grid, start));
        f.grid.mouseDrag(leftDrag(f.grid, f.boundary(12), start));
        EXPECT_DOUBLE_EQ(f.grid.getEdgePosition(kHat), 12.0);
        EXPECT_TRUE(f.grid.isGhostFadeActive(f.row(), 20)) << "ghosts fade in as the drum shortens";
        f.grid.mouseUp(leftClick(f.grid, f.boundary(12)));
        EXPECT_DOUBLE_EQ(f.grid.getEdgePosition(kHat), 12.0) << "the commit does not move it again";
    }
    DrumMachineComponent::setAnimateOffScreenForTest(false);
}
