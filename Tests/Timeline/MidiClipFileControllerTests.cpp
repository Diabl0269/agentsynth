// MidiClipFile and clip CC lanes: export writes them as controller events (Linear sampled, unchanged
// values dropped), import reads controller events back into Hold lanes, and both import paths only
// write them when asked (docs/timeline/piano-roll-lanes.md#midi-files).

#include "Timeline/MidiClipFile.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

using synth::ControllerPoint;
using synth::MidiClipFile;
using synth::TimelineDoc;
using synth::TrackKind;

namespace {

struct CcClip {
    TimelineDoc doc;
    synth::TrackId track;
    synth::ClipId clip;
    CcClip() {
        track = doc.addTrack(TrackKind::Midi, "T");
        clip = doc.addClip(track, 0.0, 4.0, "c");
        synth::MidiNote note;
        note.startBeat = 0.0;
        note.channel = 2;
        doc.addNote(clip, note);
    }

    MidiClipFile::ImportResult roundTrip() const {
        juce::MemoryOutputStream out;
        EXPECT_TRUE(MidiClipFile::exportClip(doc, clip, out));
        juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
        return MidiClipFile::importFromStream(in);
    }

    std::vector<juce::MidiMessage> exportedControllers() const {
        juce::MemoryOutputStream out;
        EXPECT_TRUE(MidiClipFile::exportClip(doc, clip, out));
        juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
        juce::MidiFile file;
        EXPECT_TRUE(file.readFrom(in));
        std::vector<juce::MidiMessage> ccs;
        for (int t = 0; t < file.getNumTracks(); ++t)
            for (const auto* event : *file.getTrack(t))
                if (event->message.isController())
                    ccs.push_back(event->message);
        return ccs;
    }
};

} // namespace

TEST(MidiClipFileControllerTest, HoldLaneRoundTripsExactlyOnTheNoteChannel) {
    CcClip f;
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 64, {{0.0, 127.0, 0}, {1.5, 0.0, 0}, {3.0, 127.0, 0}}));
    const auto ccs = f.exportedControllers();
    ASSERT_EQ(ccs.size(), 3u);
    for (const auto& m : ccs)
        EXPECT_EQ(m.getChannel(), 2) << "CCs play on the clip's note channels";

    const auto result = f.roundTrip();
    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(result.hasControllerData());
    ASSERT_EQ(result.tracks.size(), 1u);
    ASSERT_EQ(result.tracks[0].controllers.size(), 1u);
    const auto& lane = result.tracks[0].controllers[0];
    EXPECT_EQ(lane.ccNumber, 64);
    ASSERT_EQ(lane.points.size(), 3u);
    EXPECT_DOUBLE_EQ(lane.points[1].beat, 1.5);
    EXPECT_DOUBLE_EQ(lane.points[1].value, 0.0);
    EXPECT_EQ(lane.points[2].curve, static_cast<int>(synth::BreakpointCurve::Hold));
}

TEST(MidiClipFileControllerTest, LinearSegmentIsSampledAndUnchangedValuesAreDropped) {
    CcClip f;
    // 0 -> 127 over 2 beats, then flat: sampled at 1/32 beat, one event per CHANGE only.
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 1, {{0.0, 0.0, 1}, {2.0, 127.0, 1}, {3.0, 127.0, 1}}));
    const auto ccs = f.exportedControllers();
    ASSERT_GE(ccs.size(), 60u) << "64 sample steps across the ramp";
    ASSERT_LE(ccs.size(), 66u) << "the flat tail adds nothing";
    for (size_t i = 1; i < ccs.size(); ++i)
        EXPECT_GT(ccs[i].getControllerValue(), ccs[i - 1].getControllerValue());
    EXPECT_EQ(ccs.back().getControllerValue(), 127);
    EXPECT_NEAR(ccs.back().getTimeStamp(), 2.0 * MidiClipFile::kExportPpq, 1.0);
}

TEST(MidiClipFileControllerTest, ImportIntoTrackWritesControllersOnlyWhenAsked) {
    CcClip f;
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 11, {{0.0, 40.0, 0}, {2.0, 80.0, 0}}));
    const auto result = f.roundTrip();

    TimelineDoc notesOnly;
    const auto t1 = notesOnly.addTrack(TrackKind::Midi, "A");
    ASSERT_TRUE(MidiClipFile::importIntoTrack(notesOnly, t1, 0.0, result));
    EXPECT_TRUE(notesOnly.getTrack(t1)->clips[0].controllers.empty()) << "the default (and AI) path is notes only";

    TimelineDoc withCc;
    const auto t2 = withCc.addTrack(TrackKind::Midi, "B");
    ASSERT_TRUE(MidiClipFile::importIntoTrack(withCc, t2, 0.0, result, true));
    const auto& clip = withCc.getTrack(t2)->clips[0];
    ASSERT_EQ(clip.controllers.size(), 1u);
    EXPECT_EQ(clip.controllers[0].ccNumber, 11);
    EXPECT_EQ(clip.controllers[0].points.size(), 2u);
}

