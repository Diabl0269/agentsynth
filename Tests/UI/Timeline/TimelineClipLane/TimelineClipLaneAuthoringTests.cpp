// TimelineClipLaneAuthoringTests.cpp — double-click clip authoring (MIDI one-bar / audio file
// chooser), the loop-locator double-click span, OS file drag/drop, and the empty-row hint.
#include "TimelineClipLaneTestFixture.h"

namespace {

// The one y that lands on row `index` regardless of the themed/headless row height.
float rowCentreY(const TimelineClipLaneArea& lane, int index) {
    const int rowHeight = lane.getRowHeight();
    return (float)(index * rowHeight + rowHeight / 2);
}

} // namespace

TEST(TimelineClipLaneAuthoringTest, DoubleClickEmptyMidiRowCreatesOneBarClipAndOpensIt) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Bar; // 4 beats (no transport) = 160 px at 40 px/beat
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");

    std::vector<ClipId> opened;
    f.lane.onClipDoubleClicked = [&opened](ClipId id) { opened.push_back(id); };

    // x = 200 px -> beat 5.0, which FLOORS to bar 2 (beat 4.0) rather than snapping forward to 8.
    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 0)}));

    const auto* track = f.doc.getTrack(trackId);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 1u);
    const auto& clip = track->clips[0];
    EXPECT_DOUBLE_EQ(clip.startBeat, 4.0) << "the snap grid line at or BEFORE the click, never after it";
    EXPECT_DOUBLE_EQ(clip.lengthBeats, 4.0) << "one bar at the default 4/4";
    EXPECT_EQ(clip.name, juce::String("Clip 1"));
    EXPECT_TRUE(clip.assetRef.isEmpty()) << "a MIDI clip carries notes, not an asset";
    EXPECT_TRUE(f.selection.contains(clip.id)) << "the new clip is selected";
    ASSERT_EQ(opened.size(), 1u) << "the same open-piano-roll path a clip double-click uses must fire";
    EXPECT_EQ(opened[0], clip.id);

    // ONE undo step, and undoing removes the clip entirely.
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getTrack(trackId)->clips.empty());
    EXPECT_FALSE(f.undo.canUndo()) << "creating a clip was ONE undo step";
}

TEST(TimelineClipLaneAuthoringTest, DoubleClickNamesClipsInSequenceAndSnapOffKeepsTheRawBeat) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Off;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");

    f.lane.mouseDoubleClick(leftClick(f.lane, {80.0f, rowCentreY(f.lane, 0)}));  // beat 2.0
    f.lane.mouseDoubleClick(leftClick(f.lane, {400.0f, rowCentreY(f.lane, 0)})); // beat 10.0

    const auto* track = f.doc.getTrack(trackId);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 2u);
    EXPECT_DOUBLE_EQ(track->clips[0].startBeat, 2.0) << "Snap::Off passes the raw beat through";
    EXPECT_DOUBLE_EQ(track->clips[1].startBeat, 10.0);
    EXPECT_EQ(track->clips[1].name, juce::String("Clip 2")) << "the auto-name counts the row's clips";
}

// ---------------------------------------------------------------------------
// Double-click spans the loop locators (preference-gated, default ON).
// ---------------------------------------------------------------------------

TEST(TimelineClipLaneLocatorSpanTest, DoubleClickInsideTheLocatorsSpansThemByDefault) {
    LocatorSpanFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    f.setLocators(2.0, 6.0);

    // x = 160 px -> beat 4.0, inside [2, 6).
    f.lane.mouseDoubleClick(leftClick(f.lane, {160.0f, rowCentreY(f.lane, 0)}));

    const auto* track = f.doc.getTrack(trackId);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(track->clips[0].startBeat, 2.0) << "the clip starts at the LEFT locator, not the click";
    EXPECT_DOUBLE_EQ(track->clips[0].lengthBeats, 4.0) << "and runs to the right one";

    // Still ONE undo step, exactly like the one-bar path.
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getTrack(trackId)->clips.empty());
}

