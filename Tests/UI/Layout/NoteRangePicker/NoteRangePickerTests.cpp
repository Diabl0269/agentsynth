// NoteRangePickerTests.cpp -- the shared note range picker: edge drags add or remove notes and snap to the scale (and
// scroll the strip at its ends), the middle moves the range note by note (Shift: octaves), a click on empty keys moves
// it there, the overview bar scrolls, a note name types a note, the range stays ordered, and the keys reach every part.
#include "../../ModuleViews/StepGrid/StepGridCardFixture.h"
#include "../../PianoRoll/PianoRollKeyPickerHelpers.h"
#include "../../PianoRoll/PianoRollTestHelpers.h"
#include "Music/NoteNames.h"
#include "UI/Layout/NoteRangePicker/NoteRangeGeometry.h"
#include "UI/Layout/NoteRangePicker/NoteRangePicker.h"
#include "UI/ModuleViews/StepGrid/StepGridRandomizePanel.h"
#include <gtest/gtest.h>

using synth::ui::NoteRangePicker;
using Part = NoteRangePicker::Part;

namespace {

juce::MouseEvent mouseAt(NoteRangePicker& comp, float x,
                         juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, float y = 17.0f) {
    const juce::Point<float> p(x, y);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
}

struct Fixture {
    NoteRangePicker picker;
    int changes = 0;
    int lastLo = -1;
    int lastHi = -1;
    int gestureEnds = 0;
    int gestureLo = -1;
    int gestureHi = -1;

    Fixture() {
        picker.onGestureEnd = [this](int lo, int hi) {
            ++gestureEnds;
            gestureLo = lo;
            gestureHi = hi;
        };
        picker.setSize(280, 34);
        picker.setRange(48, 60); // C3 to C4 when middle C is C4
        picker.onChange = [this](int lo, int hi) {
            ++changes;
            lastLo = lo;
            lastHi = hi;
        };
    }

    float lowEdgeX() { return picker.keyCentreX(picker.getLow()) - picker.whiteKeyWidth() * 0.5f; }
    float highEdgeX() { return picker.keyCentreX(picker.getHigh()) + picker.whiteKeyWidth() * 0.5f; }

    void drag(float fromX, float toX, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
        picker.mouseDown(mouseAt(picker, fromX, mods));
        picker.mouseDrag(mouseAt(picker, (fromX + toX) * 0.5f, mods));
        picker.mouseDrag(mouseAt(picker, toX, mods));
        picker.mouseUp(mouseAt(picker, toX, mods));
    }

    void press(Part part, int key, juce::ModifierKeys mods = {}) {
        picker.getStop(part).keyPressed(juce::KeyPress(key, mods, 0));
    }
};

} // namespace

TEST(NoteRangePicker, SetRangeShowsTheNotesWithoutNotifying) {
    Fixture f;
    f.picker.setRange(60, 48); // reversed
    EXPECT_EQ(f.picker.getLow(), 48);
    EXPECT_EQ(f.picker.getHigh(), 60);
    EXPECT_EQ(f.changes, 0);
    EXPECT_EQ(f.picker.getTitle(), "Note range, C3 to C4");
}

TEST(NoteRangePicker, TheKeysShownFollowTheRangeAndStopAtTheEndsOfTheKeyboard) {
    using namespace synth::ui::noterange;
    Fixture f;
    f.picker.setRange(100, 112);
    EXPECT_TRUE(isInView(rangeExtent(100, 112), f.picker.getScroll(), f.picker.getVisibleKeys()));
    f.picker.setRange(0, 12);
    EXPECT_EQ(f.picker.getScroll(), 0.0f);
    f.picker.setRange(120, 127);
    EXPECT_EQ(f.picker.getScroll(), maxScroll(f.picker.getVisibleKeys())) << "no keys drawn above G9";
}

TEST(NoteRangePicker, KeysAreAtLeastSevenPixelsWideAndANarrowPanelShowsFewerOctaves) {
    NoteRangePicker narrow;
    narrow.setSize(158, 45); // the piano roll's panel
    EXPECT_EQ(narrow.getVisibleKeys(), 22);
    EXPECT_GE(narrow.whiteKeyWidth(), 7.0f);
    NoteRangePicker wide;
    wide.setSize(230, 45); // Randomize
    EXPECT_EQ(wide.getVisibleKeys(), 28);
    EXPECT_GE(wide.whiteKeyWidth(), 7.0f);
}

TEST(NoteRangePicker, DraggingAnEdgeAddsAndRemovesNotes) {
    Fixture f;
    const float start = f.highEdgeX();
    const float target = f.picker.keyCentreX(67) + f.picker.whiteKeyWidth() * 0.5f; // the edge of G
    f.drag(start, target);
    EXPECT_EQ(f.picker.getHigh(), 67);
    EXPECT_EQ(f.picker.getLow(), 48);
    EXPECT_GE(f.changes, 1);
    EXPECT_EQ(f.lastHi, 67);

    const float lowStart = f.lowEdgeX();
    f.drag(lowStart, f.picker.keyCentreX(52) - f.picker.whiteKeyWidth() * 0.5f);
    EXPECT_EQ(f.picker.getLow(), 52);
}

