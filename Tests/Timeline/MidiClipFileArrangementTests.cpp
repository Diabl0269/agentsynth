// Tests for MidiClipFile::exportArrangement — whole-arrangement Standard MIDI File export.
//
// Every test writes to a MemoryOutputStream and reads the bytes back through juce::MidiFile, so the
// assertions are on what another program would see, not on this class's own internals.

#include "Timeline/MidiClipFile.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

using synth::MidiClipFile;
using synth::MidiNote;
using synth::TimelineDoc;
using synth::TrackKind;

namespace {

constexpr double kPpq = MidiClipFile::kExportPpq;

MidiNote makeNote(double startBeat, double lengthBeats, int pitch, int velocity = 100, int channel = 1) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

// A note as it appears in the file: absolute ticks, both edges.
struct FileNote {
    double onTick;
    double offTick;
    int pitch;
    int velocity;
    int channel;
};

bool writeAndRead(const TimelineDoc& doc, const MidiClipFile::ArrangementExportOptions& options, juce::MidiFile& file) {
    juce::MemoryOutputStream out;
    if (!MidiClipFile::exportArrangement(doc, options, out))
        return false;
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    return file.readFrom(in);
}

std::vector<FileNote> notesOf(const juce::MidiMessageSequence& sequence) {
    std::vector<FileNote> notes;
    for (int i = 0; i < sequence.getNumEvents(); ++i) {
        const auto* on = sequence.getEventPointer(i);
        if (!on->message.isNoteOn())
            continue;
        const auto* off = on->noteOffObject;
        notes.push_back({on->message.getTimeStamp(), off != nullptr ? off->message.getTimeStamp() : -1.0,
                         on->message.getNoteNumber(), on->message.getVelocity(), on->message.getChannel()});
    }
    return notes;
}

juce::String trackName(const juce::MidiMessageSequence& sequence) {
    for (int i = 0; i < sequence.getNumEvents(); ++i)
        if (sequence.getEventPointer(i)->message.isTrackNameEvent())
            return sequence.getEventPointer(i)->message.getTextFromTextMetaEvent();
    return {};
}

// A doc with one MIDI track holding one clip at `clipStart`, with the given notes.
struct OneClipDoc {
    TimelineDoc doc;
    synth::TrackId track;
    synth::ClipId clip;

    explicit OneClipDoc(double clipStart, const std::vector<MidiNote>& notes, double clipLength = 16.0) {
        track = doc.addTrack(TrackKind::Midi, "Lead");
        clip = doc.addClip(track, clipStart, clipLength, "c");
        for (const auto& note : notes)
            doc.addNote(clip, note);
    }
};

} // namespace

TEST(MidiClipFileArrangementTest, NotesLandAtAbsolutePositionsIncludingClipOffset) {
    OneClipDoc fixture(4.0,
                       {makeNote(0.0, 1.0, 60, 90, 1), makeNote(1.5, 0.5, 64, 70, 3), makeNote(2.0, 2.0, 67, 127, 16)});

    juce::MidiFile file;
    ASSERT_TRUE(writeAndRead(fixture.doc, {}, file));
    EXPECT_EQ(file.getTimeFormat(), MidiClipFile::kExportPpq);
    ASSERT_EQ(file.getNumTracks(), 2);

    const auto notes = notesOf(*file.getTrack(1));
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_DOUBLE_EQ(notes[0].onTick, 4.0 * kPpq);
    EXPECT_DOUBLE_EQ(notes[0].offTick, 5.0 * kPpq);
    EXPECT_EQ(notes[0].pitch, 60);
    EXPECT_EQ(notes[0].velocity, 90);
    EXPECT_EQ(notes[0].channel, 1);
    EXPECT_DOUBLE_EQ(notes[1].onTick, 5.5 * kPpq);
    EXPECT_DOUBLE_EQ(notes[1].offTick, 6.0 * kPpq);
    EXPECT_EQ(notes[1].channel, 3);
    EXPECT_DOUBLE_EQ(notes[2].onTick, 6.0 * kPpq);
    EXPECT_DOUBLE_EQ(notes[2].offTick, 8.0 * kPpq);
    EXPECT_EQ(notes[2].velocity, 127);
    EXPECT_EQ(notes[2].channel, 16);
}

