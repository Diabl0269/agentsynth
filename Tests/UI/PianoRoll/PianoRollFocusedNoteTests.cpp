// PianoRollFocusedNoteTests.cpp -- the piano roll shows and speaks the focused note.
//
// The focused note is the one note the selection holds, which Alt+Left/Right (the rebindable
// pianoRollNavNextNote/PrevNote) move between. Driven through the real keyPressed path. Real OS focus
// needs a native peer, so the ring is rendered through setFocusRingForcedForTest and the spoken
// value is read from the accessibility handler's value interface (the handler is created directly;
// a native one does not exist headless).
#include "PianoRollTestHelpers.h"

namespace {

// The roll's own spoken value, read the way a screen reader would.
juce::String spokenValue(PianoRollComponent& roll) {
    auto handler = roll.createAccessibilityHandler();
    auto* value = handler->getValueInterface();
    return value != nullptr ? value->getCurrentValueAsString() : juce::String();
}

struct FocusedNoteFixture : PianoRollFixture {
    ClipId clip;
    NoteId first, second;

    FocusedNoteFixture() {
        const auto track = doc.addTrack(TrackKind::Midi, "Track 1");
        clip = doc.addClip(track, 0.0, 8.0, "Lead");
        first = doc.addNote(clip, makeNote(1.0, 60, 1.0));
        auto note = makeNote(4.0, 64, 0.5);
        note.velocity = 90;
        second = doc.addNote(clip, note);
        open(clip);
    }

    // Renders the roll into a software image (a native one reads all zeros on the Windows CI runner).
    juce::Image render() {
        juce::Image image(juce::Image::ARGB, roll.getWidth(), roll.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        roll.paintEntireComponent(g, false);
        return image;
    }

    // The ring sits just outside the note's left edge, vertically centred.
    juce::Colour ringPixel(NoteId id, const juce::Image& image) {
        const auto rect = roll.getNoteRect(id);
        return image.getPixelAt(rect.getX() - 2, rect.getCentreY());
    }
};

} // namespace

TEST(PianoRollFocusedNoteTest, AltRightMovesTheFocusedNoteAndWhatIsSpoken) {
    FocusedNoteFixture f;
    f.roll.getSelectionForTest().setSelection({f.first});
    EXPECT_EQ(f.roll.getFocusedNote(), f.first);
    EXPECT_EQ(spokenValue(f.roll), "C4, bar 1 beat 2, length 1/4, velocity 100");

    ASSERT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_EQ(f.roll.getFocusedNote(), f.second);
    EXPECT_EQ(spokenValue(f.roll), "E4, bar 2 beat 1, length 1/8, velocity 90");

    ASSERT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_EQ(f.roll.getFocusedNote(), f.first);
    EXPECT_EQ(spokenValue(f.roll), "C4, bar 1 beat 2, length 1/4, velocity 100");
}

TEST(PianoRollFocusedNoteTest, TheSpokenValueFollowsAnEditOfTheFocusedNote) {
    FocusedNoteFixture f;
    f.roll.getSelectionForTest().setSelection({f.first});
    ASSERT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0)));
    EXPECT_EQ(spokenValue(f.roll), "C#4, bar 1 beat 2, length 1/4, velocity 100");
    ASSERT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0)));
    EXPECT_EQ(spokenValue(f.roll), "C#4, bar 1 beat 3, length 1/4, velocity 100") << "nudged one quarter";
}

TEST(PianoRollFocusedNoteTest, WithNoNoteFocusedTheRollReadsClipNameAndNoteCount) {
    FocusedNoteFixture f;
    EXPECT_FALSE(f.roll.getFocusedNote().isValid());
    EXPECT_EQ(spokenValue(f.roll), "Lead, 2 notes");

    f.roll.getSelectionForTest().setSelection({f.first, f.second});
    EXPECT_FALSE(f.roll.getFocusedNote().isValid()) << "a multi-selection has no single focused note";
    EXPECT_EQ(spokenValue(f.roll), "Lead, 2 notes, 2 selected");
}

TEST(PianoRollFocusedNoteTest, TheRollIsNamedAndClosedRollSpeaksNothing) {
    FocusedNoteFixture f;
    auto handler = f.roll.createAccessibilityHandler();
    EXPECT_EQ(handler->getTitle(), "Piano roll");
    f.roll.closeRoll();
    EXPECT_TRUE(spokenValue(f.roll).isEmpty());
}

TEST(PianoRollFocusedNoteTest, TheFocusRingIsPaintedOnTheFocusedNoteOnly) {
    FocusedNoteFixture f;
    f.roll.getSelectionForTest().setSelection({f.first});
    const auto accent = juce::Colours::orange; // no AppLookAndFeel here: the helper's fallback accent

    EXPECT_NE(f.ringPixel(f.first, f.render()), accent) << "no ring while the roll does not hold keyboard focus";

    f.roll.setFocusRingForcedForTest(true);
    auto image = f.render();
    EXPECT_EQ(f.ringPixel(f.first, image), accent);
    EXPECT_NE(f.ringPixel(f.second, image), accent);

    ASSERT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    image = f.render();
    EXPECT_EQ(f.ringPixel(f.second, image), accent) << "the ring follows the focused note";
    EXPECT_NE(f.ringPixel(f.first, image), accent);

    f.roll.getSelectionForTest().clear();
    EXPECT_NE(f.ringPixel(f.second, f.render()), accent) << "no focused note, no ring";
}
