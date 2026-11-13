// Concern: the piano roll's clip key: a clip with no key of its own follows the song key (highlighting, Generate, the
// range picker, new-note snapping), an own key stays put, and the panel's Key field and Follow row show and change it.
#include "PianoRollKeyPickerHelpers.h"
#include "PianoRollTestHelpers.h"

#include "Music/SongKey.h"
#include "Timeline/ClipKey.h"
#include "Transport/TransportService.h"
#include <set>

namespace {

synth::MusicalScale keyOf(int root, const char* scale) {
    return synth::makeScale(root, synth::scalePresetIndex(scale));
}

juce::String followText(const juce::String& key) { return juce::String::fromUTF8("Song key \xc2\xb7 ") + key; }

// A roll wired to a real transport, with one empty MIDI clip. The transport outlives the roll.
struct SongKeyFixture {
    synth::TransportService transport;
    PianoRollFixture f;
    ClipId clip;

    SongKeyFixture() {
        const auto track = f.doc.addTrack(TrackKind::Midi, "Track 1");
        clip = f.doc.addClip(track, 0.0, 8.0, "Clip");
    }

    // Opens the clip with the transport wired first, so the clip's first view already reads the song key.
    void wireAndOpen(const std::optional<synth::MusicalScale>& songKey) {
        transport.setSongKey(songKey);
        f.roll.setTransport(&transport);
        f.open(clip);
    }

    ScaleAssistPanel& panel() { return f.roll.getScaleAssistPanel(); }
    const synth::Clip& clipNow() const { return *f.doc.getClip(clip); }

    // The pitch of the note starting at `startBeat`, or -1.
    int pitchStartingAt(double startBeat) const {
        for (const auto& note : clipNow().notes)
            if (std::abs(note.startBeat - startBeat) < 1.0e-6)
                return note.pitch;
        return -1;
    }

    std::vector<int> pitches() const {
        std::vector<int> out;
        for (const auto& note : clipNow().notes)
            out.push_back(note.pitch);
        return out;
    }
};

bool allIn(const std::vector<int>& pitches, const synth::MusicalScale& key) {
    return std::all_of(pitches.begin(), pitches.end(), [&key](int p) { return key.contains(p); });
}

// Puts the command key down for a test's scope, restoring the real modifier state after.
struct CommandKeyDown {
    CommandKeyDown() { juce::ModifierKeys::currentModifiers = juce::ModifierKeys(juce::ModifierKeys::commandModifier); }
    ~CommandKeyDown() { juce::ModifierKeys::currentModifiers = juce::ModifierKeys(); }
};

} // namespace

TEST(ScaleAssistPanelSongKeyTest, AFreshPanelFollowsTheSongKeyAndSaysThereIsNone) {
    ScaleAssistPanel panel;
    EXPECT_FALSE(panel.getSelectedScale().has_value());
    EXPECT_TRUE(panel.isFollowingSongKey());
    EXPECT_EQ(panel.getKeyField().getText(), followText("No key"));
}

TEST(ScaleAssistPanelSongKeyTest, KeyPickerOffersEveryKeyGroupAndYourScalesOnlyOnceOneIsSaved) {
    ScaleAssistPanel panel;
    auto titles = pickerGroupTitles(panel);
    for (const auto& group : synth::keyScaleGroups())
        EXPECT_TRUE(hasTitle(titles, group.title)) << group.title;
    EXPECT_FALSE(hasTitle(titles, "Your scales")) << "no saved scales, no group";

    for (int pc : {0, 3, 7}) // a scale needs notes to be a key
        panel.getCustomPitchToggle(pc).setToggleState(true, juce::dontSendNotification);
    panel.getCustomScaleNameEditor().setText("Mine", false);
    panel.getSaveCustomScaleButton().onClick();
    EXPECT_TRUE(hasTitle(pickerGroupTitles(panel), "Your scales"));
}

TEST(ScaleAssistPanelSongKeyTest, SavingACustomScaleWithNoNotesDoesNothing) {
    ScaleAssistPanel panel;
    int fired = 0;
    panel.onScaleChanged = [&fired](std::optional<synth::MusicalScale>) { ++fired; };
    panel.getCustomScaleNameEditor().setText("Empty", false);
    panel.getSaveCustomScaleButton().onClick();
    EXPECT_EQ(fired, 0);
    EXPECT_TRUE(panel.getUserScalesForTest().empty());
    EXPECT_TRUE(panel.isFollowingSongKey());
}