TEST(MidiClipFileArrangementTest, ConductorTrackCarriesTempoAndTimeSignature) {
    OneClipDoc fixture(0.0, {makeNote(0.0, 1.0, 60)});

    MidiClipFile::ArrangementExportOptions options;
    options.bpm = 90.0;
    options.timeSigNumerator = 7;
    options.timeSigDenominator = 8;
    juce::MidiFile file;
    ASSERT_TRUE(writeAndRead(fixture.doc, options, file));

    const auto& conductor = *file.getTrack(0);
    EXPECT_EQ(trackName(conductor), "Tempo");
    int tempoEvents = 0;
    int timeSigEvents = 0;
    for (int i = 0; i < conductor.getNumEvents(); ++i) {
        const auto& message = conductor.getEventPointer(i)->message;
        if (message.isTempoMetaEvent()) {
            ++tempoEvents;
            EXPECT_NEAR(message.getTempoSecondsPerQuarterNote(), 60.0 / 90.0, 1e-6);
            EXPECT_DOUBLE_EQ(message.getTimeStamp(), 0.0);
        } else if (message.isTimeSignatureMetaEvent()) {
            ++timeSigEvents;
            int numerator = 0;
            int denominator = 0;
            message.getTimeSignatureInfo(numerator, denominator);
            EXPECT_EQ(numerator, 7);
            EXPECT_EQ(denominator, 8);
            EXPECT_DOUBLE_EQ(message.getTimeStamp(), 0.0);
        }
    }
    EXPECT_EQ(tempoEvents, 1);
    EXPECT_EQ(timeSigEvents, 1);
}

TEST(MidiClipFileArrangementTest, OneNamedTrackPerMidiTrackAndAudioTracksAreSkipped) {
    TimelineDoc doc;
    const auto bass = doc.addTrack(TrackKind::Midi, "Bass");
    doc.addTrack(TrackKind::Audio, "Vocals");
    const auto keys = doc.addTrack(TrackKind::Midi, "Keys");
    doc.addNote(doc.addClip(bass, 0.0, 4.0, "b"), makeNote(0.0, 1.0, 36));
    doc.addNote(doc.addClip(keys, 2.0, 4.0, "k"), makeNote(0.0, 1.0, 72));

    juce::MidiFile file;
    ASSERT_TRUE(writeAndRead(doc, {}, file));
    ASSERT_EQ(file.getNumTracks(), 3); // conductor + two MIDI tracks

    EXPECT_EQ(trackName(*file.getTrack(1)), "Bass");
    EXPECT_EQ(trackName(*file.getTrack(2)), "Keys");
    const auto bassNotes = notesOf(*file.getTrack(1));
    const auto keysNotes = notesOf(*file.getTrack(2));
    ASSERT_EQ(bassNotes.size(), 1u);
    ASSERT_EQ(keysNotes.size(), 1u);
    EXPECT_EQ(bassNotes[0].pitch, 36);
    EXPECT_EQ(keysNotes[0].pitch, 72);
    EXPECT_DOUBLE_EQ(keysNotes[0].onTick, 2.0 * kPpq);
}

TEST(MidiClipFileArrangementTest, EmptyDocAndClipLessTrackStillProduceAReadableFile) {
    TimelineDoc empty;
    juce::MidiFile emptyFile;
    ASSERT_TRUE(writeAndRead(empty, {}, emptyFile));
    EXPECT_EQ(emptyFile.getNumTracks(), 1); // conductor only

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "Empty");
    juce::MidiFile file;
    ASSERT_TRUE(writeAndRead(doc, {}, file));
    ASSERT_EQ(file.getNumTracks(), 2);
    EXPECT_EQ(trackName(*file.getTrack(1)), "Empty");
    EXPECT_TRUE(notesOf(*file.getTrack(1)).empty());
}

TEST(MidiClipFileArrangementTest, MutedNotesClipsAndTracksAreOmitted) {
    TimelineDoc doc;
    const auto live = doc.addTrack(TrackKind::Midi, "Live");
    const auto mutedTrack = doc.addTrack(TrackKind::Midi, "MutedTrack");
    const auto liveClip = doc.addClip(live, 0.0, 8.0, "on");
    const auto mutedClip = doc.addClip(live, 8.0, 8.0, "off");
    const auto keptNote = doc.addNote(liveClip, makeNote(0.0, 1.0, 60));
    const auto mutedNote = doc.addNote(liveClip, makeNote(1.0, 1.0, 62));
    doc.addNote(mutedClip, makeNote(0.0, 1.0, 64));
    doc.addNote(doc.addClip(mutedTrack, 0.0, 4.0, "x"), makeNote(0.0, 1.0, 65));
    ASSERT_TRUE(keptNote.isValid());
    ASSERT_TRUE(doc.setNoteMuted(mutedNote, true));
    ASSERT_TRUE(doc.setClipMuted(mutedClip, true));
    ASSERT_TRUE(doc.setTrackMuted(mutedTrack, true));

    juce::MidiFile file;
    ASSERT_TRUE(writeAndRead(doc, {}, file));
    ASSERT_EQ(file.getNumTracks(), 3); // the muted track is still emitted, empty

    const auto notes = notesOf(*file.getTrack(1));
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].pitch, 60);
    EXPECT_TRUE(notesOf(*file.getTrack(2)).empty());
}

