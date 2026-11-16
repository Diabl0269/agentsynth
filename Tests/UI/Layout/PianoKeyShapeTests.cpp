// PianoKeyShapeTests.cpp -- every horizontal piano keyboard draws its black keys at the one shared height
// (pianokeys::kBlackKeyHeightShare of a white key). Bug kind: sibling-gap (one keyboard drew its own number).
// The sampler's key strip and map window are checked in SamplerMapAuditionTests.cpp (they need a loaded instrument).
#include "PianoKeyShapeProbe.h"
#include "UI/Layout/KeyPicker/KeyPickerKeyboard.h"
#include "UI/Layout/NoteRangePicker/NoteRangePicker.h"
#include "UI/Layout/PianoKeyShape.h"
#include "UI/ModuleViews/ShapeEditor/NoteKeyStrip.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

using namespace synth::ui;

TEST(PianoKeyShape, TheSharedShareIsTheDesignedSixtyTwoPercent) {
    EXPECT_FLOAT_EQ(pianokeys::kBlackKeyHeightShare, 0.62f);
    for (const int pc : {1, 3, 6, 8, 10})
        EXPECT_TRUE(pianokeys::isBlackPitchClass(pc));
    for (const int pc : {0, 2, 4, 5, 7, 9, 11})
        EXPECT_FALSE(pianokeys::isBlackPitchClass(pc));
}

TEST(PianoKeyShape, TheKeyPickerKeyboardDrawsBlackKeysAtTheSharedHeight) {
    KeyPickerKeyboard keyboard;
    keyboard.setSize(280, 60);
    const float whiteWidth = 280.0f / 14.0f;
    int lastBlackRow = -1;
    for (int y = 0; y < 60; ++y)
        if (keyboard.pitchClassAt({(int)whiteWidth, y}) == 1)
            lastBlackRow = y;
    ASSERT_GE(lastBlackRow, 0);
    EXPECT_NEAR((float)(lastBlackRow + 1) / 60.0f, pianokeys::kBlackKeyHeightShare, 0.03f);
}

TEST(PianoKeyShape, TheShapeEditorKeyStripDrawsBlackKeysAtTheSharedHeight) {
    NoteKeyStrip strip;
    strip.setSize(300, 60);
    const int black = strip.getBaseNote() + 1; // C#
    const int white = strip.getBaseNote();
    EXPECT_NEAR((float)strip.getKeyBounds(black).getHeight() / (float)strip.getKeyBounds(white).getHeight(),
                pianokeys::kBlackKeyHeightShare, 0.03f);
}

TEST(PianoKeyShape, TheNoteRangePickerDrawsBlackKeysAtTheSharedHeight) {
    NoteRangePicker picker;
    // Just under the preferred height the overview bar (the same colour as a black key) is not drawn.
    const int height = NoteRangePicker::kPreferredHeight - 1;
    picker.setSize(280, height);
    picker.setRange(60, 60); // one key: the band that tints the keys inside it stays clear of D#4 and E4
    juce::Image image(juce::Image::ARGB, 280, height, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    picker.paintEntireComponent(g, false);
    // The band tints the keys inside it, so each key's own colour is read from its pixels, not from the theme.
    const int blackX = (int)picker.keyCentreX(63), whiteX = (int)picker.keyCentreX(64);
    const auto black = pianokeyprobe::extentOf(image, blackX, image.getPixelAt(blackX, 2));
    const auto white = pianokeyprobe::extentOf(image, whiteX, image.getPixelAt(whiteX, 4));
    ASSERT_GT(black.height(), 0);
    ASSERT_GT(white.height(), 0);
    EXPECT_NEAR((float)black.height() / (float)white.height(), pianokeys::kBlackKeyHeightShare, 0.05f);
}