TEST(TimelineClipLaneLocatorSpanTest, OutsideTheLocatorsKeepsTheOneBarBehaviour) {
    LocatorSpanFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    f.setLocators(2.0, 6.0);

    // x = 400 px -> beat 10.0, past the right locator.
    f.lane.mouseDoubleClick(leftClick(f.lane, {400.0f, rowCentreY(f.lane, 0)}));
    // x = 20 px -> beat 0.5, before the left one.
    f.lane.mouseDoubleClick(leftClick(f.lane, {20.0f, rowCentreY(f.lane, 0)}));

    const auto* track = f.doc.getTrack(trackId);
    ASSERT_EQ(track->clips.size(), 2u);
    for (const auto& clip : track->clips)
        EXPECT_DOUBLE_EQ(clip.lengthBeats, 4.0) << "one bar at the default 4/4";
    EXPECT_DOUBLE_EQ(track->clips[0].startBeat, 0.5) << "authored where the user clicked (Snap::Off)";
    EXPECT_DOUBLE_EQ(track->clips[1].startBeat, 10.0);
}

// The span is half-open: a click exactly ON the right locator belongs to what comes after the loop.
TEST(TimelineClipLaneLocatorSpanTest, TheSpanIsHalfOpenAtBothEnds) {
    LocatorSpanFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    f.setLocators(2.0, 6.0);

    // Exactly on the LEFT locator (beat 2.0) is inside.
    f.lane.mouseDoubleClick(leftClick(f.lane, {80.0f, rowCentreY(f.lane, 0)}));
    ASSERT_EQ(f.doc.getTrack(trackId)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(trackId)->clips[0].startBeat, 2.0);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(trackId)->clips[0].lengthBeats, 4.0);

    // Exactly on the RIGHT locator (beat 6.0) is not.
    f.lane.mouseDoubleClick(leftClick(f.lane, {240.0f, rowCentreY(f.lane, 0)}));
    const auto* track = f.doc.getTrack(trackId);
    ASSERT_EQ(track->clips.size(), 2u);
    const auto& atRightLocator = track->clips[1];
    EXPECT_DOUBLE_EQ(atRightLocator.startBeat, 6.0);
    EXPECT_DOUBLE_EQ(atRightLocator.lengthBeats, 4.0) << "one bar, not a second copy of the loop";
}