TEST(NoteRangePicker, AnEdgeDragSnapsToScaleNotes) {
    Fixture f;
    const auto cMajor = synth::makeScale(0, 0);
    f.picker.setScale(&cMajor);
    f.picker.setRange(48, 60);
    // F sharp's black key is the nearest key; F sharp is out of C major, and the tie goes to the lower F.
    const float target = f.picker.keyCentreX(66) + f.picker.whiteKeyWidth() * 0.5f;
    f.drag(f.highEdgeX(), target);
    EXPECT_TRUE(cMajor.contains(f.picker.getHigh()));
    EXPECT_NE(f.picker.getHigh(), 60);
}

TEST(NoteRangePicker, TheRangeStaysOrderedAndKeepsOneNote) {
    Fixture f;
    f.drag(f.highEdgeX(), f.picker.keyCentreX(40)); // far past the low edge
    EXPECT_GE(f.picker.getHigh(), f.picker.getLow());
    f.picker.setRange(48, 60);
    f.drag(f.lowEdgeX(), f.picker.keyCentreX(70));
    EXPECT_LE(f.picker.getLow(), f.picker.getHigh());
    EXPECT_GE(f.picker.getHigh() - f.picker.getLow(), 0);
}

TEST(NoteRangePicker, DraggingTheMiddleFollowsThePointerNoteByNote) {
    Fixture f;
    f.drag(f.picker.keyCentreX(54), f.picker.keyCentreX(57)); // grab on F sharp's neighbour, move three notes
    EXPECT_EQ(f.picker.getLow(), 51);
    EXPECT_EQ(f.picker.getHigh(), 63);
    EXPECT_EQ(f.lastLo, 51);
    EXPECT_EQ(f.gestureEnds, 1);
    f.drag(f.picker.keyCentreX(54), f.picker.keyCentreX(53));
    EXPECT_EQ(f.picker.getLow(), 50) << "and back down a note";
}

TEST(NoteRangePicker, ShiftDraggingTheMiddleMovesByOctaves) {
    Fixture f;
    const auto shift = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    f.drag(f.picker.keyCentreX(54), f.picker.keyCentreX(57), shift);
    EXPECT_EQ(f.picker.getLow(), 48) << "three notes round to no octave";
    f.drag(f.picker.keyCentreX(54), f.picker.keyCentreX(62), shift);
    EXPECT_EQ(f.picker.getLow(), 60);
    EXPECT_EQ(f.picker.getHigh(), 72);
}

TEST(NoteRangePicker, TheMiddleCanBeGrabbedAnywhereInsideTheBand) {
    Fixture f;
    f.picker.setRange(48, 72); // two octaves
    const float near = f.picker.keyCentreX(50);
    f.drag(near, near + f.picker.whiteKeyWidth() * 2.0f);
    EXPECT_GT(f.picker.getLow(), 48);
    EXPECT_EQ(f.picker.getHigh() - f.picker.getLow(), 24) << "the width is kept";
}

TEST(NoteRangePicker, DraggingTheMiddleWithAScaleMovesByScaleNotes) {
    Fixture f;
    const auto cMajor = synth::makeScale(0, 0);
    f.picker.setScale(&cMajor);
    f.picker.setRange(48, 60);
    f.drag(f.picker.keyCentreX(50), f.picker.keyCentreX(52)); // D to E: one scale note
    EXPECT_EQ(f.picker.getLow(), 50);
    EXPECT_EQ(f.picker.getHigh(), 62);
    EXPECT_TRUE(cMajor.contains(f.picker.getLow()));
    EXPECT_TRUE(cMajor.contains(f.picker.getHigh()));
}

TEST(NoteRangePicker, AClickOnEmptyKeysMovesTheRangeThereAtTheSameWidth) {
    Fixture f;
    f.picker.setRange(48, 60);
    const int before = f.picker.getHigh() - f.picker.getLow();
    const float x = f.picker.keyCentreX(70);
    f.picker.mouseDown(mouseAt(f.picker, x));
    f.picker.mouseUp(mouseAt(f.picker, x));
    EXPECT_EQ(f.picker.getHigh() - f.picker.getLow(), before);
    EXPECT_NEAR((f.picker.getLow() + f.picker.getHigh()) * 0.5, 70.0, 1.0);
    EXPECT_EQ(f.gestureEnds, 1) << "one undo step";
    EXPECT_GE(f.changes, 1);
}

TEST(NoteRangePicker, AClickOnTheBandItselfChangesNothing) {
    Fixture f;
    const float x = f.picker.keyCentreX(54);
    f.picker.mouseDown(mouseAt(f.picker, x));
    f.picker.mouseUp(mouseAt(f.picker, x));
    EXPECT_EQ(f.picker.getLow(), 48);
    EXPECT_EQ(f.gestureEnds, 0);
}