TEST(ScaleAssistPanelSongKeyTest, PickingAKeyFiresOnScaleChangedWithTheRightScale) {
    ScaleAssistPanel panel;
    std::vector<std::optional<synth::MusicalScale>> fired;
    panel.onScaleChanged = [&](std::optional<synth::MusicalScale> s) { fired.push_back(s); };

    ASSERT_NO_FATAL_FAILURE(pickPanelKey(panel, "c major"));
    ASSERT_EQ(fired.size(), 1u);
    ASSERT_TRUE(fired.back().has_value());
    EXPECT_EQ(juce::String(fired.back()->name), "Major");
    EXPECT_EQ(fired.back()->rootPitchClass, 0);
    EXPECT_FALSE(panel.isFollowingSongKey()) << "a picked key is the clip's own";

    ASSERT_NO_FATAL_FAILURE(pickPanelKey(panel, "d major"));
    ASSERT_EQ(fired.size(), 2u);
    ASSERT_TRUE(fired.back().has_value());
    EXPECT_EQ(fired.back()->rootPitchClass, 2);
    EXPECT_EQ(juce::String(fired.back()->name), "Major");

    ASSERT_NO_FATAL_FAILURE(pickPanelKey(panel, "d dor"));
    ASSERT_EQ(fired.size(), 3u);
    ASSERT_TRUE(fired.back().has_value());
    EXPECT_EQ(juce::String(fired.back()->name), "Dorian");
}

TEST(ScaleAssistPanelSongKeyTest, TheFollowRowFiresOnFollowSongKeyAndNotOnScaleChanged) {
    ScaleAssistPanel panel;
    int followed = 0;
    int picked = 0;
    panel.onFollowSongKey = [&] { ++followed; };
    panel.onScaleChanged = [&](std::optional<synth::MusicalScale>) { ++picked; };
    ASSERT_NO_FATAL_FAILURE(chooseFollowSongKey(panel));
    EXPECT_EQ(followed, 1);
    EXPECT_EQ(picked, 0);
}

TEST(PianoRollSongKeyTest, AFollowingClipShowsTheSongKeyAndHighlightsItsRows) {
    SongKeyFixture s;
    s.wireAndOpen(keyOf(2, "Dorian"));

    ASSERT_TRUE(s.panel().getSelectedScale().has_value());
    EXPECT_EQ(s.panel().getSelectedScale()->rootPitchClass, 2);
    EXPECT_EQ(juce::String(s.panel().getSelectedScale()->name), "Dorian");
    EXPECT_TRUE(s.panel().isFollowingSongKey());
    EXPECT_EQ(s.panel().getKeyField().getText(), followText("D Dorian"));
    EXPECT_FALSE(s.clipNow().scale.has_value()) << "following writes no key into the clip";

    s.f.roll.toggleScaleFilter();
    ASSERT_TRUE(s.f.roll.isRowFilterActive());
    const auto& rows = s.f.roll.getVisiblePitchesForTest();
    EXPECT_TRUE(std::find(rows.begin(), rows.end(), 62) != rows.end()) << "D is in D Dorian";
    EXPECT_TRUE(std::find(rows.begin(), rows.end(), 61) == rows.end()) << "C# is out of D Dorian";
    EXPECT_TRUE(allIn(rows, keyOf(2, "Dorian")));
}

TEST(PianoRollSongKeyTest, ChangingTheSongKeyMovesAFollowingClipAndLeavesItsNotes) {
    SongKeyFixture s;
    s.f.doc.addNote(s.clip, makeNote(0.0, 60));
    s.f.doc.addNote(s.clip, makeNote(1.0, 61));
    s.f.doc.addNote(s.clip, makeNote(2.0, 65));
    s.wireAndOpen(keyOf(2, "Dorian"));
    const auto before = s.pitches();

    s.transport.setSongKey(keyOf(9, "Natural Minor"));
    s.f.roll.refreshSongKey();

    ASSERT_TRUE(s.panel().getSelectedScale().has_value());
    EXPECT_EQ(s.panel().getSelectedScale()->rootPitchClass, 9);
    EXPECT_EQ(juce::String(s.panel().getSelectedScale()->name), "Natural Minor");
    EXPECT_TRUE(s.panel().isFollowingSongKey());
    EXPECT_EQ(s.pitches(), before) << "a key change only re-highlights; it never moves a note";
}