TEST(TimelineClipLaneLocatorSpanTest, PreferenceOffAlwaysGivesOneBar) {
    LocatorSpanFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    f.setLocators(2.0, 6.0);
    f.setPreference(false);

    f.lane.mouseDoubleClick(leftClick(f.lane, {160.0f, rowCentreY(f.lane, 0)})); // beat 4.0, inside
    const auto* track = f.doc.getTrack(trackId);
    ASSERT_EQ(track->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(track->clips[0].startBeat, 4.0);
    EXPECT_DOUBLE_EQ(track->clips[0].lengthBeats, 4.0);

    // Flipping it back on takes effect on the very next double-click — the key is read at use
    // time, never cached. Clicked at beat 2.5, which is inside the locators AND clear of the
    // one-bar clip just authored at [4, 8) (a double-click ON a clip opens the roll instead).
    f.setPreference(true);
    f.lane.mouseDoubleClick(leftClick(f.lane, {100.0f, rowCentreY(f.lane, 0)}));
    ASSERT_EQ(f.doc.getTrack(trackId)->clips.size(), 2u);
    const auto& spanned = f.doc.getTrack(trackId)->clips[0];
    EXPECT_DOUBLE_EQ(spanned.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(spanned.lengthBeats, 4.0);
}

// A degenerate span (end <= start) is also what "no locators set yet" looks like: nothing to span.
TEST(TimelineClipLaneLocatorSpanTest, DegenerateOrAbsentLocatorsFallBackToOneBar) {
    LocatorSpanFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");

    // A fresh transport's loop is whatever it defaults to; force the degenerate case explicitly by
    // asking locatorSpanForDoubleClick directly (setLoop itself refuses a zero-length range).
    const auto snap = f.transport.getPositionSnapshot();
    if (!(snap.loopEndPpq > snap.loopStartPpq))
        EXPECT_FALSE(f.lane.locatorSpanForDoubleClick(0.0).has_value());

    // With a real span, the same query answers for a beat inside and declines for one outside.
    f.setLocators(2.0, 6.0);
    ASSERT_TRUE(f.lane.locatorSpanForDoubleClick(4.0).has_value());
    EXPECT_DOUBLE_EQ(f.lane.locatorSpanForDoubleClick(4.0)->first, 2.0);
    EXPECT_DOUBLE_EQ(f.lane.locatorSpanForDoubleClick(4.0)->second, 6.0);
    EXPECT_FALSE(f.lane.locatorSpanForDoubleClick(6.0).has_value());
    EXPECT_FALSE(f.lane.locatorSpanForDoubleClick(-1.0).has_value());

    // Looping switched OFF still counts: the locators are a RANGE, and disarming them only stops
    // playback from wrapping (see TimelineRulerComponent::braceStateFor, same rule).
    f.setLocators(2.0, 6.0, /*arm=*/false);
    EXPECT_TRUE(f.lane.locatorSpanForDoubleClick(4.0).has_value());
    f.lane.mouseDoubleClick(leftClick(f.lane, {160.0f, rowCentreY(f.lane, 0)}));
    ASSERT_EQ(f.doc.getTrack(trackId)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(trackId)->clips[0].lengthBeats, 4.0);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(trackId)->clips[0].startBeat, 2.0);
}

// No transport (and no settings file) at all: the feature is simply unavailable, never a crash.
TEST(TimelineClipLaneLocatorSpanTest, NoTransportOrNoSettingsFallsBackToOneBar) {
    ClipLaneFixture bare; // no transport, no ApplicationProperties
    const auto trackId = bare.doc.addTrack(TrackKind::Midi, "Track 1");
    bare.state.snap = TimelineViewState::Snap::Off;
    EXPECT_FALSE(bare.lane.locatorSpanForDoubleClick(4.0).has_value());

    bare.lane.mouseDoubleClick(leftClick(bare.lane, {160.0f, rowCentreY(bare.lane, 0)}));
    ASSERT_EQ(bare.doc.getTrack(trackId)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(bare.doc.getTrack(trackId)->clips[0].startBeat, 4.0);
    EXPECT_DOUBLE_EQ(bare.doc.getTrack(trackId)->clips[0].lengthBeats, 4.0);
}

// The Draw tool authors its own clips through the same createMidiClipAt and must NOT pick up the
// locator span — a drag states its own length.
TEST(TimelineClipLaneLocatorSpanTest, TheDrawToolIsUnaffectedByTheLocators) {
    LocatorSpanFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    f.setLocators(2.0, 6.0);
    f.lane.setActiveTool(synth::ui::EditTool::Draw);

    // A Draw press inside the locators, released without a drag: one bar, as it always was.
    const juce::Point<float> pos(160.0f, rowCentreY(f.lane, 0));
    f.lane.mouseDown(leftClick(f.lane, pos));
    f.lane.mouseUp(leftClick(f.lane, pos));

    const auto* track = f.doc.getTrack(trackId);
    ASSERT_EQ(track->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(track->clips[0].lengthBeats, 4.0);
    EXPECT_DOUBLE_EQ(track->clips[0].startBeat, 4.0) << "the Draw anchor, not the left locator";
}

TEST(TimelineClipLaneAuthoringTest, DoubleClickIgnoresAutomationRowsAndEmptyPanelSpace) {
    ClipLaneFixture f;
    const auto automationTrack = f.doc.addTrack(TrackKind::Automation, "Automation");
    ASSERT_TRUE(automationTrack.isValid());

    int opens = 0;
    f.lane.onClipDoubleClicked = [&opens](ClipId) { ++opens; };

    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 0)}));
    EXPECT_TRUE(f.doc.getTrack(automationTrack)->clips.empty()) << "an automation row authors nothing";

    // Below the last row: panel space, not a row.
    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 4)}));
    EXPECT_EQ(f.doc.getTrack(automationTrack)->clips.size(), 0u);

    EXPECT_EQ(opens, 0);
    EXPECT_FALSE(f.undo.canUndo()) << "neither gesture may create an undo step";
}