TEST(NoteRangePicker, DraggingAnEdgeToTheTopOfTheStripScrollsTheStripUpToTheHighestNote) {
    Fixture f;
    const float width = (float)f.picker.getWidth();
    f.picker.mouseDown(mouseAt(f.picker, f.highEdgeX()));
    f.picker.dragForTest(f.highEdgeX() + 6.0f, 0.016, {});
    const float scrollBefore = f.picker.getScroll();
    float last = scrollBefore;
    for (int i = 0; i < 400; ++i) {
        f.picker.dragForTest(width + 20.0f, 0.1, {}); // held past the end
        EXPECT_GE(f.picker.getScroll(), last) << "the strip only moves one way";
        last = f.picker.getScroll();
    }
    f.picker.mouseUp(mouseAt(f.picker, width));
    EXPECT_GT(f.picker.getScroll(), scrollBefore);
    EXPECT_EQ(f.picker.getHigh(), 127) << "the highest note is reachable";
    EXPECT_EQ(f.picker.getLow(), 48);
    EXPECT_EQ(f.picker.getScroll(), synth::ui::noterange::maxScroll(f.picker.getVisibleKeys()));
    EXPECT_EQ(f.gestureEnds, 1);
}

TEST(NoteRangePicker, DraggingAnEdgeToTheBottomOfTheStripScrollsDownToTheLowestNote) {
    Fixture f;
    f.picker.mouseDown(mouseAt(f.picker, f.lowEdgeX()));
    f.picker.dragForTest(f.lowEdgeX() - 6.0f, 0.016, {});
    for (int i = 0; i < 400; ++i)
        f.picker.dragForTest(-20.0f, 0.1, {});
    f.picker.mouseUp(mouseAt(f.picker, 0.0f));
    EXPECT_EQ(f.picker.getLow(), 0);
    EXPECT_EQ(f.picker.getHigh(), 60);
    EXPECT_EQ(f.picker.getScroll(), 0.0f);
}

TEST(NoteRangePicker, TheBandDraggedToTheEndOfTheStripPushesTheKeyboardAlongAndKeepsItsWidth) {
    Fixture f;
    f.picker.mouseDown(mouseAt(f.picker, f.picker.keyCentreX(54)));
    f.picker.dragForTest(f.picker.keyCentreX(54) + 8.0f, 0.016, {});
    for (int i = 0; i < 400; ++i)
        f.picker.dragForTest((float)f.picker.getWidth() + 5.0f, 0.1, {});
    f.picker.mouseUp(mouseAt(f.picker, (float)f.picker.getWidth()));
    EXPECT_EQ(f.picker.getHigh(), 127);
    EXPECT_EQ(f.picker.getHigh() - f.picker.getLow(), 12);
}

TEST(NoteRangePicker, TheStripScrollsFasterTheFurtherPastTheEndTheDragGoes) {
    const auto travelled = [](float past) {
        Fixture f;
        f.picker.mouseDown(mouseAt(f.picker, f.highEdgeX()));
        f.picker.dragForTest(f.highEdgeX() + 6.0f, 0.016, {});
        const float start = f.picker.getScroll();
        f.picker.dragForTest((float)f.picker.getWidth() + past, 0.1, {});
        return f.picker.getScroll() - start;
    };
    EXPECT_GT(travelled(5.0f), 0.0f);
    EXPECT_GT(travelled(60.0f), travelled(5.0f));
}

TEST(NoteRangePicker, APressOnAnEdgeAtTheStripsEndDoesNotScrollUntilItMoves) {
    Fixture f;
    f.picker.setRange(0, 5);
    f.picker.mouseDown(mouseAt(f.picker, f.lowEdgeX()));
    f.picker.dragForTest(f.lowEdgeX(), 0.5, {});
    EXPECT_EQ(f.picker.getScroll(), 0.0f);
    f.picker.mouseUp(mouseAt(f.picker, f.lowEdgeX()));
    EXPECT_EQ(f.gestureEnds, 0);
}

TEST(NoteRangePicker, TheOverviewBarShowsOtherOctavesWithoutTouchingTheRange) {
    using namespace synth::ui::noterange;
    NoteRangePicker picker;
    int gestureEnds = 0;
    picker.onGestureEnd = [&](int, int) { ++gestureEnds; };
    picker.setSize(280, NoteRangePicker::kPreferredHeight);
    picker.setRange(48, 60);
    const float before = picker.getScroll();
    const float x = overviewX(70.0f, 280.0f);
    picker.mouseDown(mouseAt(picker, x, juce::ModifierKeys::leftButtonModifier, 4.0f));
    EXPECT_GT(picker.getScroll(), before);
    picker.mouseDrag(mouseAt(picker, overviewX(75.0f, 280.0f), juce::ModifierKeys::leftButtonModifier, 4.0f));
    picker.mouseUp(mouseAt(picker, x, juce::ModifierKeys::leftButtonModifier, 4.0f));
    EXPECT_EQ(picker.getScroll(), maxScroll(picker.getVisibleKeys()));
    EXPECT_EQ(picker.getLow(), 48);
    EXPECT_EQ(picker.getHigh(), 60);
    EXPECT_EQ(gestureEnds, 0);
    EXPECT_NEAR(picker.overviewFrame().getX(), overviewX(picker.getScroll(), 280.0f), 0.01f);
}

