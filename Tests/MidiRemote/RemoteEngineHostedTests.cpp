// docs/control/midi-remote.md#the-plugin-build-vst3au-inside-a-host: HostMode::Hosted never opens hardware MIDI, and
// the SAME early-return in AudioEngine::handleIncomingMidiMessageFromSource (Source/AudioEngine/AudioEngineMidi.cpp)
// gates BOTH of a message's non-RemoteEngine destinations -- the MidiMessageCollector push and the
// ExternalMidiModule name-match fan-out:
//
//     if (auto* sink = remoteMessageSink_.load(...); sink != nullptr)
//         if (sink->handleMessage(sourceKey, message))
//             return;                                  // <-- neither destination is reached
//
// so proving a message never reaches ONE of them (through the real seam, not a fake) is proof it
// never reaches either. This file proves it against the collector, which IS observable headlessly
// via the same FakeAudioIODevice + MidiRecorder recipe Tests/Engine/DeviceChangeTests.cpp and
// Tests/Timeline/MidiRecorderTests.cpp already use. It cannot ALSO be proven directly against the
// ExternalMidiModule fan-out: that branch requires a non-null real juce::MidiInput*, and
// Tests/Engine/BounceExporter/BounceExporterTransportTests.cpp already documents why headless code
// can't construct one (MidiInput's constructor is private; only openDevice()/createNewDevice() can
// make one, and both talk to real OS MIDI drivers). See that file's comment on
// ExternalMidiSuspendedOnlyForTheDurationOfARender for the established precedent this follows.
//
// Suite names contain "MidiRemote" per the ship-task --gtest_filter convention.

#include "../FakeAudioIODevice.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Timeline/MidiRecorder.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"

#include <gtest/gtest.h>
#include <set>
#include <vector>

using namespace synth;
using namespace synth::midi;
using synth::test::FakeAudioIODevice;
using synth::test::kFakeDeviceBlockSize;

namespace {

// One button-target action assignment on a note number, bound to `deviceKey`'s profile.
Assignment makeNoteActionAssignment(const juce::String& id, const juce::String& profileId,
                                    const juce::String& controlId, int noteNumber, const juce::String& actionId) {
    Assignment a;
    a.id = id;
    a.control.profileId = profileId;
    a.control.controlId = controlId;
    a.spec.type = MessageType::note;
    a.spec.channel = 1;
    a.spec.number = noteNumber;
    a.specEncoding = Encoding::abs7;
    a.target.kind = Target::Kind::action;
    a.target.action.actionId = actionId;
    return a;
}

Control makeButtonControl(const juce::String& id, int noteNumber) {
    Control c;
    c.id = id;
    c.name = id;
    c.kind = ControlKind::button;
    c.message.type = MessageType::note;
    c.message.channel = 1;
    c.message.number = noteNumber;
    return c;
}

ControllerProfile makeProfile(const juce::String& id, const juce::String& deviceKey, bool passMapped,
                              const Control& control) {
    ControllerProfile p;
    p.id = id;
    p.name = id;
    p.input.identifier = deviceKey;
    p.input.name = deviceKey;
    p.passMapped = passMapped;
    p.controls.push_back(control);
    return p;
}

class CountingActionInvoker : public RemoteActionInvoker {
public:
    void invokeRemoteCommand(juce::CommandID commandId) override { invoked.push_back(commandId); }
    // FRO253: records a nodeCommand invocation the same way invokeRemoteCommand above does.
    void invokeNodeCommand(juce::AudioProcessorGraph::NodeID nodeId, NodeCommandKind command) override {
        invokedNodeCommands.push_back({nodeId, command});
    }
    // FRO236: this suite doesn't exercise continuous targets -- stub, never called.
    double getContinuousValue(ContinuousTargetKind) override { return 0.0; }
    void setContinuousValue(ContinuousTargetKind, double) override {}
    bool getContinuousWindow(ContinuousTargetKind, double&, double&) override { return false; }
    std::vector<juce::CommandID> invoked;
    std::vector<std::pair<juce::AudioProcessorGraph::NodeID, NodeCommandKind>> invokedNodeCommands;
};

// One silent standalone device callback, exactly like Tests/Engine/DeviceChangeTests.cpp's
// driveSilentBlock -- it is what drains AudioEngine's midiMessageCollector into the buffer
// renderPass hands the MidiRecorder.
void driveOneBlock(AudioEngine& engine) {
    std::vector<float> left((std::size_t)kFakeDeviceBlockSize, 0.0f), right((std::size_t)kFakeDeviceBlockSize, 0.0f);
    std::vector<float> outLeft((std::size_t)kFakeDeviceBlockSize, 0.0f),
        outRight((std::size_t)kFakeDeviceBlockSize, 0.0f);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kFakeDeviceBlockSize, {});
}

} // namespace

// ============================================================================
// synth::midi::hostSourceKey()
// ============================================================================

TEST(MidiRemoteEngineHostedTest, HostSourceKeyEqualsHost) { EXPECT_EQ(hostSourceKey(), juce::String("host")); }