TEST(TimelineClipLaneAuthoringTest, DoubleClickEmptyAudioRowAsksForAFileAndReportsTheChoice) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Bar;
    const auto midiTrack = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(midiTrack.isValid());

    const juce::File fixture =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_clipslane_chooser.wav");

    // The injected chooser stands in for juce::FileChooser, which never runs in a test process —
    // same idiom as applyClipContextChoice standing in for showMenuAsync.
    int chooserCalls = 0;
    f.lane.setAudioFileChooser([&](std::function<void(const juce::File&)> onChosen) {
        ++chooserCalls;
        onChosen(fixture);
    });

    struct Report {
        synth::TrackId track;
        double beat = 0.0;
        juce::File file;
    };
    std::vector<Report> reports;
    f.lane.onAudioFileDropped = [&reports](synth::TrackId track, double beat, juce::File file) {
        reports.push_back({track, beat, file});
    };

    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 1)}));

    EXPECT_EQ(chooserCalls, 1);
    ASSERT_EQ(reports.size(), 1u) << "the lane area reports the choice; the OWNER imports it";
    EXPECT_EQ(reports[0].track, audioTrack);
    EXPECT_DOUBLE_EQ(reports[0].beat, 4.0) << "the same floor-snapped beat a MIDI clip would start on";
    EXPECT_EQ(reports[0].file, fixture);
    EXPECT_TRUE(f.doc.getTrack(audioTrack)->clips.empty()) << "the lane area never creates the audio clip itself";
    EXPECT_FALSE(f.undo.canUndo());

    // A cancelled dialog reports nothing.
    f.lane.setAudioFileChooser([](std::function<void(const juce::File&)> onChosen) { onChosen(juce::File()); });
    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 1)}));
    EXPECT_EQ(reports.size(), 1u);

    // A double-click on the MIDI row still creates a clip rather than asking for a file.
    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 0)}));
    EXPECT_EQ(chooserCalls, 1);
    EXPECT_EQ(f.doc.getTrack(midiTrack)->clips.size(), 1u);
}

TEST(TimelineClipLaneAuthoringTest, FileDragInterestIsExtensionBased) {
    ClipLaneFixture f;
    EXPECT_TRUE(f.lane.isInterestedInFileDrag({"/tmp/loop.wav"}));
    EXPECT_TRUE(f.lane.isInterestedInFileDrag({"/tmp/notes.txt", "/tmp/loop.aiff"}))
        << "at least one readable audio file is enough";
    EXPECT_FALSE(f.lane.isInterestedInFileDrag({"/tmp/notes.txt"}));
    EXPECT_FALSE(f.lane.isInterestedInFileDrag({}));
}

TEST(TimelineClipLaneAuthoringTest, FileDragHighlightsAudioRowsOnly) {
    ClipLaneFixture f;
    f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(audioTrack.isValid());
    const juce::StringArray dragged{"/tmp/loop.wav"};

    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1);

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 1));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), 1) << "the audio row under the cursor highlights";

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 0));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1) << "a MIDI row never highlights";

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 1));
    f.lane.fileDragMove({"/tmp/notes.txt"}, 200, (int)rowCentreY(f.lane, 1));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1) << "nothing droppable, nothing highlighted";

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 1));
    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 5)); // below the last row
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1);

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 1));
    f.lane.fileDragExit(dragged);
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1) << "leaving the component clears the highlight";
}

TEST(TimelineClipLaneAuthoringTest, FilesDroppedReportsTrackAndSnappedBeatForAudioRowsOnly) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Bar;
    const auto midiTrack = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(midiTrack.isValid());

    struct Report {
        synth::TrackId track;
        double beat = 0.0;
        juce::File file;
    };
    std::vector<Report> reports;
    f.lane.onAudioFileDropped = [&reports](synth::TrackId track, double beat, juce::File file) {
        reports.push_back({track, beat, file});
    };

    // Two files dropped together: the FIRST readable audio one wins, and the .txt is ignored.
    f.lane.fileDragMove({"/tmp/loop.wav"}, 200, (int)rowCentreY(f.lane, 1));
    f.lane.filesDropped({"/tmp/notes.txt", "/tmp/loop.wav", "/tmp/second.wav"}, 200, (int)rowCentreY(f.lane, 1));

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].track, audioTrack);
    EXPECT_DOUBLE_EQ(reports[0].beat, 4.0) << "x = 200 px -> beat 5.0, floored onto the bar grid";
    EXPECT_EQ(reports[0].file, juce::File("/tmp/loop.wav"));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1) << "the drop clears the highlight";

    // A MIDI row, an automation row and empty panel space all refuse the drop.
    f.lane.filesDropped({"/tmp/loop.wav"}, 200, (int)rowCentreY(f.lane, 0));
    f.lane.filesDropped({"/tmp/loop.wav"}, 200, (int)rowCentreY(f.lane, 7));
    EXPECT_EQ(reports.size(), 1u);
    EXPECT_TRUE(f.doc.getTrack(midiTrack)->clips.empty());
}