TEST(NoteRangePicker, PageUpAndPageDownShowTheOctaveAboveAndBelow) {
    Fixture f;
    const float start = f.picker.getScroll();
    f.press(Part::Band, juce::KeyPress::pageUpKey);
    f.picker.finishMotionForTest();
    EXPECT_FLOAT_EQ(f.picker.getScroll(), start + 7.0f);
    f.press(Part::LowEdge, juce::KeyPress::pageDownKey);
    f.picker.finishMotionForTest();
    EXPECT_FLOAT_EQ(f.picker.getScroll(), start);
    EXPECT_EQ(f.picker.getLow(), 48) << "the range is untouched";
    EXPECT_EQ(f.gestureEnds, 0);
}

TEST(NoteRangePicker, AClickOnANoteNameTypesAnExactNote) {
    Fixture f;
    const auto band = f.picker.bandBounds();
    const float x = (float)band.getX() + 8.0f; // inside the low name
    const float y = 34.0f - 7.0f;
    f.picker.mouseDown(mouseAt(f.picker, x, juce::ModifierKeys::leftButtonModifier, y));
    f.picker.mouseUp(mouseAt(f.picker, x, juce::ModifierKeys::leftButtonModifier, y));
    ASSERT_TRUE(f.picker.isEditingNote());
    EXPECT_EQ(f.picker.getNoteEditor().getText(), "C3");
    f.picker.getNoteEditor().setText("e3", false);
    f.picker.getNoteEditor().onReturnKey();
    EXPECT_FALSE(f.picker.isEditingNote());
    EXPECT_EQ(f.picker.getLow(), 52);
    EXPECT_EQ(f.picker.getHigh(), 60);
    EXPECT_EQ(f.gestureEnds, 1);
}

TEST(NoteRangePicker, ATypedEdgeThatPassesTheOtherPushesItAndANameThatDoesNotParseStaysOpen) {
    Fixture f;
    f.picker.beginNoteEdit(Part::LowEdge);
    f.picker.getNoteEditor().setText("C#5", false);
    f.picker.getNoteEditor().onReturnKey();
    EXPECT_EQ(f.picker.getLow(), 73);
    EXPECT_EQ(f.picker.getHigh(), 73) << "the range always holds a note";
    f.picker.beginNoteEdit(Part::HighEdge);
    f.picker.getNoteEditor().setText("Z9", false);
    f.picker.getNoteEditor().onReturnKey();
    EXPECT_TRUE(f.picker.isEditingNote());
    EXPECT_EQ(f.picker.getHigh(), 73);
    f.picker.getNoteEditor().onEscapeKey();
    EXPECT_FALSE(f.picker.isEditingNote());
}

TEST(NoteRangePicker, EnterOnAnEdgeOpensTheNoteEditorButNotOnTheBand) {
    Fixture f;
    f.press(Part::Band, juce::KeyPress::returnKey);
    EXPECT_FALSE(f.picker.isEditingNote());
    f.press(Part::HighEdge, juce::KeyPress::returnKey);
    EXPECT_TRUE(f.picker.isEditingNote());
    EXPECT_EQ(f.picker.getNoteEditor().getText(), "C4");
}

TEST(NoteRangePicker, ThreeTabStopsInOrderEachNamedWithATooltip) {
    Fixture f;
    EXPECT_EQ(f.picker.getStop(Part::LowEdge).getTitle(), "Lowest note");
    EXPECT_EQ(f.picker.getStop(Part::HighEdge).getTitle(), "Highest note");
    EXPECT_EQ(f.picker.getStop(Part::Band).getTitle(), "Move the whole range");
    for (const auto part : {Part::LowEdge, Part::HighEdge, Part::Band}) {
        auto& stop = f.picker.getStop(part);
        EXPECT_TRUE(stop.getWantsKeyboardFocus());
        const auto tip = dynamic_cast<juce::SettableTooltipClient&>(stop).getTooltip();
        EXPECT_TRUE(tip.startsWith(stop.getTitle())) << "the tooltip names the part, then its keys";
        EXPECT_TRUE(tip.contains("Left and Right")) << tip;
    }
    EXPECT_LT(f.picker.getStop(Part::LowEdge).getExplicitFocusOrder(),
              f.picker.getStop(Part::HighEdge).getExplicitFocusOrder());
    EXPECT_LT(f.picker.getStop(Part::HighEdge).getExplicitFocusOrder(),
              f.picker.getStop(Part::Band).getExplicitFocusOrder());
}

TEST(NoteRangePicker, ArrowKeysMoveTheFocusedPartByOneNoteOrAnOctave) {
    Fixture f;
    f.press(Part::LowEdge, juce::KeyPress::rightKey);
    EXPECT_EQ(f.picker.getLow(), 49);
    f.press(Part::HighEdge, juce::KeyPress::leftKey);
    EXPECT_EQ(f.picker.getHigh(), 59);
    f.press(Part::HighEdge, juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier);
    EXPECT_EQ(f.picker.getHigh(), 71);
    f.press(Part::Band, juce::KeyPress::leftKey);
    EXPECT_EQ(f.picker.getLow(), 48);
    EXPECT_EQ(f.picker.getHigh(), 70);
    f.press(Part::Band, juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier);
    EXPECT_EQ(f.picker.getLow(), 36);
    EXPECT_EQ(f.picker.getHigh(), 58);
    EXPECT_GE(f.changes, 5);
}