TEST(PianoRollSongKeyTest, AnOwnKeyStaysWhenTheSongKeyChanges) {
    SongKeyFixture s;
    s.wireAndOpen(keyOf(2, "Dorian"));
    ASSERT_NO_FATAL_FAILURE(pickPanelKey(s.panel(), "a phr"));

    s.transport.setSongKey(keyOf(4, "Natural Minor"));
    s.f.roll.refreshSongKey();

    ASSERT_TRUE(s.panel().getSelectedScale().has_value());
    EXPECT_EQ(s.panel().getSelectedScale()->rootPitchClass, 9);
    EXPECT_EQ(juce::String(s.panel().getSelectedScale()->name), "Phrygian");
    EXPECT_FALSE(s.panel().isFollowingSongKey());
    EXPECT_EQ(s.panel().getKeyField().getText(), juce::String::fromUTF8("A Phrygian \xc2\xb7 own"));
    ASSERT_TRUE(s.clipNow().scale.has_value());
    EXPECT_EQ(s.clipNow().scale->rootPitchClass, 9);
}

TEST(PianoRollSongKeyTest, FollowSongKeyDropsTheOwnKeyInOneUndoStep) {
    SongKeyFixture s;
    s.wireAndOpen(keyOf(2, "Dorian"));
    ASSERT_NO_FATAL_FAILURE(pickPanelKey(s.panel(), "a phr"));
    ASSERT_TRUE(s.clipNow().scale.has_value());
    s.f.undo.clearUndoHistory();

    ASSERT_NO_FATAL_FAILURE(chooseFollowSongKey(s.panel()));
    EXPECT_FALSE(s.clipNow().scale.has_value());
    EXPECT_TRUE(s.panel().isFollowingSongKey());
    EXPECT_EQ(s.panel().getKeyField().getText(), followText("D Dorian"));

    ASSERT_TRUE(s.f.undo.canUndo());
    s.f.undo.undo();
    s.f.roll.refreshFromDoc(); // what the timeline panel's doc listener does after an undo
    EXPECT_FALSE(s.f.undo.canUndo()) << "following again was a single step";
    ASSERT_TRUE(s.clipNow().scale.has_value());
    EXPECT_EQ(juce::String(s.clipNow().scale->name), "Phrygian");
    EXPECT_FALSE(s.panel().isFollowingSongKey());
    EXPECT_EQ(s.panel().getKeyField().getText(), juce::String::fromUTF8("A Phrygian \xc2\xb7 own"));
}

// Regression test for FRO1136: a transport wired after the clip was opened left the panel showing no key.
TEST(PianoRollSongKeyTest, ATransportWiredAfterTheClipOpenedShowsTheSongKeyAtOnce) {
    SongKeyFixture s;
    s.transport.setSongKey(keyOf(2, "Dorian"));
    s.f.open(s.clip);
    EXPECT_FALSE(s.panel().getSelectedScale().has_value()) << "no transport yet, so nothing to follow";

    s.f.roll.setTransport(&s.transport);

    ASSERT_TRUE(s.panel().getSelectedScale().has_value());
    EXPECT_EQ(juce::String(s.panel().getSelectedScale()->name), "Dorian");
    EXPECT_TRUE(s.panel().isFollowingSongKey());
    EXPECT_EQ(s.panel().getKeyField().getText(), followText("D Dorian"));
}