TEST(TimelineClipLaneAuthoringTest, EmptyRowHintTextIsPerKindAndOnlyForEmptyRows) {
    ClipLaneFixture f;
    const auto midiTrack = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    f.doc.addTrack(TrackKind::Automation, "Automation");
    ASSERT_TRUE(audioTrack.isValid());

    // ASCII: the hint used to carry a UTF-8 em dash, which juce::String decoded as Latin-1 and
    // painted as mojibake (see CLAUDE.md's string-literal invariant).
    EXPECT_EQ(f.lane.getEmptyRowHintForTest(0), juce::String("Double-click to add a clip - or arm (R) and record"));
    EXPECT_EQ(f.lane.getEmptyRowHintForTest(1), juce::String("Drop an audio file - or arm (R) and record"));
    EXPECT_TRUE(f.lane.getEmptyRowHintForTest(2).isEmpty()) << "an automation row has nothing to author";
    EXPECT_TRUE(f.lane.getEmptyRowHintForTest(9).isEmpty()) << "no such row";

    // A row with ANY clip on it stops hinting.
    ASSERT_TRUE(f.doc.addClip(midiTrack, 0.0, 4.0, "Clip 1").isValid());
    EXPECT_TRUE(f.lane.getEmptyRowHintForTest(0).isEmpty());
    EXPECT_FALSE(f.lane.getEmptyRowHintForTest(1).isEmpty()) << "the audio row is still empty";
}

TEST(TimelineClipLaneAuthoringTest, EmptyRowHintIsPaintedAndDroppedWhenTooNarrow) {
    // Both fixtures paint ONE empty row at the same size, differing only in track kind — so any
    // pixel difference is the hint line itself (an automation row never hints).
    const auto imageFor = [](TrackKind kind, int width) {
        ClipLaneFixture f;
        f.doc.addTrack(kind, "Row");
        f.lane.setSize(width, f.lane.getRowHeight());
        return f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    };

    const auto wideMidi = imageFor(TrackKind::Midi, 1000);
    const auto wideAutomation = imageFor(TrackKind::Automation, 1000);
    ASSERT_FALSE(wideMidi.isNull());
    EXPECT_FALSE(imagesIdentical(wideMidi, wideAutomation)) << "an empty MIDI row must paint its hint line";

    // Too narrow for the line plus its padding: dropped entirely rather than truncated, so the row
    // paints exactly like the (never-hinting) automation one.
    const auto narrowMidi = imageFor(TrackKind::Midi, 90);
    const auto narrowAutomation = imageFor(TrackKind::Automation, 90);
    ASSERT_FALSE(narrowMidi.isNull());
    EXPECT_TRUE(imagesIdentical(narrowMidi, narrowAutomation)) << "a row too narrow to read must not paint the hint";
}

TEST(TimelineClipLaneAuthoringTest, FileDropHighlightPaints) {
    ClipLaneFixture f;
    f.doc.addTrack(TrackKind::Audio, "Audio 1");
    f.lane.setSize(1000, f.lane.getRowHeight());

    const auto before = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    f.lane.fileDragMove({"/tmp/loop.wav"}, 200, (int)rowCentreY(f.lane, 0));
    ASSERT_EQ(f.lane.getFileDropRowForTest(), 0);
    const auto during = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    f.lane.fileDragExit({"/tmp/loop.wav"});
    const auto after = f.lane.createComponentSnapshot(f.lane.getLocalBounds());

    EXPECT_FALSE(imagesIdentical(before, during)) << "the hovered audio row must be visibly marked";
    EXPECT_TRUE(imagesIdentical(before, after)) << "and the mark must be gone once the drag leaves";
}
