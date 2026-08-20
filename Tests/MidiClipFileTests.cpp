// Tests for MidiClipFile — Standard MIDI File (SMF) import/export for timeline clips.
//
// Most of these hand-build a juce::MidiFile/MidiMessageSequence directly (the same house style
// juce's own MidiFile unit tests use) rather than going through MidiClipFile::exportClip, so each
// test exercises importFromStream() against a file it fully controls. RoundTripClip is the
// exception — it drives both halves together to prove the export/import contract composes.

#include "../Source/Timeline/MidiClipFile.h"
#include "../Source/Timeline/TimelineDoc.h"
#include <cstdint>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

using synth::ClipId;
using synth::MidiClipFile;
using synth::MidiNote;
using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;

namespace {

// Round-trip tolerance: PPQ 960 quantises to the nearest tick, so the worst case is half a tick.
constexpr double kBeatTol = 1.0 / 1920.0 + 1e-9;

MidiNote makeNote(double startBeat, double lengthBeats, int pitch, int velocity, int channel) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

juce::MidiMessage timestamped(juce::MidiMessage message, double tick) {
    message.setTimeStamp(tick);
    return message;
}

} // namespace

// ============================================================================
// 1. RoundTripClip
// ============================================================================

TEST(MidiClipFileTest, RoundTripClip) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c");

    // A chord, overlapping same-pitch retriggers, multiple channels and velocities.
    const std::vector<MidiNote> wants = {
        makeNote(0.0, 1.0, 60, 100, 1), makeNote(0.0, 1.0, 64, 90, 1),   makeNote(0.0, 1.0, 67, 80, 2),
        makeNote(0.5, 2.0, 72, 70, 3), // overlapping same pitch/channel as the note below
        makeNote(1.5, 1.0, 72, 60, 3),  makeNote(3.0, 1.0, 50, 127, 16),
    };
    for (const auto& note : wants)
        ASSERT_TRUE(doc.addNote(clip, note).isValid());

    juce::MemoryOutputStream out;
    ASSERT_TRUE(MidiClipFile::exportClip(doc, clip, out));

    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    const auto result = MidiClipFile::importFromStream(in);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.tracks.size(), 1u);

    const auto& roundTripped = result.tracks[0].notes;
    const auto& original = doc.getClip(clip)->notes;
    ASSERT_EQ(roundTripped.size(), original.size());

    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_NEAR(roundTripped[i].startBeat, original[i].startBeat, kBeatTol) << "note " << i;
        EXPECT_NEAR(roundTripped[i].lengthBeats, original[i].lengthBeats, kBeatTol) << "note " << i;
        EXPECT_EQ(roundTripped[i].pitch, original[i].pitch) << "note " << i;
        EXPECT_EQ(roundTripped[i].velocity, original[i].velocity) << "note " << i;
        EXPECT_EQ(roundTripped[i].channel, original[i].channel) << "note " << i;
    }
}

// ============================================================================
// 2. ImportType1MultiTrack
// ============================================================================

TEST(MidiClipFileTest, ImportType1MultiTrack) {
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(480); // non-960 PPQ, to prove the divide

    {
        juce::MidiMessageSequence seq;
        seq.addEvent(timestamped(juce::MidiMessage::textMetaEvent(3, "Lead"), 0));
        seq.addEvent(timestamped(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0));
        seq.addEvent(timestamped(juce::MidiMessage::noteOff(1, 60), 480)); // 1 beat at 480 ppq
        midiFile.addTrack(seq);
    }
    {
        juce::MidiMessageSequence seq;
        seq.addEvent(timestamped(juce::MidiMessage::textMetaEvent(3, "Bass"), 0));
        seq.addEvent(timestamped(juce::MidiMessage::noteOn(2, 36, (juce::uint8)110), 240)); // 0.5 beat
        seq.addEvent(timestamped(juce::MidiMessage::noteOff(2, 36), 960));                  // 2.0 beats
        midiFile.addTrack(seq);
    }
    {
        juce::MidiMessageSequence seq; // deliberately empty
        midiFile.addTrack(seq);
    }

    juce::MemoryOutputStream out;
    ASSERT_TRUE(midiFile.writeTo(out, 1));
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);

    const auto result = MidiClipFile::importFromStream(in);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.tracks.size(), 2u); // the empty track is omitted

    EXPECT_EQ(result.tracks[0].name, "Lead");
    ASSERT_EQ(result.tracks[0].notes.size(), 1u);
    EXPECT_NEAR(result.tracks[0].notes[0].startBeat, 0.0, 1e-9);
    EXPECT_NEAR(result.tracks[0].notes[0].lengthBeats, 1.0, 1e-9);
    EXPECT_EQ(result.tracks[0].notes[0].pitch, 60);
    EXPECT_EQ(result.tracks[0].notes[0].channel, 1);

    EXPECT_EQ(result.tracks[1].name, "Bass");
    ASSERT_EQ(result.tracks[1].notes.size(), 1u);
    EXPECT_NEAR(result.tracks[1].notes[0].startBeat, 0.5, 1e-9);
    EXPECT_NEAR(result.tracks[1].notes[0].lengthBeats, 1.5, 1e-9);
    EXPECT_EQ(result.tracks[1].notes[0].pitch, 36);
    EXPECT_EQ(result.tracks[1].notes[0].channel, 2);
}

