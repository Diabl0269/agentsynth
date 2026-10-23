// TimelineDoc track audio input tests: the setter's rules, one undo step, duplication, and the project-file round
// trip including files written before the field existed and values that must fail the load.

#include "AppUndoManager.h"
#include "TimelineDocTestHelpers.h"

namespace {

synth::AudioInputChoice deviceInput(int firstChannel, int numChannels, const juce::String& device = "Scarlett") {
    synth::AudioInputChoice choice;
    choice.kind = synth::AudioInputChoice::Kind::Device;
    choice.firstChannel = firstChannel;
    choice.numChannels = numChannels;
    choice.deviceName = device;
    return choice;
}

synth::AudioInputChoice wholeMix() {
    synth::AudioInputChoice choice;
    choice.kind = synth::AudioInputChoice::Kind::WholeMix;
    return choice;
}

} // namespace

TEST_F(TimelineDocTest, NewAudioTrackHasNoInput) {
    const auto id = doc.addTrack(TrackKind::Audio, "Vox");
    EXPECT_EQ(doc.getTrack(id)->audioInput.kind, synth::AudioInputChoice::Kind::None);
}

TEST_F(TimelineDocTest, SetTrackAudioInputStoresTheChoiceAndNotifiesOnlyOnAChange) {
    const auto id = doc.addTrack(TrackKind::Audio, "Vox");
    const auto before = doc.getRevision();
    EXPECT_TRUE(doc.setTrackAudioInput(id, {}));
    EXPECT_EQ(doc.getRevision(), before) << "the stored value again is not a change";

    EXPECT_TRUE(doc.setTrackAudioInput(id, deviceInput(2, 2)));
    EXPECT_EQ(doc.getTrack(id)->audioInput, deviceInput(2, 2));
    const auto afterSet = doc.getRevision();
    EXPECT_GT(afterSet, before);
    EXPECT_TRUE(doc.setTrackAudioInput(id, deviceInput(2, 2)));
    EXPECT_EQ(doc.getRevision(), afterSet);

    // Whole mix carries no channels or device: whatever the caller passed, it is stored in one form.
    auto messyMix = deviceInput(5, 2);
    messyMix.kind = synth::AudioInputChoice::Kind::WholeMix;
    EXPECT_TRUE(doc.setTrackAudioInput(id, messyMix));
    EXPECT_EQ(doc.getTrack(id)->audioInput, wholeMix());
}

TEST_F(TimelineDocTest, SetTrackAudioInputRefusesBadChannelsAndTracksThatCannotRecordAudio) {
    const auto audio = doc.addTrack(TrackKind::Audio, "Vox");
    const auto midi = doc.addTrack(TrackKind::Midi, "Keys");
    EXPECT_FALSE(doc.setTrackAudioInput(midi, wholeMix()));
    EXPECT_FALSE(doc.setTrackAudioInput(audio, deviceInput(-1, 1)));
    EXPECT_FALSE(doc.setTrackAudioInput(audio, deviceInput(0, 3)));
    EXPECT_FALSE(doc.setTrackAudioInput(audio, deviceInput(7, 2))) << "In 8 + 9 is past the last input";
    EXPECT_FALSE(doc.setTrackAudioInput(synth::TrackId{999}, wholeMix()));
    EXPECT_TRUE(doc.setTrackAudioInput(audio, deviceInput(7, 1)));
    EXPECT_TRUE(doc.setTrackAudioInput(audio, deviceInput(6, 2)));
}

TEST_F(TimelineDocTest, SetTrackAudioInputIsOneUndoStep) {
    AppUndoManager undo;
    const auto id = doc.addTrack(TrackKind::Audio, "Vox");
    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { doc.setTrackAudioInput(id, deviceInput(1, 1)); }));
    EXPECT_EQ(doc.getTrack(id)->audioInput, deviceInput(1, 1));
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(doc.getTrack(id)->audioInput.kind, synth::AudioInputChoice::Kind::None);
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(doc.getTrack(id)->audioInput, deviceInput(1, 1));
}

TEST_F(TimelineDocTest, DuplicateTrackKeepsTheAudioInput) {
    const auto id = doc.addTrack(TrackKind::Audio, "Vox");
    ASSERT_TRUE(doc.setTrackAudioInput(id, deviceInput(0, 2)));
    const auto copy = doc.duplicateTrack(id, "Vox 2", 0xff00ff00, {});
    ASSERT_TRUE(copy.isValid());
    EXPECT_EQ(doc.getTrack(copy)->audioInput, deviceInput(0, 2));
}

TEST_F(TimelineDocTest, AudioInputSurvivesTheProjectFileRoundTrip) {
    const auto vox = doc.addTrack(TrackKind::Audio, "Vox");
    const auto mix = doc.addTrack(TrackKind::Audio, "Mix");
    ASSERT_TRUE(doc.setTrackAudioInput(vox, deviceInput(2, 2, "Scarlett 4i4 USB")));
    ASSERT_TRUE(doc.setTrackAudioInput(mix, wholeMix()));

    const auto text = juce::JSON::toString(doc.toVar());
    EXPECT_TRUE(text.contains("\"channel\": 3")) << "the file names inputs 1-based: " << text;

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(text)));
    EXPECT_EQ(loaded.getTrack(vox)->audioInput, deviceInput(2, 2, "Scarlett 4i4 USB"));
    EXPECT_EQ(loaded.getTrack(mix)->audioInput, wholeMix());
    EXPECT_EQ(dump(loaded), dump(doc));
}

TEST_F(TimelineDocTest, NoAudioInputWritesNoKeyAndAnOlderFileLoadsAsNoInput) {
    doc.addTrack(TrackKind::Audio, "Vox");
    EXPECT_FALSE(juce::JSON::toString(doc.toVar()).contains("audioInput")) << "the default is left out";

    const juce::String old = R"({"version":1,"tracks":[{"id":1,"kind":1,"name":"Vox","bindingUuid":""}]})";
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(old)));
    EXPECT_EQ(loaded.getTracks().front().audioInput.kind, synth::AudioInputChoice::Kind::None);
}

TEST_F(TimelineDocTest, FromVarRejectsAMistypedOrOutOfRangeAudioInput) {
    for (const juce::String input :
         {R"({"kind":"device","channel":0})", R"({"kind":"device","channel":9})",
          R"({"kind":"device","channel":8,"channels":2})", R"({"kind":"device","channel":1,"channels":3})",
          R"({"kind":"device"})", R"({"kind":"device","channel":"1"})", R"({"kind":"device","channel":1,"device":4})",
          R"({"kind":"aux"})", R"({"channel":1})", R"("mix")"}) {
        TimelineDoc loaded;
        EXPECT_FALSE(loaded.fromVar(
            juce::JSON::parse(R"({"version":1,"tracks":[{"id":1,"kind":1,"audioInput":)" + input + "}]}")))
            << input;
    }
    TimelineDoc loaded;
    EXPECT_TRUE(loaded.fromVar(
        juce::JSON::parse(R"({"version":1,"tracks":[{"id":1,"kind":1,"audioInput":{"kind":"device","channel":8}}]})")))
        << "In 8 mono is the last valid input";
}