TEST(NoteRangePicker, ArrowKeysStepByScaleNotesAndNeverCrossTheOtherEdge) {
    Fixture f;
    const auto cMajor = synth::makeScale(0, 0);
    f.picker.setScale(&cMajor);
    f.picker.setRange(48, 50);
    f.press(Part::LowEdge, juce::KeyPress::rightKey);
    EXPECT_EQ(f.picker.getLow(), 50); // C to D
    f.press(Part::LowEdge, juce::KeyPress::rightKey);
    EXPECT_EQ(f.picker.getLow(), 50); // cannot pass the high edge
    f.press(Part::HighEdge, juce::KeyPress::leftKey);
    EXPECT_EQ(f.picker.getHigh(), 50);
}

TEST(NoteRangePicker, TheGroupReadsTheRangeAndNamesFollowTheMiddleCOctave) {
    Fixture f;
    f.picker.setMiddleCOctave(3);
    f.picker.setRange(48, 60);
    EXPECT_EQ(f.picker.getTitle(), "Note range, C2 to C3");
}

TEST(NoteRangeGeometry, KeysSitWhereAPianoPutsThem) {
    using namespace synth::ui::noterange;
    const float scroll = (float)whiteIndexOf(48);
    const auto c = keySpan(48, scroll, 10.0f);
    const auto cSharp = keySpan(49, scroll, 10.0f);
    const auto d = keySpan(50, scroll, 10.0f);
    EXPECT_FLOAT_EQ(c.x0, 0.0f);
    EXPECT_FLOAT_EQ(cSharp.centre(), c.x1); // a black key sits on the line between its whites
    EXPECT_FLOAT_EQ(d.x0, c.x1);
    EXPECT_EQ(pitchAtX(d.centre(), scroll, 10.0f), 50);
    EXPECT_FLOAT_EQ(keySpan(48 + 48, scroll, 10.0f).x0, 280.0f); // four octaves on
    EXPECT_EQ(whiteIndexOf(127), kTotalWhiteKeys - 1) << "G9 is the last white key";
}

TEST(NoteRangeGeometry, TheWindowFitsSevenPixelKeysAndNeverLeavesTheKeyboard) {
    using namespace synth::ui::noterange;
    EXPECT_EQ(visibleKeysFor(158.0f), 22);
    EXPECT_EQ(visibleKeysFor(230.0f), 28);
    EXPECT_EQ(visibleKeysFor(900.0f), 28);
    EXPECT_EQ(visibleKeysFor(10.0f), 7);
    EXPECT_FLOAT_EQ(clampScroll(-5.0f, 28), 0.0f);
    EXPECT_FLOAT_EQ(clampScroll(500.0f, 28), 47.0f);
    // At the top of the keyboard the last key drawn is G9, ending at the strip's right side.
    EXPECT_FLOAT_EQ(keySpan(127, maxScroll(28), 10.0f).x1, 280.0f);
}

TEST(NoteRangeGeometry, ScrollingUnderADragIsZeroInTheMiddleAndGrowsPastEitherEnd) {
    using namespace synth::ui::noterange;
    const float width = 280.0f;
    const float key = 10.0f;
    EXPECT_FLOAT_EQ(autoScrollSpeed(140.0f, width, key), 0.0f);
    EXPECT_GT(autoScrollSpeed(width - 2.0f, width, key), 0.0f) << "at the strip's end";
    EXPECT_LT(autoScrollSpeed(1.0f, width, key), 0.0f);
    EXPECT_GT(autoScrollSpeed(width + 40.0f, width, key), autoScrollSpeed(width + 5.0f, width, key));
    EXPECT_LT(autoScrollSpeed(-40.0f, width, key), autoScrollSpeed(-5.0f, width, key));
    EXPECT_FLOAT_EQ(autoScrollSpeed(width + 5000.0f, width, key), kScrollMaxKeysPerSecond);
}

TEST(NoteRangeGeometry, RevealingARangeMovesTheWindowAsLittleAsPossible) {
    using namespace synth::ui::noterange;
    const auto in = rangeExtent(48, 60);
    EXPECT_FLOAT_EQ(scrollToReveal(in, (float)whiteIndexOf(36), 28), (float)whiteIndexOf(36));
    const float up = scrollToReveal(rangeExtent(100, 100), 0.0f, 28);
    EXPECT_TRUE(isInView(rangeExtent(100, 100), up, 28));
    EXPECT_GT(up, 30.0f);
    EXPECT_FLOAT_EQ(scrollToReveal(rangeExtent(0, 0), 20.0f, 28), 0.0f);
}

