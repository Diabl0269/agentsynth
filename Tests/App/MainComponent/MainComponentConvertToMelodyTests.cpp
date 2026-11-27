// Concern: "Convert to melody" in the app: a recorded hum on an audio clip becomes a new instrument track with a MIDI
// clip of its notes, in one undo step, after an analysis that runs off the message thread.
#include "../../TestWait.h"
#include "../../Timeline/HumSynth.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponentConvertToMelody.h"
#include "MainComponentTestFixture.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

void writeHumWav(const juce::File& file, const std::vector<float>& samples, double sampleRate) {
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> out(file.createOutputStream());
    ASSERT_NE(out, nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(out.get(), sampleRate, 1, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    out.release();
    juce::AudioBuffer<float> buffer(1, (int)samples.size());
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, samples[(size_t)i]);
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

// A saved project with one audio track holding a clip over `samples`.
synth::ClipId addHumClip(MainComponent& mc, const juce::File& bundleDir, const std::vector<float>& samples) {
    EXPECT_TRUE(mc.saveProjectForTest(bundleDir));
    auto& doc = mc.getTimelineDoc();
    const auto track = doc.addTrack(synth::TrackKind::Audio, "Hum");
    const auto clip = doc.addClip(track, 0.0, 8.0, "Hum take");
    writeHumWav(bundleDir.getChildFile("Audio/hum.wav"), samples, 22050.0);
    EXPECT_TRUE(doc.setClipAsset(clip, "Audio/hum.wav", 0.0));
    EXPECT_TRUE(mc.saveProjectForTest(bundleDir));
    return clip;
}

} // namespace

TEST_F(MainComponentTest, ConvertToMelodyAddsOneInstrumentTrackWithTheNotesInOneUndoStep) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    humsynth::Spec spec;
    spec.notes = {60, 62, 64, 65, 67, 65, 64, 62};
    const auto clip = addHumClip(mc, tempRoot.getChildFile("Hum.agsproj"), humsynth::render(spec));
    auto& doc = mc.getTimelineDoc();
    ASSERT_FALSE(mc.getAudioEngine().getTransport().getDocumentState().songKey.has_value());
    mc.getUndoManager().clearUndoHistory();
    // The hum's notes are 0.8 beat long; a one-beat grid would merge some of them.
    mc.getTimelinePanel().getViewState().snap = synth::ui::TimelineViewState::Snap::Sixteenth;

    MainComponentConvertToMelody::request(mc, clip, /*snapToKey=*/true);
    synth::test::waitUntil([&] { return doc.getTracks().size() == 2; }, 60000);
    ASSERT_EQ(doc.getTracks().size(), 2u);

    const auto& track = doc.getTracks().back();
    EXPECT_EQ(track.kind, synth::TrackKind::Midi);
    EXPECT_EQ(track.name, juce::String("Hum take melody"));
    ASSERT_EQ(track.clips.size(), 1u);
    const auto& melody = track.clips[0];
    EXPECT_DOUBLE_EQ(melody.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(melody.lengthBeats, 8.0);
    std::vector<int> pitches;
    for (const auto& note : melody.notes)
        pitches.push_back(note.pitch);
    EXPECT_EQ(pitches, (std::vector<int>{60, 62, 64, 65, 67, 65, 64, 62}));
    EXPECT_TRUE(melody.scale.has_value()) << "no song key, so the clip carries the key found in the hum";
    EXPECT_TRUE(mc.getTimelinePanel().getClipSelection().contains(melody.id));

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    mc.getUndoManager().undo();
    EXPECT_EQ(doc.getTracks().size(), 1u) << "one undo removes the track and its clip";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the conversion was a single undo step";
}

TEST_F(MainComponentTest, ConvertToMelodyWithNoNotesChangesNothing) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    const auto clip = addHumClip(mc, tempRoot.getChildFile("Silent.agsproj"), std::vector<float>(44100, 0.0f));
    mc.getUndoManager().clearUndoHistory();

    MainComponentConvertToMelody::request(mc, clip, false);
    // The analysis finds nothing; wait for the result to have been delivered (the status line changes).
    synth::test::waitUntil([&] { return mc.getStatusBar().getAccessibilityText() == "No melody found in this clip"; },
                           60000);
    EXPECT_EQ(mc.getStatusBar().getAccessibilityText(), "No melody found in this clip");
    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

TEST_F(MainComponentTest, ConvertToMelodyIgnoresAClipDeletedDuringTheAnalysis) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    const auto clip = addHumClip(mc, tempRoot.getChildFile("Gone.agsproj"), std::vector<float>(22050, 0.1f));
    ASSERT_TRUE(mc.getTimelineDoc().removeClip(clip));
    mc.getUndoManager().clearUndoHistory();

    const synth::melody::MelodyNote note{0.0, 1.0, 60, 100};
    const auto made = MainComponentConvertToMelody::apply(mc, clip, {note}, std::nullopt);
    EXPECT_FALSE(made.isValid());
    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}