TEST(MidiClipFileControllerTest, ImportIntoClipMergesNotesGrowsAndReplacesLanes) {
    CcClip source;
    ASSERT_TRUE(source.doc.setControllerLanePoints(source.clip, 1, {{0.0, 100.0, 0}}));
    synth::MidiNote late;
    late.startBeat = 6.0;
    source.doc.resizeClip(source.clip, 8.0);
    source.doc.addNote(source.clip, late);
    const auto result = source.roundTrip();

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 2.0, "c");
    ASSERT_TRUE(doc.setControllerLanePoints(clip, 1, {{0.0, 5.0, 1}, {1.0, 6.0, 1}}));

    ASSERT_TRUE(MidiClipFile::importIntoClip(doc, clip, result, /*withControllers=*/false));
    EXPECT_EQ(doc.getClip(clip)->notes.size(), 2u);
    EXPECT_GE(doc.getClip(clip)->lengthBeats, 7.0) << "grown to fit the last note";
    EXPECT_EQ(doc.getControllerLane(clip, 1)->points.size(), 2u) << "notes only: the lane is untouched";

    ASSERT_TRUE(MidiClipFile::importIntoClip(doc, clip, result, /*withControllers=*/true));
    ASSERT_EQ(doc.getControllerLane(clip, 1)->points.size(), 1u) << "the imported lane replaces it";
    EXPECT_DOUBLE_EQ(doc.getControllerLane(clip, 1)->points[0].value, 100.0);
}

TEST(MidiClipFileControllerTest, ChannelModeMessagesAreNotImportedAsLanes) {
    juce::MidiMessageSequence sequence;
    auto on = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    on.setTimeStamp(0);
    sequence.addEvent(on);
    auto off = juce::MidiMessage::noteOff(1, 60);
    off.setTimeStamp(480);
    sequence.addEvent(off);
    auto allNotesOff = juce::MidiMessage::controllerEvent(1, 123, 0);
    allNotesOff.setTimeStamp(480);
    sequence.addEvent(allNotesOff);
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    file.addTrack(sequence);
    juce::MemoryOutputStream out;
    ASSERT_TRUE(file.writeTo(out, 1));
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    const auto result = MidiClipFile::importFromStream(in);
    ASSERT_TRUE(result.ok);
    EXPECT_FALSE(result.hasControllerData());
}

// ---- Review regressions -------------------------------------------------------------------------

namespace {

// A format-1 file: track 0 holds only CC, track 1 the notes, track 2 more CC for the same number.
MidiClipFile::ImportResult controllerTrackFile() {
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence ccTrack;
    auto a = juce::MidiMessage::controllerEvent(1, 1, 20);
    a.setTimeStamp(0);
    ccTrack.addEvent(a);
    auto b = juce::MidiMessage::controllerEvent(1, 1, 40);
    b.setTimeStamp(960);
    ccTrack.addEvent(b);
    file.addTrack(ccTrack);
    juce::MidiMessageSequence notes;
    auto on = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    on.setTimeStamp(0);
    notes.addEvent(on);
    auto off = juce::MidiMessage::noteOff(1, 60);
    off.setTimeStamp(480);
    notes.addEvent(off);
    file.addTrack(notes);
    juce::MidiMessageSequence more;
    auto c = juce::MidiMessage::controllerEvent(1, 1, 80);
    c.setTimeStamp(1920);
    more.addEvent(c);
    file.addTrack(more);
    juce::MemoryOutputStream out;
    file.writeTo(out, 1);
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    return MidiClipFile::importFromStream(in);
}

} // namespace

TEST(MidiClipFileControllerTest, ControllerOnlyTracksAreKeptAndMergedPerCc) {
    const auto result = controllerTrackFile();
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.tracks.size(), 3u) << "CC-only tracks are no longer dropped";
    EXPECT_TRUE(result.hasControllerData());

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 1.0, "c");
    ASSERT_TRUE(MidiClipFile::importIntoClip(doc, clip, result, true));
    const auto* lane = doc.getControllerLane(clip, 1);
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 3u) << "both tracks' CC 1 streams merged, not last-track-wins";
    EXPECT_DOUBLE_EQ(lane->points[0].value, 20.0);
    EXPECT_DOUBLE_EQ(lane->points[1].value, 40.0);
    EXPECT_DOUBLE_EQ(lane->points[2].value, 80.0);

    TimelineDoc intoTrack;
    const auto t2 = intoTrack.addTrack(TrackKind::Midi, "T");
    ASSERT_TRUE(MidiClipFile::importIntoTrack(intoTrack, t2, 0.0, result, true));
    ASSERT_EQ(intoTrack.getTrack(t2)->clips.size(), 1u) << "one note clip; CC-only tracks merge into it";
    EXPECT_EQ(intoTrack.getTrack(t2)->clips[0].controllers.size(), 1u);
    EXPECT_EQ(intoTrack.getTrack(t2)->clips[0].controllers[0].points.size(), 3u);
}

TEST(MidiClipFileControllerTest, AControllerOnlyFileImportsIntoAClipWhenAsked) {
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence ccTrack;
    auto a = juce::MidiMessage::controllerEvent(1, 11, 64);
    a.setTimeStamp(480);
    ccTrack.addEvent(a);
    file.addTrack(ccTrack);
    juce::MemoryOutputStream out;
    file.writeTo(out, 1);
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    const auto result = MidiClipFile::importFromStream(in);
    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(result.hasControllerData());

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    EXPECT_FALSE(MidiClipFile::importIntoClip(doc, clip, result, false)) << "notes only: nothing to add";
    ASSERT_TRUE(MidiClipFile::importIntoClip(doc, clip, result, true));
    ASSERT_NE(doc.getControllerLane(clip, 11), nullptr);
    ASSERT_TRUE(MidiClipFile::importIntoTrack(doc, track, 8.0, result, true));
    EXPECT_EQ(doc.getTrack(track)->clips.size(), 2u) << "a CC-only file becomes one CC clip";
    EXPECT_FALSE(MidiClipFile::importIntoTrack(doc, track, 16.0, result)) << "notes-only path: nothing";
}