TEST(NoteRangeGeometry, DraggingTheBandMovesItByNotesScaleNotesOrOctavesAndKeepsItOnTheKeyboard) {
    using namespace synth::ui::noterange;
    EXPECT_EQ(dragBandRange(48, 60, 54, 57, false, nullptr), (Range{51, 63}));
    EXPECT_EQ(dragBandRange(48, 60, 54, 51, false, nullptr), (Range{45, 57}));
    EXPECT_EQ(dragBandRange(48, 60, 54, 57, true, nullptr), (Range{48, 60})) << "three notes is not an octave";
    EXPECT_EQ(dragBandRange(48, 60, 54, 63, true, nullptr), (Range{60, 72})) << "nine notes rounds to an octave";
    EXPECT_EQ(dragBandRange(48, 60, 54, 62, true, nullptr), (Range{60, 72}));
    EXPECT_EQ(dragBandRange(120, 127, 123, 200, false, nullptr), (Range{120, 127})) << "stops at G9";
    EXPECT_EQ(dragBandRange(2, 14, 8, -50, false, nullptr), (Range{0, 12})) << "stops at C-1, keeps its width";
    const auto cMajor = synth::makeScale(0, 0);
    EXPECT_EQ(dragBandRange(48, 60, 50, 52, false, &cMajor), (Range{50, 62}));
}

TEST(NoteRangeGeometry, AClickOnEmptyKeysCentresTheSameWidthThere) {
    using namespace synth::ui::noterange;
    EXPECT_EQ(bandCentredOn(80, 48, 60, nullptr), (Range{74, 86}));
    EXPECT_EQ(bandCentredOn(127, 48, 60, nullptr), (Range{115, 127}));
    EXPECT_EQ(bandCentredOn(2, 48, 60, nullptr), (Range{0, 12}));
    const auto cMajor = synth::makeScale(0, 0);
    const auto r = bandCentredOn(80, 48, 60, &cMajor);
    EXPECT_TRUE(cMajor.contains(r.lo));
    EXPECT_TRUE(cMajor.contains(r.hi));
    EXPECT_EQ(scaleStepsBetween(r.lo, r.hi, &cMajor), scaleStepsBetween(48, 60, &cMajor));
}

TEST(NoteRangeGeometry, TheOverviewBarMapsTheWholeKeyboardAndCentresTheWindowOnThePointer) {
    using namespace synth::ui::noterange;
    EXPECT_FLOAT_EQ(overviewX((float)kTotalWhiteKeys, 150.0f), 150.0f);
    EXPECT_FLOAT_EQ(overviewScrollFor(75.0f, 150.0f, 28), 37.5f - 14.0f);
    EXPECT_FLOAT_EQ(overviewScrollFor(0.0f, 150.0f, 28), 0.0f);
    EXPECT_FLOAT_EQ(overviewScrollFor(150.0f, 150.0f, 28), 47.0f);
}

TEST(NoteRangeGeometry, ATypedKeyParsesInTheOwnersOctaveNumbering) {
    using namespace synth::ui::noterange;
    EXPECT_EQ(parseNoteName("C4", 4), 60);
    EXPECT_EQ(parseNoteName("C3", 3), 60);
    EXPECT_EQ(parseNoteName("c#3", 4), 49);
    EXPECT_EQ(parseNoteName(" Bb2 ", 4), 46);
    EXPECT_EQ(parseNoteName("C-1", 4), 0);
    EXPECT_EQ(parseNoteName("G9", 4), 127);
    EXPECT_FALSE(parseNoteName("G#9", 4).has_value()) << "above 127";
    EXPECT_FALSE(parseNoteName("H3", 4).has_value());
    EXPECT_FALSE(parseNoteName("C", 4).has_value());
    EXPECT_FALSE(parseNoteName("", 4).has_value());
    EXPECT_FALSE(parseNoteName("C3x", 4).has_value());
    EXPECT_EQ(parseNoteName(synth::noteNameWithOctave(77, 3).toStdString(), 3), 77) << "names round-trip";
}

// Regression test for FRO1238: a whole drag is one finished gesture, so the owner records one undo step, not one per
// pointer move.
TEST(NoteRangePicker, AWholeDragFiresGestureEndOnceWithTheFinalRange) {
    Fixture f;
    const float octave = f.picker.whiteKeyWidth() * 7.0f;
    const float mid = (f.lowEdgeX() + f.highEdgeX()) * 0.5f;
    f.drag(mid, mid + octave);
    EXPECT_GE(f.changes, 1);
    EXPECT_EQ(f.gestureEnds, 1);
    EXPECT_EQ(f.gestureLo, 60);
    EXPECT_EQ(f.gestureHi, 72);
}

TEST(NoteRangePicker, AClickThatChangesNothingFiresNoGestureEnd) {
    Fixture f;
    const float mid = (f.lowEdgeX() + f.highEdgeX()) * 0.5f;
    f.drag(mid, mid);
    EXPECT_EQ(f.gestureEnds, 0);
    // A drag that comes back to where it began changed nothing either.
    const float octave = f.picker.whiteKeyWidth() * 7.0f;
    f.picker.mouseDown(mouseAt(f.picker, mid));
    f.picker.mouseDrag(mouseAt(f.picker, mid + octave));
    f.picker.mouseDrag(mouseAt(f.picker, mid));
    f.picker.mouseUp(mouseAt(f.picker, mid));
    EXPECT_EQ(f.gestureEnds, 0);
}

