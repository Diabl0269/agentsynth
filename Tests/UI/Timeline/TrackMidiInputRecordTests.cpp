// Recording from one MIDI input, end to end through MainComponent: the record button picks the take's targets from
// the armed tracks and their inputs, notes arrive through the engine's real MIDI entry point and device callback,
// and each track commits only what came from its own input (docs/timeline/transport.md#recording-from-one-input).

#include "../../FakeAudioIODevice.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTransportBar.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

namespace {

class MockProviderMidiInput : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMidiInput"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

// One device callback at a time, by hand, as a real device would make them.
struct DeviceDriver {
    AudioEngine& engine;
    std::vector<float> inL, inR, outL, outR;
    explicit DeviceDriver(AudioEngine& e)
        : engine(e)
        , inL(synth::test::kFakeDeviceBlockSize)
        , inR(synth::test::kFakeDeviceBlockSize)
        , outL(synth::test::kFakeDeviceBlockSize)
        , outR(synth::test::kFakeDeviceBlockSize) {}
    void blocks(int count) {
        for (int i = 0; i < count; ++i) {
            const float* inputs[] = {inL.data(), inR.data()};
            float* outputs[] = {outL.data(), outR.data()};
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, synth::test::kFakeDeviceBlockSize, {});
        }
    }
};

juce::MidiMessage stamped(juce::MidiMessage message) {
    message.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    return message;
}

std::vector<int> pitchesOn(const synth::TimelineDoc& doc, synth::TrackId track) {
    std::vector<int> pitches;
    for (const auto& clip : doc.getTrack(track)->clips)
        for (const auto& note : clip.notes)
            pitches.push_back(note.pitch);
    std::sort(pitches.begin(), pitches.end());
    return pitches;
}

} // namespace

// Two controllers, each armed into its own track, plus a track left on "All MIDI inputs": the two get only their own
// controller, the all-inputs track (first armed, the old rule) gets both, and the whole take is one undo step. Run
// with and without automation slicing, which renders one device callback as several passes.
TEST(TrackMidiInputRecordTest, TwoTracksOnTwoControllersEachRecordOnlyTheirOwn) {
    for (const bool sliced : {false, true}) {
        SCOPED_TRACE(sliced ? "sliced" : "one pass");
        MainComponent mc(std::make_unique<MockProviderMidiInput>());
        mc.setSize(1600, 900);
        auto& engine = mc.getAudioEngine();
        engine.suspendDeviceCallback();
        synth::test::FakeAudioIODevice fake(2, 2, 0, 0);
        engine.audioDeviceAboutToStart(&fake);
        engine.setAutomationSlicingEnabled(sliced);
        DeviceDriver driver(engine);

        auto& doc = mc.getTimelineDoc();
        const auto all = doc.addTrack(synth::TrackKind::Midi, "Everything");
        const auto keys = doc.addTrack(synth::TrackKind::Midi, "Keys");
        const auto pads = doc.addTrack(synth::TrackKind::Midi, "Pads");
        ASSERT_TRUE(doc.setTrackMidiInput(keys, "dev-keys", "Keystation 49", 0));
        ASSERT_TRUE(doc.setTrackMidiInput(pads, "dev-pads", "Pad Controller", 0));
        for (const auto id : {all, keys, pads})
            ASSERT_TRUE(doc.setTrackArmed(id, true));

        auto& bar = mc.getTimelinePanel().getTransportBar();
        bar.getRecordButton().onClick();
        ASSERT_TRUE(mc.getMidiRecorderForTest().isRecording());
        driver.blocks(2); // the posted play() takes effect

        engine.handleIncomingMidiMessageFromSource("dev-keys",
                                                   stamped(juce::MidiMessage::noteOn(1, 60, (juce::uint8)99)));
        engine.handleIncomingMidiMessageFromSource("dev-pads",
                                                   stamped(juce::MidiMessage::noteOn(1, 72, (juce::uint8)99)));
        driver.blocks(4);
        engine.handleIncomingMidiMessageFromSource("dev-keys", stamped(juce::MidiMessage::noteOff(1, 60)));
        engine.handleIncomingMidiMessageFromSource("dev-pads", stamped(juce::MidiMessage::noteOff(1, 72)));
        driver.blocks(2);

        bar.getRecordButton().onClick(); // record off: commits
        EXPECT_FALSE(mc.getMidiRecorderForTest().isRecording());
        EXPECT_EQ(pitchesOn(doc, keys), (std::vector<int>{60}));
        EXPECT_EQ(pitchesOn(doc, pads), (std::vector<int>{72}));
        EXPECT_EQ(pitchesOn(doc, all), (std::vector<int>{60, 72}));

        ASSERT_TRUE(mc.getUndoManager().undo());
        for (const auto id : {all, keys, pads})
            EXPECT_TRUE(doc.getTrack(id)->clips.empty());
    }
}