TEST(MidiRemoteEngineHostedTest, HostSourceKeyIsAStableFunctionLocalStatic) {
    // A function-local static, never a temporary -- constructing a juce::String on the MIDI path
    // would allocate (RemoteMessageSink.h's class comment).
    EXPECT_EQ(&hostSourceKey(), &hostSourceKey());
}

// ============================================================================
// HostMode::Hosted never opens hardware MIDI
// ============================================================================

TEST(MidiRemoteEngineHostedTest, HostedAudioEngineNeverOpensAMidiDevice) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.ensureMidiDeviceOpen("Some Real Controller");
    engine.openMidiDevicesForRemote({"Some Real Controller", "Another One"});
    EXPECT_TRUE(engine.getOpenMidiInputIdentifiers().empty())
        << "HostMode::Hosted never touches hardware -- the host owns device routing "
           "(Source/CLAUDE.md's MIDI-path tripwire)";
}

// ============================================================================
// Consumed vs. passed, through the real AudioEngine::handleIncomingMidiMessageFromSource seam
// ============================================================================

TEST(MidiRemoteEngineHostedTest, ConsumedMessageNeverReachesTheCollectorWhileUnmappedAndPassMappedDo) {
    constexpr int kConsumedNote = 60;   // profile "p1", passMapped = false -- fully consumed
    constexpr int kUnmappedNote = 61;   // no assignment at all -- must reach the collector
    constexpr int kPassMappedNote = 62; // profile "p2", passMapped = true -- applied AND passed

    AudioEngine engine(AudioEngine::HostMode::Standalone);
    FakeAudioIODevice fakeDevice(2, 2);
    engine.audioDeviceAboutToStart(&fakeDevice);

    RemoteEngine remote;
    CountingActionInvoker invoker;
    remote.setActionInvoker(&invoker);
    remote.setActionCommandLookup([](const juce::String& actionId) -> juce::CommandID {
        if (actionId == "consumed.action")
            return 101;
        if (actionId == "passmapped.action")
            return 102;
        return 0;
    });
    remote.setProfiles({makeProfile("p1", "dev", /*passMapped=*/false, makeButtonControl("btn60", kConsumedNote)),
                        makeProfile("p2", "dev2", /*passMapped=*/true, makeButtonControl("btn62", kPassMappedNote))});
    remote.setSources({"dev", "dev2"});
    remote.setAssignments(
        {makeNoteActionAssignment("a-consumed", "p1", "btn60", kConsumedNote, "consumed.action"),
         makeNoteActionAssignment("a-passmapped", "p2", "btn62", kPassMappedNote, "passmapped.action")});
    engine.setRemoteMessageSink(&remote);

    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");
    MidiRecorder recorder;
    engine.setMidiCaptureSink(&recorder);

    ASSERT_TRUE(engine.getTransport().play());
    driveOneBlock(engine); // ticks the transport into "playing" before anything is captured
    recorder.startRecording(track, engine.getTransport().getPositionSnapshot().ppq);

    const auto sendNote = [&](const juce::String& sourceKey, int noteNumber) {
        engine.handleIncomingMidiMessageFromSource(sourceKey,
                                                   juce::MidiMessage::noteOn(1, noteNumber, (juce::uint8)100));
        driveOneBlock(engine);
        engine.handleIncomingMidiMessageFromSource(sourceKey, juce::MidiMessage::noteOff(1, noteNumber));
        driveOneBlock(engine);
    };

    sendNote("dev", kConsumedNote);
    sendNote("dev", kUnmappedNote);
    sendNote("dev2", kPassMappedNote);

    // Apply the queued events (fires the two assigned actions; the unmapped note has no slot to
    // apply at all).
    remote.drain();
    EXPECT_EQ(invoker.invoked.size(), 2u) << "the consumed action and the passMapped action both apply";

    ASSERT_TRUE(recorder.stopAndCommit(doc, undo));
    const auto* trackPtr = doc.getTrack(track);
    ASSERT_NE(trackPtr, nullptr);
    ASSERT_EQ(trackPtr->clips.size(), 1u);

    std::set<int> recordedPitches;
    for (const auto& note : trackPtr->clips[0].notes)
        recordedPitches.insert(note.pitch);

    EXPECT_EQ(recordedPitches.count(kConsumedNote), 0u)
        << "a fully consumed message must never reach the collector-drained buffer";
    EXPECT_EQ(recordedPitches.count(kUnmappedNote), 1u) << "an unmapped message must still reach it";
    EXPECT_EQ(recordedPitches.count(kPassMappedNote), 1u) << "passMapped applies the message AND still lets it through";

    // RemoteEngine.h's LIFETIME comment: the owner must clear the sink before the sink object is
    // destroyed. `remote` and `recorder` are local and would outlive-order incorrectly (destroyed
    // before `engine`), so unhook both explicitly rather than relying on destruction order.
    engine.setRemoteMessageSink(nullptr);
    engine.setMidiCaptureSink(nullptr);
    engine.audioDeviceStopped();
}