TEST(NoteRangePicker, EachKeyStepThatMovesTheRangeFiresOneGestureEnd) {
    Fixture f;
    f.press(Part::LowEdge, juce::KeyPress::rightKey);
    EXPECT_EQ(f.gestureEnds, 1);
    EXPECT_EQ(f.gestureLo, 49);
    f.press(Part::HighEdge, juce::KeyPress::leftKey);
    EXPECT_EQ(f.gestureEnds, 2);
    f.picker.setRange(0, 5);
    f.press(Part::LowEdge, juce::KeyPress::leftKey); // already at the bottom: nothing moves
    EXPECT_EQ(f.gestureEnds, 2);
}

TEST(NoteRangePicker, SetRangeFiresNoGestureEnd) {
    Fixture f;
    f.picker.setRange(40, 70);
    EXPECT_EQ(f.gestureEnds, 0);
}

// ---- The note range band: saved with its owner and undone one gesture at a time ----

namespace {

using synth::ui::NoteRangePicker;

juce::MouseEvent pickerMouse(NoteRangePicker& picker, float x) {
    const juce::Point<float> p(x, 17.0f);
    const auto mods = juce::ModifierKeys::leftButtonModifier;
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &picker, &picker, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
}

// Drags the high edge to the edge of `target` with real mouse events: down, two moves, up.
void dragHighEdgeTo(NoteRangePicker& picker, int target) {
    ASSERT_GT(picker.getWidth(), 0);
    const float half = picker.whiteKeyWidth() * 0.5f;
    const float from = picker.keyCentreX(picker.getHigh()) + half;
    const float to = picker.keyCentreX(target) + half;
    picker.mouseDown(pickerMouse(picker, from));
    picker.mouseDrag(pickerMouse(picker, (from + to) * 0.5f));
    picker.mouseDrag(pickerMouse(picker, to));
    picker.mouseUp(pickerMouse(picker, to));
    ASSERT_EQ(picker.getHigh(), target);
}

} // namespace

// Regression test for FRO1238: the note range of the Randomize panel and of the piano roll's Scale assist panel was
// not part of the saved state, so Cmd+Z skipped it. One guard over both owners: a real drag on the picker, then undo
// restores the range, redo reapplies it, and the drag was exactly one undo step.
TEST(NoteRangeUndoGuard, ADragOnEitherOwnersPickerIsOneUndoStepAndRedoReapplies) {
    struct Owner {
        const char* name;
        std::function<NoteRangePicker&()> picker;
        std::function<std::pair<int, int>()> shown; // what the band shows after the owner refreshed
        std::function<bool()> canUndo;
        std::function<void()> undo;
        std::function<void()> redo;
        std::function<void()> clearHistory;
    };

    // Owner 1: a sequencer card's Randomize popover.
    stepgrid_test::SeqCard card;
    std::unique_ptr<synth::ui::StepGridRandomizePanel> popover;
    const auto openPopover = [&] {
        synth::ui::test_hooks::randomizePanelHookForTest() = [&](auto p) { popover = std::move(p); };
        card.grid->getTools().getButton(synth::ui::StepGridTools::Tool::Randomize).onClick();
        synth::ui::test_hooks::randomizePanelHookForTest() = nullptr;
    };
    openPopover();
    ASSERT_NE(popover, nullptr);

    // Owner 2: the piano roll's Scale assist panel.
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.toggleScalePanel();
    auto& panel = f.roll.getScaleAssistPanel();

    const Owner owners[] = {
        {"sequencer Randomize panel", [&]() -> NoteRangePicker& { return popover->getRangePicker(); },
         [&] {
             openPopover(); // reopening reads the pattern's saved range
             return std::make_pair(popover->getRangePicker().getLow(), popover->getRangePicker().getHigh());
         },
         [&] { return card.canvas.undo.canUndo(); }, [&] { card.canvas.undo.getUndoManager().undo(); },
         [&] { card.canvas.undo.getUndoManager().redo(); },
         [&] { card.canvas.undo.getUndoManager().clearUndoHistory(); }},
        {"piano roll Scale assist panel", [&]() -> NoteRangePicker& { return panel.getRangePicker(); },
         [&] { return std::make_pair(panel.getMinPitchSelection(), panel.getMaxPitchSelection()); },
         [&] { return f.undo.canUndo(); },
         [&] {
             f.undo.undo();
             f.roll.refreshFromDoc();
         },
         [&] {
             f.undo.redo();
             f.roll.refreshFromDoc();
         },
         [&] { f.undo.getUndoManager().clearUndoHistory(); }},
    };

    for (const auto& owner : owners) {
        SCOPED_TRACE(owner.name);
        owner.clearHistory();
        const auto start = std::make_pair(owner.picker().getLow(), owner.picker().getHigh());
        const int target = start.second - 3;
        ASSERT_NO_FATAL_FAILURE(dragHighEdgeTo(owner.picker(), target));
        const auto moved = std::make_pair(start.first, target);

        ASSERT_TRUE(owner.canUndo()) << "the drag recorded an undo step";
        owner.undo();
        EXPECT_EQ(owner.shown(), start) << "undo restores the range";
        EXPECT_FALSE(owner.canUndo()) << "a drag is exactly one undo step";
        owner.redo();
        EXPECT_EQ(owner.shown(), moved) << "redo reapplies it";
    }
}

