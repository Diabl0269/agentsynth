// AudioEngine::setSilentAudioDevice: a Standalone engine whose audio thread is the silent device, so audio-fed state
// (voice count, meters, the transport snapshot, the metronome) behaves as with a real device and nothing is heard
// (docs/architecture/audio-engine.md#automation-launch-the-silent-device).

#include "../TestWait.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/SilentAudioDevice.h"
#include "Transport/Metronome.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;
constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;

void setPoly(juce::AudioProcessor& proc) {
    for (auto* param : proc.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
            ranged != nullptr && ranged->paramID == "poly")
            ranged->setValueNotifyingHost(ranged->convertTo0to1(1.0f));
}

// Replaces the patch with: MIDI input -> Poly MIDI -> poly Oscillator -> VCA (gated by an ADSR) -> Audio Output.
void buildSoundingPatch(AudioEngine& engine) {
    auto& graph = engine.getGraph();
    graph.clear();
    auto midiIn = graph.addNode(std::make_unique<IO>(IO::midiInputNode));
    auto out = graph.addNode(std::make_unique<IO>(IO::audioOutputNode));
    auto poly = graph.addNode(synth::AIStateMapper::createModule("Poly MIDI"));
    auto osc = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    auto adsr = graph.addNode(synth::AIStateMapper::createModule("ADSR"));
    auto vca = graph.addNode(synth::AIStateMapper::createModule("VCA"));
    for (auto* node : {osc.get(), adsr.get(), vca.get()})
        setPoly(*node->getProcessor());

    graph.addConnection({{midiIn->nodeID, kMidi}, {poly->nodeID, kMidi}});
    graph.addConnection({{midiIn->nodeID, kMidi}, {osc->nodeID, kMidi}});
    graph.addConnection({{midiIn->nodeID, kMidi}, {adsr->nodeID, kMidi}});
    // Poly MIDI: pitch fan 0-7, gate fan 8-15. Oscillator: audio L 0-7, audio R 14-21, gate 20-27. VCA: audio L 0-7,
    // gain CV 8-15, audio R 16-23; its outputs are ch0 and ch16.
    for (int v = 0; v < 8; ++v) {
        graph.addConnection({{poly->nodeID, v}, {osc->nodeID, v}});
        graph.addConnection({{poly->nodeID, 8 + v}, {adsr->nodeID, v}});
        graph.addConnection({{poly->nodeID, 8 + v}, {osc->nodeID, 20 + v}});
        graph.addConnection({{adsr->nodeID, v}, {vca->nodeID, 8 + v}});
        graph.addConnection({{osc->nodeID, v}, {vca->nodeID, v}});
        graph.addConnection({{osc->nodeID, 14 + v}, {vca->nodeID, 16 + v}});
    }
    graph.addConnection({{vca->nodeID, 0}, {out->nodeID, 0}});
    graph.addConnection({{vca->nodeID, 16}, {out->nodeID, 1}});
}

// An engine on the silent device with the sounding patch. The graph is edited while the callback is detached.
class SilentEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine.getAudioClipStreamer().setPrefetchPausedForTest(true);
        engine.setSilentAudioDevice(true);
        engine.initialise();
        ASSERT_TRUE(engine.suspendDeviceCallback());
        buildSoundingPatch(engine);
        engine.resumeDeviceCallback();
    }
    void TearDown() override { engine.shutdown(); }

    AudioEngine engine{AudioEngine::HostMode::Standalone};
};

} // namespace

TEST_F(SilentEngineTest, InitialiseOpensTheSilentDeviceAndNothingElse) {
    EXPECT_TRUE(engine.usesSilentAudioDevice());
    auto& manager = engine.getDeviceManager();
    auto* device = dynamic_cast<synth::SilentAudioIODevice*>(manager.getCurrentAudioDevice());
    ASSERT_NE(device, nullptr);
    EXPECT_TRUE(engine.isReceivingDeviceCallbacks());
    EXPECT_EQ(manager.getAvailableDeviceTypes().size(), 1);
    EXPECT_EQ(device->getActiveInputChannels().countNumberOfSetBits(), 0);
    EXPECT_TRUE(engine.getOpenMidiInputIdentifiers().empty());

    const auto blocks = device->blocksRendered();
    synth::test::waitUntil([&] { return device->blocksRendered() > blocks + 2; }, 5000);
    EXPECT_GT(device->blocksRendered(), blocks + 2) << "the device keeps rendering with nothing driving it";
}

TEST_F(SilentEngineTest, ANoteThroughTheMidiSeamRaisesTheVoiceCountAndTheOutputMeter) {
    EXPECT_EQ(engine.getDisplayVoiceCount(), 0);
    auto note = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    note.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    engine.handleIncomingMidiMessageFromSource("test", note);

    synth::test::waitUntil([&] { return engine.getDisplayVoiceCount() > 0; }, 5000);
    EXPECT_GT(engine.getDisplayVoiceCount(), 0);

    float peak = 0.0f;
    synth::test::waitUntil(
        [&] {
            peak = std::max(peak, engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 0));
            return peak > 0.001f;
        },
        5000);
    EXPECT_GT(peak, 0.001f);
}

TEST_F(SilentEngineTest, ALoopSetOnTheTransportReachesThePositionSnapshotWithNoSyncTimer) {
    ASSERT_TRUE(engine.getTransport().setLoop(0.0, 8.0, true));
    synth::test::waitUntil([&] { return engine.getTransport().getPositionSnapshot().looping; }, 5000);
    const auto snap = engine.getTransport().getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 0.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 8.0);
}

TEST_F(SilentEngineTest, PlayAdvancesThePlayheadAndAnEnabledMetronomeClicks) {
    engine.getMetronome().setEnabled(true);
    ASSERT_TRUE(engine.getTransport().play());

    synth::test::waitUntil([&] { return engine.getTransport().getPositionSnapshot().ppq > 0.25; }, 5000);
    const auto snap = engine.getTransport().getPositionSnapshot();
    EXPECT_TRUE(snap.playing);
    EXPECT_GT(snap.ppq, 0.25);

    bool clicked = false;
    synth::test::waitUntil(
        [&] {
            clicked = clicked || engine.getMetronome().getActiveVoiceCountForTest() > 0;
            return clicked;
        },
        5000);
    EXPECT_TRUE(clicked) << "the click renders on the silent device's blocks";
    engine.getTransport().stop();
}

TEST(SilentAudioEngine, TakesPrecedenceOverTheDisabledSwitch) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    engine.setAudioDeviceDisabled(true);
    engine.setSilentAudioDevice(true);
    engine.initialise();
    EXPECT_NE(dynamic_cast<synth::SilentAudioIODevice*>(engine.getDeviceManager().getCurrentAudioDevice()), nullptr);
    engine.shutdown();
}

TEST(SilentAudioEngine, IgnoresASavedDeviceSetup) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    engine.setSavedDeviceState(
        juce::parseXML("<DEVICESETUP deviceType=\"CoreAudio\" audioOutputDeviceName=\"Elsewhere\" "
                       "audioInputDeviceName=\"Mic\" audioDeviceInChans=\"11\"/>"));
    engine.setSilentAudioDevice(true);
    engine.initialise();
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->getName(), juce::String(synth::SilentAudioIODevice::kDeviceName));
    EXPECT_EQ(device->getActiveInputChannels().countNumberOfSetBits(), 0);
    engine.shutdown();
}