TEST(MidiClipFileArrangementTest, RangeTrimsBothBoundariesAndShiftsToZero) {
    // Range [4, 8). Notes: wholly before, crossing the start, inside, crossing the end, wholly after,
    // and one ending exactly at the range start (must be dropped, not emitted zero-length).
    OneClipDoc fixture(0.0, {makeNote(0.0, 1.0, 50), makeNote(3.0, 2.0, 51), makeNote(5.0, 1.0, 52),
                             makeNote(7.0, 3.0, 53), makeNote(9.0, 1.0, 54), makeNote(2.0, 2.0, 55)});

    MidiClipFile::ArrangementExportOptions options;
    options.rangeBeats = std::make_pair(4.0, 8.0);
    juce::MidiFile file;
    ASSERT_TRUE(writeAndRead(fixture.doc, options, file));

    const auto notes = notesOf(*file.getTrack(1));
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_EQ(notes[0].pitch, 51); // 3..5 cut to 4..5, shifted to 0..1
    EXPECT_DOUBLE_EQ(notes[0].onTick, 0.0);
    EXPECT_DOUBLE_EQ(notes[0].offTick, 1.0 * kPpq);
    EXPECT_EQ(notes[1].pitch, 52); // 5..6 shifted to 1..2
    EXPECT_DOUBLE_EQ(notes[1].onTick, 1.0 * kPpq);
    EXPECT_DOUBLE_EQ(notes[1].offTick, 2.0 * kPpq);
    EXPECT_EQ(notes[2].pitch, 53); // 7..10 cut to 7..8, shifted to 3..4
    EXPECT_DOUBLE_EQ(notes[2].onTick, 3.0 * kPpq);
    EXPECT_DOUBLE_EQ(notes[2].offTick, 4.0 * kPpq);

    EXPECT_LE(file.getTrack(1)->getEndTime(), 4.0 * kPpq);
}

TEST(MidiClipFileArrangementTest, InvalidRangeOrOptionsAreRejected) {
    OneClipDoc fixture(0.0, {makeNote(0.0, 1.0, 60)});
    juce::MemoryOutputStream out;

    MidiClipFile::ArrangementExportOptions options;
    options.rangeBeats = std::make_pair(4.0, 4.0);
    EXPECT_FALSE(MidiClipFile::exportArrangement(fixture.doc, options, out));
    options.rangeBeats = std::make_pair(8.0, 4.0);
    EXPECT_FALSE(MidiClipFile::exportArrangement(fixture.doc, options, out));

    options.rangeBeats.reset();
    options.bpm = 0.0;
    EXPECT_FALSE(MidiClipFile::exportArrangement(fixture.doc, options, out));
    options.bpm = std::nan("");
    EXPECT_FALSE(MidiClipFile::exportArrangement(fixture.doc, options, out));
}

TEST(MidiClipFileArrangementTest, ExportToFileWritesAReadableFileAndReplacesAnExistingOne) {
    OneClipDoc fixture(0.0, {makeNote(0.0, 1.0, 60)});
    juce::TemporaryFile temp(".mid");
    ASSERT_TRUE(temp.getFile().replaceWithText("stale"));

    ASSERT_TRUE(MidiClipFile::exportArrangementToFile(fixture.doc, {}, temp.getFile()));
    juce::MidiFile file;
    juce::FileInputStream in(temp.getFile());
    ASSERT_TRUE(in.openedOk());
    ASSERT_TRUE(file.readFrom(in));
    EXPECT_EQ(file.getNumTracks(), 2);

    MidiClipFile::ArrangementExportOptions bad;
    bad.rangeBeats = std::make_pair(2.0, 1.0);
    EXPECT_FALSE(MidiClipFile::exportArrangementToFile(fixture.doc, bad, temp.getFile()));
}