TEST(PianoRollScaleAssistTest, TheNoteRangeSurvivesSaveAndReopenAndAnOldProjectKeepsTheDefaults) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.toggleScalePanel();
    ASSERT_NO_FATAL_FAILURE(dragHighEdgeTo(f.roll.getScaleAssistPanel().getRangePicker(), 69));
    ASSERT_TRUE(f.doc.getClip(clipId)->noteRange.has_value());

    const auto reload = [&](const juce::var& saved, TimelineDoc& doc, std::unique_ptr<TimelineViewState>& state,
                            std::unique_ptr<PianoRollComponent>& roll) {
        ASSERT_TRUE(doc.fromVar(saved));
        state = std::make_unique<TimelineViewState>();
        roll = std::make_unique<PianoRollComponent>(*state);
        roll->setTimelineDoc(&doc);
        roll->setSize(900, 160);
        roll->openClip(clipId);
    };

    TimelineDoc reloaded;
    std::unique_ptr<TimelineViewState> state;
    std::unique_ptr<PianoRollComponent> roll;
    ASSERT_NO_FATAL_FAILURE(reload(juce::JSON::parse(juce::JSON::toString(f.doc.toVar())), reloaded, state, roll));
    EXPECT_EQ(reloaded.getClip(clipId)->noteRange, std::make_pair(36, 69));
    EXPECT_EQ(roll->getScaleAssistPanel().getMinPitchSelection(), 36);
    EXPECT_EQ(roll->getScaleAssistPanel().getMaxPitchSelection(), 69);

    // A project saved before the range existed has no "noteRange" key and shows today's C2 to C5.
    auto old = juce::JSON::parse(juce::JSON::toString(f.doc.toVar()));
    for (auto& track : *old.getProperty("tracks", {}).getArray())
        if (auto* clips = track.getProperty("clips", {}).getArray())
            for (auto& clip : *clips)
                clip.getDynamicObject()->removeProperty("noteRange");
    TimelineDoc oldDoc;
    std::unique_ptr<TimelineViewState> oldState;
    std::unique_ptr<PianoRollComponent> oldRoll;
    ASSERT_NO_FATAL_FAILURE(reload(old, oldDoc, oldState, oldRoll));
    EXPECT_FALSE(oldDoc.getClip(clipId)->noteRange.has_value());
    EXPECT_EQ(oldRoll->getScaleAssistPanel().getMinPitchSelection(), 36);
    EXPECT_EQ(oldRoll->getScaleAssistPanel().getMaxPitchSelection(), 72);
}

TEST(PianoRollScaleAssistTest, AKeyChangeMovesTheSavedRangeInTheSameUndoStepAndFollowingTheSongKeyWritesNothing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.toggleScalePanel();
    auto& panel = f.roll.getScaleAssistPanel();

    // No saved range: a key change shifts the band on screen but writes no range.
    ASSERT_NO_FATAL_FAILURE(pickPanelKey(panel, "d dor"));
    EXPECT_FALSE(f.doc.getClip(clipId)->noteRange.has_value());
    EXPECT_EQ(panel.getMinPitchSelection(), 38) << "C2 moved to D2";
    f.undo.undo();
    f.roll.refreshFromDoc();
    EXPECT_FALSE(f.doc.getClip(clipId)->scale.has_value());
    EXPECT_EQ(panel.getMinPitchSelection(), 36) << "undo shows the default range again";

    // A saved range: the key change carries it, and one undo gives back both.
    f.undo.getUndoManager().clearUndoHistory();
    ASSERT_NO_FATAL_FAILURE(dragHighEdgeTo(panel.getRangePicker(), 70));
    const auto saved = std::make_pair(36, 70);
    ASSERT_EQ(f.doc.getClip(clipId)->noteRange, saved);
    ASSERT_NO_FATAL_FAILURE(pickPanelKey(panel, "d dor"));
    EXPECT_EQ(f.doc.getClip(clipId)->noteRange, std::make_pair(38, 72)) << "moved with the root, in the doc";
    ASSERT_TRUE(f.doc.getClip(clipId)->scale.has_value());

    f.undo.undo();
    f.roll.refreshFromDoc();
    EXPECT_FALSE(f.doc.getClip(clipId)->scale.has_value()) << "one Cmd+Z undoes the key";
    EXPECT_EQ(f.doc.getClip(clipId)->noteRange, saved) << "and the range with it";
    EXPECT_EQ(panel.getMinPitchSelection(), 36);
    EXPECT_EQ(panel.getMaxPitchSelection(), 70);
    f.undo.redo();
    f.roll.refreshFromDoc();
    EXPECT_EQ(f.doc.getClip(clipId)->noteRange, std::make_pair(38, 72));

    // Following the song key again changes the clip's scale as it does today and leaves the saved range alone.
    panel.onFollowSongKey();
    EXPECT_FALSE(f.doc.getClip(clipId)->scale.has_value());
    EXPECT_EQ(f.doc.getClip(clipId)->noteRange, std::make_pair(38, 72));
    EXPECT_EQ(panel.getMinPitchSelection(), 38) << "the saved range stays on screen";
}