// A track whose controller is not connected still takes part in the take but records nothing; it does not fall back
// to every input, and it does not stop the all-inputs track recording.
TEST(TrackMidiInputRecordTest, ATrackOnAnUnpluggedControllerRecordsNothing) {
    MainComponent mc(std::make_unique<MockProviderMidiInput>());
    mc.setSize(1600, 900);
    auto& engine = mc.getAudioEngine();
    engine.suspendDeviceCallback();
    synth::test::FakeAudioIODevice fake(2, 2, 0, 0);
    engine.audioDeviceAboutToStart(&fake);
    DeviceDriver driver(engine);

    auto& doc = mc.getTimelineDoc();
    const auto gone = doc.addTrack(synth::TrackKind::Midi, "Old keys");
    const auto all = doc.addTrack(synth::TrackKind::Midi, "Everything");
    ASSERT_TRUE(doc.setTrackMidiInput(gone, "dev-unplugged", "Old Keyboard", 0));
    ASSERT_TRUE(doc.setTrackArmed(gone, true));
    ASSERT_TRUE(doc.setTrackArmed(all, true));

    auto& bar = mc.getTimelinePanel().getTransportBar();
    bar.getRecordButton().onClick();
    driver.blocks(2);
    engine.handleIncomingMidiMessageFromSource("dev-other", stamped(juce::MidiMessage::noteOn(1, 64, (juce::uint8)80)));
    driver.blocks(2);
    engine.handleIncomingMidiMessageFromSource("dev-other", stamped(juce::MidiMessage::noteOff(1, 64)));
    driver.blocks(2);
    bar.getRecordButton().onClick();

    EXPECT_TRUE(doc.getTrack(gone)->clips.empty());
    EXPECT_EQ(pitchesOn(doc, all), (std::vector<int>{64}));
}

// A channel-only input ("All MIDI inputs", channel 10) splits one controller's stream by channel.
TEST(TrackMidiInputRecordTest, AChannelOnlyInputRecordsThatChannelFromEveryInput) {
    MainComponent mc(std::make_unique<MockProviderMidiInput>());
    mc.setSize(1600, 900);
    auto& engine = mc.getAudioEngine();
    engine.suspendDeviceCallback();
    synth::test::FakeAudioIODevice fake(2, 2, 0, 0);
    engine.audioDeviceAboutToStart(&fake);
    DeviceDriver driver(engine);

    auto& doc = mc.getTimelineDoc();
    const auto drums = doc.addTrack(synth::TrackKind::Midi, "Drums");
    ASSERT_TRUE(doc.setTrackMidiInput(drums, {}, {}, 10));
    ASSERT_TRUE(doc.setTrackArmed(drums, true));

    auto& bar = mc.getTimelinePanel().getTransportBar();
    bar.getRecordButton().onClick();
    driver.blocks(2);
    engine.handleIncomingMidiMessageFromSource("dev-a", stamped(juce::MidiMessage::noteOn(10, 36, (juce::uint8)80)));
    engine.handleIncomingMidiMessageFromSource("dev-a", stamped(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80)));
    driver.blocks(2);
    engine.handleIncomingMidiMessageFromSource("dev-a", stamped(juce::MidiMessage::noteOff(10, 36)));
    engine.handleIncomingMidiMessageFromSource("dev-a", stamped(juce::MidiMessage::noteOff(1, 60)));
    driver.blocks(2);
    bar.getRecordButton().onClick();

    EXPECT_EQ(pitchesOn(doc, drums), (std::vector<int>{36}));
}
