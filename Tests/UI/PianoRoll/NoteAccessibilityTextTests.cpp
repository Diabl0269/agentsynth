// NoteAccessibilityTextTests.cpp -- the wording a screen reader speaks for the piano roll's
// focused note and for the roll itself (Source/UI/PianoRoll/NoteAccessibilityText.h).
#include "UI/PianoRoll/NoteAccessibilityText.h"
#include <gtest/gtest.h>

using synth::ui::describeNoteForAccessibility;
using synth::ui::describeNoteLength;
using synth::ui::describeNotePitch;
using synth::ui::describeNotePosition;
using synth::ui::describeRollForAccessibility;

TEST(NoteAccessibilityTextTest, PitchNamesUseTheKeysColumnOctaveNumbering) {
    EXPECT_EQ(describeNotePitch(60), "C4");
    EXPECT_EQ(describeNotePitch(61), "C#4");
    EXPECT_EQ(describeNotePitch(69), "A4");
    EXPECT_EQ(describeNotePitch(0), "C-1");
    EXPECT_EQ(describeNotePitch(127), "G9");
}

TEST(NoteAccessibilityTextTest, PositionIsOneBasedBarAndBeat) {
    EXPECT_EQ(describeNotePosition(0.0, 4.0), "bar 1 beat 1");
    EXPECT_EQ(describeNotePosition(4.0, 4.0), "bar 2 beat 1");
    EXPECT_EQ(describeNotePosition(6.0, 4.0), "bar 2 beat 3");
    EXPECT_EQ(describeNotePosition(4.5, 4.0), "bar 2 beat 1.5");
    EXPECT_EQ(describeNotePosition(6.0, 3.0), "bar 3 beat 1") << "a 3-beat bar";
    EXPECT_EQ(describeNotePosition(-2.0, 4.0), "bar 1 beat 1") << "never before the first bar";
}

TEST(NoteAccessibilityTextTest, LengthIsAFractionOfAWholeNoteWhenExact) {
    EXPECT_EQ(describeNoteLength(0.5), "1/8");
    EXPECT_EQ(describeNoteLength(1.0), "1/4");
    EXPECT_EQ(describeNoteLength(0.25), "1/16");
    EXPECT_EQ(describeNoteLength(1.5), "3/8");
    EXPECT_EQ(describeNoteLength(2.0), "1/2");
    EXPECT_EQ(describeNoteLength(4.0), "1/1");
    EXPECT_EQ(describeNoteLength(8.0), "2/1");
}

TEST(NoteAccessibilityTextTest, LengthFallsBackToBeatsWhenNoFractionIsExact) {
    EXPECT_EQ(describeNoteLength(1.0 / 3.0), "0.333 beats");
    EXPECT_EQ(describeNoteLength(1.3), "1.3 beats");
}

TEST(NoteAccessibilityTextTest, NoteDescriptionReadsPitchPositionLengthVelocity) {
    synth::MidiNote note;
    note.pitch = 60;
    note.lengthBeats = 0.5;
    note.velocity = 100;
    EXPECT_EQ(describeNoteForAccessibility(note, 4.0, 4.0), "C4, bar 2 beat 1, length 1/8, velocity 100");
}

TEST(NoteAccessibilityTextTest, RollWithoutAFocusedNoteReadsClipNameAndCount) {
    EXPECT_EQ(describeRollForAccessibility("Lead", 0, 0), "Lead, 0 notes");
    EXPECT_EQ(describeRollForAccessibility("Lead", 1, 0), "Lead, 1 note");
    EXPECT_EQ(describeRollForAccessibility("Lead", 12, 0), "Lead, 12 notes");
    EXPECT_EQ(describeRollForAccessibility("Lead", 12, 1), "Lead, 12 notes");
    EXPECT_EQ(describeRollForAccessibility("Lead", 12, 3), "Lead, 12 notes, 3 selected");
}