// ============================================================================
// 3. VelocityZeroNoteOnIsNoteOff
// ============================================================================

TEST(MidiClipFileTest, VelocityZeroNoteOnIsNoteOff) {
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);

    juce::MidiMessageSequence seq;
    seq.addEvent(timestamped(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0));
    // A note-on with velocity 0 acting as the note-off, per SMF convention.
    seq.addEvent(timestamped(juce::MidiMessage::noteOn(1, 60, (juce::uint8)0), 480));
    midiFile.addTrack(seq);

    juce::MemoryOutputStream out;
    ASSERT_TRUE(midiFile.writeTo(out, 1));
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);

    const auto result = MidiClipFile::importFromStream(in);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.tracks.size(), 1u);
    ASSERT_EQ(result.tracks[0].notes.size(), 1u);

    const auto& note = result.tracks[0].notes[0];
    EXPECT_EQ(note.pitch, 60);
    EXPECT_EQ(note.velocity, 100); // the note-ON's velocity, not the closing zero
    EXPECT_NEAR(note.startBeat, 0.0, 1e-9);
    EXPECT_NEAR(note.lengthBeats, 0.5, 1e-9);
}

// ============================================================================
// 4. DanglingNoteOnGetsMinLength
// ============================================================================

TEST(MidiClipFileTest, DanglingNoteOnGetsMinLength) {
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);

    juce::MidiMessageSequence seq;
    seq.addEvent(timestamped(juce::MidiMessage::noteOn(1, 64, (juce::uint8)90), 0));
    // No matching note-off, ever.
    midiFile.addTrack(seq);

    juce::MemoryOutputStream out;
    ASSERT_TRUE(midiFile.writeTo(out, 1));
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);

    const auto result = MidiClipFile::importFromStream(in);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.tracks.size(), 1u);
    ASSERT_EQ(result.tracks[0].notes.size(), 1u);
    EXPECT_NEAR(result.tracks[0].notes[0].lengthBeats, MidiClipFile::kMinNoteLengthBeats, 1e-9);
    EXPECT_EQ(result.tracks[0].notes[0].velocity, 90);
}

// ============================================================================
// 5. SmpteTimeFormatRejected
// ============================================================================

TEST(MidiClipFileTest, SmpteTimeFormatRejected) {
    juce::MidiFile midiFile;
    midiFile.setSmpteTimeFormat(25, 40);

    juce::MidiMessageSequence seq;
    seq.addEvent(timestamped(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0));
    seq.addEvent(timestamped(juce::MidiMessage::noteOff(1, 60), 100));
    midiFile.addTrack(seq);

    juce::MemoryOutputStream out;
    ASSERT_TRUE(midiFile.writeTo(out, 1));
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);

    const auto result = MidiClipFile::importFromStream(in);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("SMPTE")) << result.message;
    EXPECT_TRUE(result.tracks.empty());
}

// ============================================================================
// 6. TempoAndMetaEventsIgnored
// ============================================================================

TEST(MidiClipFileTest, TempoAndMetaEventsIgnored) {
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);

    juce::MidiMessageSequence seq;
    seq.addEvent(timestamped(juce::MidiMessage::tempoMetaEvent(500000), 0));       // 120 BPM
    seq.addEvent(timestamped(juce::MidiMessage::timeSignatureMetaEvent(3, 4), 0)); // 3/4
    seq.addEvent(timestamped(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 0));
    // A tempo change mid-note must have zero effect on beat math — ticks/PPQ only.
    seq.addEvent(timestamped(juce::MidiMessage::tempoMetaEvent(300000), 960));
    seq.addEvent(timestamped(juce::MidiMessage::noteOff(1, 67), 1920)); // 2 beats
    midiFile.addTrack(seq);

    juce::MemoryOutputStream out;
    ASSERT_TRUE(midiFile.writeTo(out, 1));
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);

    const auto result = MidiClipFile::importFromStream(in);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.tracks.size(), 1u);
    ASSERT_EQ(result.tracks[0].notes.size(), 1u);
    EXPECT_NEAR(result.tracks[0].notes[0].startBeat, 0.0, 1e-9);
    EXPECT_NEAR(result.tracks[0].notes[0].lengthBeats, 2.0, 1e-9);
    EXPECT_EQ(result.tracks[0].notes[0].pitch, 67);
}

// ============================================================================
// 7. GarbageBytesRejected
// ============================================================================

TEST(MidiClipFileTest, GarbageBytesRejected) {
    std::vector<std::uint8_t> garbage(256);
    for (size_t i = 0; i < garbage.size(); ++i)
        garbage[i] = (std::uint8_t)((i * 37 + 11) % 256);

    juce::MemoryInputStream in(garbage.data(), garbage.size(), false);
    MidiClipFile::ImportResult result;
    EXPECT_NO_THROW(result = MidiClipFile::importFromStream(in));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.tracks.empty());
}

