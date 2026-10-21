// NoteRangePickerTests.cpp -- the shared note range picker: edge drags add or remove notes and snap to the scale, the
// middle moves the range by octaves (by one note with Option), the range stays ordered, and the keys reach every part.
#include "UI/Layout/NoteRangePicker/NoteRangeGeometry.h"
#include "UI/Layout/NoteRangePicker/NoteRangePicker.h"
#include <gtest/gtest.h>

using synth::ui::NoteRangePicker;
using Part = NoteRangePicker::Part;

namespace {

juce::MouseEvent mouseAt(NoteRangePicker& comp, float x,
                         juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
    const juce::Point<float> p(x, 17.0f);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
}

struct Fixture {
    NoteRangePicker picker;
    int changes = 0;
    int lastLo = -1;
    int lastHi = -1;

    Fixture() {
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

TEST(NoteRangePicker, TheOctavesShownFollowTheRange) {
    Fixture f;
    f.picker.setRange(100, 112);
    EXPECT_LE(f.picker.getWindowBase(), 100);
    EXPECT_GE(f.picker.getWindowBase() + 47, 112);
    f.picker.setRange(0, 12);
    EXPECT_EQ(f.picker.getWindowBase(), 0);
}

TEST(NoteRangePicker, DraggingAnEdgeAddsAndRemovesNotes) {
    Fixture f;
    const int base = f.picker.getWindowBase();
    const float start = f.highEdgeX();
    const float target = f.picker.keyCentreX(67) + f.picker.whiteKeyWidth() * 0.5f; // the edge of G
    ASSERT_GE(67, base);
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

TEST(NoteRangePicker, DraggingTheMiddleMovesByOctaves) {
    Fixture f;
    const float octave = f.picker.whiteKeyWidth() * 7.0f;
    const float mid = (f.lowEdgeX() + f.highEdgeX()) * 0.5f;
    f.drag(mid, mid + octave);
    EXPECT_EQ(f.picker.getLow(), 60);
    EXPECT_EQ(f.picker.getHigh(), 72);
    EXPECT_EQ(f.lastLo, 60);
}

TEST(NoteRangePicker, OptionDraggingTheMiddleMovesByOneNote) {
    Fixture f;
    const float mid = (f.lowEdgeX() + f.highEdgeX()) * 0.5f;
    const float note = f.picker.whiteKeyWidth() * 7.0f / 12.0f;
    f.drag(mid, mid + note,
           juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier));
    EXPECT_EQ(f.picker.getLow(), 49);
    EXPECT_EQ(f.picker.getHigh(), 61);
}

TEST(NoteRangePicker, ThreeTabStopsInOrderEachNamedWithATooltip) {
    Fixture f;
    EXPECT_EQ(f.picker.getStop(Part::LowEdge).getTitle(), "Lowest note");
    EXPECT_EQ(f.picker.getStop(Part::HighEdge).getTitle(), "Highest note");
    EXPECT_EQ(f.picker.getStop(Part::Band).getTitle(), "Move the whole range");
    for (const auto part : {Part::LowEdge, Part::HighEdge, Part::Band}) {
        auto& stop = f.picker.getStop(part);
        EXPECT_TRUE(stop.getWantsKeyboardFocus());
        EXPECT_EQ(dynamic_cast<juce::SettableTooltipClient&>(stop).getTooltip(), stop.getTitle());
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
    EXPECT_EQ(f.picker.getLow(), 37);
    EXPECT_EQ(f.picker.getHigh(), 59);
    EXPECT_GE(f.changes, 4);
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
    const auto c = keySpan(48, 48, 280.0f);
    const auto cSharp = keySpan(49, 48, 280.0f);
    const auto d = keySpan(50, 48, 280.0f);
    EXPECT_FLOAT_EQ(c.x0, 0.0f);
    EXPECT_FLOAT_EQ(cSharp.centre(), c.x1); // a black key sits on the line between its whites
    EXPECT_FLOAT_EQ(d.x0, c.x1);
    EXPECT_EQ(pitchAtX(d.centre(), 48, 280.0f), 50);
    EXPECT_FLOAT_EQ(keySpan(48 + 48, 48, 280.0f).x0, 280.0f); // the next C starts where the strip ends
}