// Regression test for FRO1136: switching the scale filter on a following clip turned it into one with its own key.
// The follow-freeze guard: every piano roll write path that keeps the key (the filter chip, the panel's toggle, undo
// and redo) leaves a following clip following.
TEST(PianoRollSongKeyTest, EveryWritePathKeepsAFollowerFollowing) {
    SongKeyFixture s;
    s.wireAndOpen(keyOf(2, "Dorian"));

    s.f.roll.toggleScaleFilter();
    EXPECT_FALSE(s.clipNow().scale.has_value());
    EXPECT_TRUE(s.clipNow().scaleVisible);

    s.panel().getPitchVisibilityToggle().setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(s.clipNow().scale.has_value());
    EXPECT_FALSE(s.clipNow().scaleVisible);

    s.panel().getPitchVisibilityToggle().setToggleState(true, juce::sendNotificationSync);
    EXPECT_FALSE(s.clipNow().scale.has_value());
    EXPECT_TRUE(s.panel().isFollowingSongKey());

    s.f.undo.undo();
    s.f.roll.refreshFromDoc();
    EXPECT_FALSE(s.clipNow().scale.has_value()) << "undo";
    s.f.undo.redo();
    s.f.roll.refreshFromDoc();
    EXPECT_FALSE(s.clipNow().scale.has_value()) << "redo";
    EXPECT_TRUE(s.panel().isFollowingSongKey());

    s.transport.setSongKey(keyOf(4, "Natural Minor"));
    s.f.roll.refreshSongKey();
    EXPECT_FALSE(s.clipNow().scale.has_value());
    ASSERT_TRUE(s.f.roll.isRowFilterActive());
    const auto& rows = s.f.roll.getVisiblePitchesForTest();
    EXPECT_TRUE(std::find(rows.begin(), rows.end(), 64) != rows.end()) << "E is in E minor";
    EXPECT_TRUE(std::find(rows.begin(), rows.end(), 63) == rows.end()) << "D# is not";
    EXPECT_TRUE(allIn(rows, keyOf(4, "Natural Minor")));
}

TEST(PianoRollSongKeyTest, GenerateUsesTheFollowedKey) {
    SongKeyFixture s;
    s.wireAndOpen(keyOf(2, "Major"));

    s.panel().getGenerateButton().onClick();

    const auto generated = s.pitches();
    ASSERT_FALSE(generated.empty());
    EXPECT_TRUE(allIn(generated, keyOf(2, "Major"))) << "every generated pitch is in D major";
}

TEST(PianoRollSongKeyTest, ANewNoteSnapsToTheFollowedKeyUnlessCommandIsHeld) {
    SongKeyFixture s;
    setSnap(s.f, TimelineViewState::Snap::Quarter);
    const auto existing = s.f.doc.addNote(s.clip, makeNote(6.0, 61));
    s.wireAndOpen(keyOf(0, "Major"));

    // A black key near the top of the grid; every black key is a tie in C major and ties go down.
    int pitch = s.f.roll.getFirstVisiblePitchForTest() - 8;
    while (std::set<int>{1, 3, 6, 8, 10}.count(pitch % 12) == 0)
        ++pitch;

    s.f.roll.mouseDoubleClick(leftClick(s.f.roll, pointAt(s.f.roll, 2.1, pitch)));
    ASSERT_EQ(s.clipNow().notes.size(), 2u);
    EXPECT_EQ(s.pitchStartingAt(2.0), pitch - 1) << "the new note landed on the key";
    EXPECT_EQ(s.f.doc.getNote(existing)->pitch, 61) << "a note already there is never moved";

    {
        CommandKeyDown command;
        s.f.roll.mouseDoubleClick(leftClick(s.f.roll, pointAt(s.f.roll, 4.1, pitch)));
    }
    ASSERT_EQ(s.clipNow().notes.size(), 3u);
    EXPECT_EQ(s.pitchStartingAt(4.0), pitch) << "holding Command places it off the key";
    EXPECT_EQ(s.f.doc.getNote(existing)->pitch, 61);
}

TEST(PianoRollSongKeyTest, TheRangePickerFollowsTheKeysRoot) {
    ScaleAssistPanel fresh;
    EXPECT_EQ(fresh.getMinPitchSelection(), 36);
    EXPECT_EQ(fresh.getMaxPitchSelection(), 72);

    SongKeyFixture s;
    s.wireAndOpen(keyOf(2, "Dorian"));
    EXPECT_EQ(s.panel().getMinPitchSelection(), 38) << "C2 in C is D2 in D";
    EXPECT_EQ(s.panel().getMaxPitchSelection(), 74);
}