// ============================================================================
// 8. OverCapRejectedWhole
// ============================================================================

TEST(MidiClipFileTest, OverCapRejectedWhole) {
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);

    juce::MidiMessageSequence seq;
    const int total = TimelineDoc::kMaxNotesPerClip + 1;
    for (int i = 0; i < total; ++i) {
        seq.addEvent(timestamped(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), i * 20.0));
        seq.addEvent(timestamped(juce::MidiMessage::noteOff(1, 60), i * 20.0 + 10.0));
    }
    midiFile.addTrack(seq);

    juce::MemoryOutputStream out;
    ASSERT_TRUE(midiFile.writeTo(out, 1));
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);

    const auto result = MidiClipFile::importFromStream(in);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.tracks.empty());
}

// ============================================================================
// 9. ImportIntoTrackCreatesClips
// ============================================================================

TEST(MidiClipFileTest, ImportIntoTrackCreatesClips) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");

    MidiClipFile::ImportResult result;
    result.ok = true;

    MidiClipFile::ImportedTrack a;
    a.name = "A";
    a.notes.push_back(makeNote(0.0, 1.0, 60, 100, 1));
    a.notes.push_back(makeNote(2.0, 1.0, 64, 90, 1));
    result.tracks.push_back(a);

    MidiClipFile::ImportedTrack b;
    b.name = "B";
    b.notes.push_back(makeNote(0.5, 0.5, 50, 80, 2));
    result.tracks.push_back(b);

    ASSERT_TRUE(MidiClipFile::importIntoTrack(doc, track, 4.0, result));

    const auto* trackPtr = doc.getTrack(track);
    ASSERT_NE(trackPtr, nullptr);
    ASSERT_EQ(trackPtr->clips.size(), 2u);
    EXPECT_DOUBLE_EQ(trackPtr->clips[0].startBeat, 4.0); // both clips stacked at the same start
    EXPECT_DOUBLE_EQ(trackPtr->clips[1].startBeat, 4.0);

    const auto& clipA = trackPtr->clips[0].name == "A" ? trackPtr->clips[0] : trackPtr->clips[1];
    const auto& clipB = trackPtr->clips[0].name == "A" ? trackPtr->clips[1] : trackPtr->clips[0];

    EXPECT_DOUBLE_EQ(clipA.lengthBeats, 3.0); // ceil(2.0 + 1.0)
    EXPECT_DOUBLE_EQ(clipB.lengthBeats, 1.0); // ceil(0.5 + 0.5), already at the 1-beat minimum

    ASSERT_EQ(clipA.notes.size(), 2u);
    EXPECT_DOUBLE_EQ(clipA.notes[0].startBeat, 0.0); // clip-relative, unchanged from the import
    EXPECT_EQ(clipA.notes[0].pitch, 60);
    EXPECT_DOUBLE_EQ(clipA.notes[1].startBeat, 2.0);
    EXPECT_EQ(clipA.notes[1].pitch, 64);

    ASSERT_EQ(clipB.notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clipB.notes[0].startBeat, 0.5);
    EXPECT_EQ(clipB.notes[0].pitch, 50);

    // A doc already at its clip cap: all-or-nothing rejects the whole import, doc unchanged.
    TimelineDoc fullDoc;
    const auto fullTrack = fullDoc.addTrack(TrackKind::Midi, "Full");
    for (int i = 0; i < TimelineDoc::kMaxClipsPerTrack; ++i)
        ASSERT_TRUE(fullDoc.addClip(fullTrack, (double)i, 1.0, "c").isValid());
    const auto revisionBefore = fullDoc.getRevision();

    MidiClipFile::ImportResult oneMore;
    oneMore.ok = true;
    MidiClipFile::ImportedTrack single;
    single.notes.push_back(makeNote(0.0, 1.0, 60, 100, 1));
    oneMore.tracks.push_back(single);

    EXPECT_FALSE(MidiClipFile::importIntoTrack(fullDoc, fullTrack, 0.0, oneMore));
    EXPECT_EQ(fullDoc.getTrack(fullTrack)->clips.size(), (size_t)TimelineDoc::kMaxClipsPerTrack);
    EXPECT_EQ(fullDoc.getRevision(), revisionBefore);
}

// ============================================================================
// 10. ExportUnknownClipFails
// ============================================================================

TEST(MidiClipFileTest, ExportUnknownClipFails) {
    TimelineDoc doc;
    juce::MemoryOutputStream out;
    EXPECT_FALSE(MidiClipFile::exportClip(doc, ClipId{999}, out));

    const auto tempFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MidiClipFileTests_unknown_clip.mid");
    tempFile.deleteFile();
    EXPECT_FALSE(MidiClipFile::exportClipToFile(doc, ClipId{999}, tempFile));
    EXPECT_FALSE(tempFile.existsAsFile());
    tempFile.deleteFile();
}
