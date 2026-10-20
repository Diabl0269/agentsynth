// TimelineDoc track MIDI input tests: the setter's rules and notifications, one undo step, duplication, and the
// project-file round trip including files written before the field existed.

#include "AppUndoManager.h"
#include "TimelineDocTestHelpers.h"

TEST_F(TimelineDocTest, NewMidiTrackRecordsEveryInputOnAnyChannel) {
    const auto id = doc.addTrack(TrackKind::Midi, "Keys");
    const auto* track = doc.getTrack(id);
    ASSERT_NE(track, nullptr);
    EXPECT_TRUE(track->midiInputId.isEmpty());
    EXPECT_TRUE(track->midiInputName.isEmpty());
    EXPECT_EQ(track->midiInputChannel, 0);
    EXPECT_TRUE(track->recordsEveryMidiInput());
}

TEST_F(TimelineDocTest, SetTrackMidiInputStoresDeviceAndChannelAndNotifiesOnlyOnAChange) {
    const auto id = doc.addTrack(TrackKind::Midi, "Keys");
    CountingListener counting;
    doc.addListener(&counting);
    const auto revision = doc.getRevision();

    EXPECT_TRUE(doc.setTrackMidiInput(id, {}, {}, 0));
    EXPECT_EQ(counting.calls, 0) << "the defaults already stored: no notification";
    EXPECT_EQ(doc.getRevision(), revision);

    EXPECT_TRUE(doc.setTrackMidiInput(id, "dev-a", "Keystation 49", 3));
    EXPECT_EQ(counting.calls, 1);
    EXPECT_EQ(doc.getRevision(), revision + 1);
    const auto* track = doc.getTrack(id);
    EXPECT_EQ(track->midiInputId, "dev-a");
    EXPECT_EQ(track->midiInputName, "Keystation 49");
    EXPECT_EQ(track->midiInputChannel, 3);
    EXPECT_FALSE(track->recordsEveryMidiInput());

    EXPECT_TRUE(doc.setTrackMidiInput(id, "dev-a", "Keystation 49", 3));
    EXPECT_EQ(counting.calls, 1) << "the same input again: no notification";

    // Back to all inputs: the device name goes with the device.
    EXPECT_TRUE(doc.setTrackMidiInput(id, {}, "Keystation 49", 0));
    EXPECT_TRUE(doc.getTrack(id)->midiInputName.isEmpty());
    EXPECT_TRUE(doc.getTrack(id)->recordsEveryMidiInput());
    doc.removeListener(&counting);
}

TEST_F(TimelineDocTest, SetTrackMidiInputRefusesBadChannelsAndTracksThatCannotRecordMidi) {
    const auto midi = doc.addTrack(TrackKind::Midi, "Keys");
    const auto audio = doc.addTrack(TrackKind::Audio, "Vox");
    const auto revision = doc.getRevision();
    EXPECT_FALSE(doc.setTrackMidiInput(midi, {}, {}, -1));
    EXPECT_FALSE(doc.setTrackMidiInput(midi, {}, {}, Track::kMaxMidiInputChannel + 1));
    EXPECT_FALSE(doc.setTrackMidiInput(audio, "dev-a", "Pads", 1));
    EXPECT_FALSE(doc.setTrackMidiInput(TrackId{9999}, "dev-a", "Pads", 1));
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_TRUE(doc.setTrackMidiInput(midi, {}, {}, Track::kMaxMidiInputChannel));
}

TEST_F(TimelineDocTest, SetTrackMidiInputIsOneUndoStep) {
    AppUndoManager undo;
    const auto id = doc.addTrack(TrackKind::Midi, "Keys");
    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { doc.setTrackMidiInput(id, "dev-b", "Pads", 10); }));
    EXPECT_EQ(doc.getTrack(id)->midiInputId, "dev-b");
    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(doc.getTrack(id)->recordsEveryMidiInput());
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(doc.getTrack(id)->midiInputChannel, 10);
    EXPECT_EQ(doc.getTrack(id)->midiInputName, "Pads");
}

TEST_F(TimelineDocTest, DuplicateTrackKeepsTheMidiInput) {
    const auto id = doc.addTrack(TrackKind::Midi, "Keys");
    ASSERT_TRUE(doc.setTrackMidiInput(id, "dev-a", "Keystation 49", 2));
    const auto copy = doc.duplicateTrack(id, "Keys 2", 0xff00ff00, {});
    ASSERT_TRUE(copy.isValid());
    const auto* track = doc.getTrack(copy);
    EXPECT_EQ(track->midiInputId, "dev-a");
    EXPECT_EQ(track->midiInputName, "Keystation 49");
    EXPECT_EQ(track->midiInputChannel, 2);
}

TEST_F(TimelineDocTest, MidiInputSurvivesTheProjectFileRoundTrip) {
    const auto id = doc.addTrack(TrackKind::Midi, "Keys");
    ASSERT_TRUE(doc.setTrackMidiInput(id, "dev-a", "Keystation 49", 16));
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(juce::JSON::toString(doc.toVar()))));
    const auto& track = loaded.getTracks().front();
    EXPECT_EQ(track.midiInputId, "dev-a");
    EXPECT_EQ(track.midiInputName, "Keystation 49");
    EXPECT_EQ(track.midiInputChannel, 16);
    EXPECT_EQ(dump(loaded), dump(doc));
}

TEST_F(TimelineDocTest, DefaultMidiInputWritesNoKeysAndAnOlderFileLoadsAsDefault) {
    doc.addTrack(TrackKind::Midi, "Keys");
    const auto text = juce::JSON::toString(doc.toVar());
    EXPECT_FALSE(text.contains("midiInput")) << "older files and fixtures stay byte-identical";

    // A file from before the field existed: no input keys at all.
    const juce::String old = R"({"version":1,"tracks":[{"id":1,"kind":0,"name":"Keys","bindingUuid":""}]})";
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(old)));
    ASSERT_EQ(loaded.getTracks().size(), 1u);
    EXPECT_TRUE(loaded.getTracks().front().recordsEveryMidiInput());
}

TEST_F(TimelineDocTest, FromVarRejectsAMistypedOrOutOfRangeMidiInput) {
    for (const juce::String track :
         {R"({"id":1,"kind":0,"midiInputChannel":17})", R"({"id":1,"kind":0,"midiInputChannel":-1})",
          R"({"id":1,"kind":0,"midiInputChannel":"3"})", R"({"id":1,"kind":0,"midiInputId":7})",
          R"({"id":1,"kind":0,"midiInputName":false})"}) {
        TimelineDoc loaded;
        EXPECT_FALSE(loaded.fromVar(juce::JSON::parse(R"({"version":1,"tracks":[)" + track + "]}"))) << track;
    }
}
