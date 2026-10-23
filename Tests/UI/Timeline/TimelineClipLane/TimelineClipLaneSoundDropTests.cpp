// A Sound dragged onto the clip lanes: an audio row takes it like an OS file drop, the empty space under the last track
// offers a new audio track, and the lanes outline what would take it while a sound is dragged.
#include "TimelineClipLaneTestFixture.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"

namespace {

int rowCentre(const TimelineClipLaneArea& lane, int index) {
    return index * lane.getRowHeight() + lane.getRowHeight() / 2;
}

juce::DragAndDropTarget::SourceDetails sound(const juce::String& path, juce::Point<int> at) {
    return {synth::ui::makeSoundPayload(path), nullptr, at};
}

} // namespace

TEST(TimelineClipLaneSoundDropTest, ASoundOnAnAudioRowAddsAClipAtTheSnappedBeat) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Bar;
    f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto audio = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    double beat = -1.0;
    synth::TrackId track;
    juce::File file;
    f.lane.onAudioFileDropped = [&](synth::TrackId t, double b, juce::File fl) {
        track = t;
        beat = b;
        file = fl;
    };

    const auto details = sound("/tmp/loop.wav", {200, rowCentre(f.lane, 1)});
    ASSERT_TRUE(f.lane.isInterestedInDragSource(details));
    f.lane.itemDragMove(details);
    EXPECT_EQ(f.lane.getFileDropRowForTest(), 1) << "the same row highlight an OS file drag gets";
    f.lane.itemDropped(details);
    EXPECT_EQ(track, audio);
    EXPECT_DOUBLE_EQ(beat, 4.0);
    EXPECT_EQ(file, juce::File("/tmp/loop.wav"));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1);

    f.lane.itemDragMove(sound("/tmp/loop.wav", {200, rowCentre(f.lane, 0)}));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1) << "a MIDI row never highlights";
}

TEST(TimelineClipLaneSoundDropTest, AnInstrumentSoundOrAModuleDragIsNotTaken) {
    ClipLaneFixture f;
    f.doc.addTrack(TrackKind::Audio, "Audio 1");
    EXPECT_FALSE(f.lane.isInterestedInDragSource(sound("/tmp/piano.sfz", {10, 10})));
    juce::DragAndDropTarget::SourceDetails module("Oscillator", nullptr, {10, 10});
    EXPECT_FALSE(f.lane.isInterestedInDragSource(module));
}

TEST(TimelineClipLaneSoundDropTest, TheSpaceUnderTheLastTrackOffersANewAudioTrack) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Bar;
    f.doc.addTrack(TrackKind::Audio, "Audio 1");
    double beat = -1.0;
    juce::File file;
    f.lane.onAudioFileDroppedBelowTracks = [&](double b, juce::File fl) {
        beat = b;
        file = fl;
    };
    const int below = rowCentre(f.lane, 3);

    const auto details = sound("/tmp/loop.wav", {200, below});
    f.lane.itemDragMove(details);
    EXPECT_TRUE(f.lane.isBelowTracksHintForTest());
    EXPECT_FALSE(f.lane.getBelowTracksStripForTest().isEmpty());
    f.lane.itemDragMove(sound("/tmp/loop.wav", {200, rowCentre(f.lane, 0)}));
    EXPECT_FALSE(f.lane.isBelowTracksHintForTest()) << "over a row, the strip is not the target";

    f.lane.itemDragMove(details);
    f.lane.itemDropped(details);
    EXPECT_DOUBLE_EQ(beat, 4.0);
    EXPECT_EQ(file, juce::File("/tmp/loop.wav"));
    EXPECT_FALSE(f.lane.isBelowTracksHintForTest());
}

TEST(TimelineClipLaneSoundDropTest, AnOsFileDropBelowTheTracksIsOfferedTheSameWay) {
    ClipLaneFixture f;
    double beat = -1.0;
    f.lane.onAudioFileDroppedBelowTracks = [&](double b, juce::File) { beat = b; };
    f.lane.fileDragMove({"/tmp/loop.wav"}, 120, 200);
    EXPECT_TRUE(f.lane.isBelowTracksHintForTest()) << "with no tracks at all, the whole area is the strip's";
    f.lane.filesDropped({"/tmp/loop.wav"}, 120, 200);
    EXPECT_GE(beat, 0.0);
    f.lane.fileDragMove({"/tmp/notes.txt"}, 120, 200);
    EXPECT_FALSE(f.lane.isBelowTracksHintForTest());
}

TEST(TimelineClipLaneSoundDropTest, WithoutTheOwnersCallbackTheSpaceBelowTakesNothing) {
    ClipLaneFixture f;
    f.doc.addTrack(TrackKind::Audio, "Audio 1");
    f.lane.fileDragMove({"/tmp/loop.wav"}, 120, rowCentre(f.lane, 3));
    EXPECT_FALSE(f.lane.isBelowTracksHintForTest());
}

TEST(TimelineClipLaneSoundDropTest, TheLanesOutlineWhileAnAudioSoundIsDraggedAndNotForAnInstrument) {
    ClipLaneFixture f;
    f.doc.addTrack(TrackKind::Audio, "Audio 1");
    EXPECT_FALSE(f.lane.isShowingSoundOutline());
    synth::ui::SoundDragState::begin(juce::File("/tmp/piano.sfz"));
    EXPECT_FALSE(f.lane.isShowingSoundOutline());
    synth::ui::SoundDragState::begin(juce::File("/tmp/loop.wav"));
    EXPECT_TRUE(f.lane.isShowingSoundOutline());
    synth::ui::SoundDragState::end();
    EXPECT_FALSE(f.lane.isShowingSoundOutline());
}
