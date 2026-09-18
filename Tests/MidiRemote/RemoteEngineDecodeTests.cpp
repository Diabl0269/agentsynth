// MIDI/audio thread: RemoteEngine::handleMessage's classify -> lookup -> decode pipeline
// (docs/control/midi-remote.md#are-mapped-messages-consumed-or-also-forwarded-to-the-graph,
// docs/control/midi-remote.md#the-engine). Every test here drives handleMessage() directly and reads back through
// drainActivity(), which mirrors every decoded event -- assigned or not -- without requiring a resolved parameter
// (RemoteEvent.h's file comment). Suite names contain "MidiRemote" per the ship-task --gtest_filter convention.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"

#include <gtest/gtest.h>
#include <iterator>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "dev";

// One RemoteEngine plus the bookkeeping to add (control, assignment) pairs to a single profile
// before publishing. Every assignment targets a never-resolving parameter -- decode only depends
// on slot.encoding/slot.buttonLike, never on whether the target actually resolves (RemoteEngine.h:
// applying happens later, in drain(), which none of these tests call).
struct DecodeHarness {
    RemoteEngine engine;
    std::vector<Control> controls;
    std::vector<Assignment> assignments;

    // `kind` controls buttonLike (ControlKind::button/pad) vs an ordinary continuous control
    // (ControlKind::knob) -- see RemoteEngineReconcile.cpp's addSlot for the exact buttonLike rule.
    void addControl(const juce::String& id, MessageType type, int channel, int number, Encoding encoding,
                    ControlKind kind = ControlKind::knob) {
        Control control;
        control.id = id;
        control.name = id;
        control.kind = kind;
        control.message.type = type;
        control.message.channel = channel;
        control.message.number = number;
        control.encoding = encoding;
        controls.push_back(control);

        Assignment assignment;
        assignment.id = id + "-assign";
        assignment.control.profileId = "profile";
        assignment.control.controlId = id;
        assignment.spec.type = type;
        assignment.spec.channel = channel;
        assignment.spec.number = number;
        assignment.specEncoding = encoding;
        assignment.target.kind = Target::Kind::parameter;
        assignment.target.parameter.nodeUuid = "unresolvable-node";
        assignment.target.parameter.paramId = "unresolvable-param";
        assignments.push_back(assignment);
    }

    void finalize() {
        ControllerProfile profile;
        profile.id = "profile";
        profile.name = "profile";
        profile.input.identifier = kSource;
        profile.input.name = kSource;
        profile.controls = controls;
        engine.setProfiles({profile});
        engine.setSources({juce::String(kSource)});
        engine.setAssignments(assignments);
    }

    bool send(const juce::MidiMessage& message) { return engine.handleMessage(kSource, message); }

    // Every activity event queued since the last drain, in FIFO order -- decoded, assigned or not.
    std::vector<RemoteEvent> drainActivity() {
        std::vector<RemoteEvent> events;
        engine.drainActivity([&](const juce::String&, const RemoteEvent& event) { events.push_back(event); });
        return events;
    }

    // Convenience for the common "send one message, read back exactly one activity event" shape.
    RemoteEvent sendAndReadOne(const juce::MidiMessage& message) {
        send(message);
        auto events = drainActivity();
        EXPECT_EQ(events.size(), 1u);
        return events.empty() ? RemoteEvent{} : events.front();
    }
};

} // namespace

// ============================================================================
// abs7 -- CC, pitch bend, channel pressure
// ============================================================================

TEST(MidiRemoteEngineDecodeTest, Abs7DecodesCcPitchBendAndChannelPressure) {
    DecodeHarness h;
    h.addControl("cc", MessageType::cc, 1, 10, Encoding::abs7);
    h.addControl("pb", MessageType::pitchBend, 1, 0, Encoding::abs7);
    h.addControl("cp", MessageType::channelPressure, 1, 0, Encoding::abs7);
    h.finalize();

    // CC 0 / 64 / 127 -> 0.0 / ~0.504 / 1.0.
    EXPECT_NEAR(h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 10, 0)).value, 0.0f, 1e-6f);
    EXPECT_NEAR(h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 10, 64)).value, 64.0f / 127.0f, 1e-6f);
    EXPECT_NEAR(h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 10, 127)).value, 1.0f, 1e-6f);

    // Pitch bend 0 / 8192 / 16383 -> 0.0 / ~0.5 / 1.0.
    EXPECT_NEAR(h.sendAndReadOne(juce::MidiMessage::pitchWheel(1, 0)).value, 0.0f, 1e-6f);
    EXPECT_NEAR(h.sendAndReadOne(juce::MidiMessage::pitchWheel(1, 8192)).value, 8192.0f / 16383.0f, 1e-6f);
    EXPECT_NEAR(h.sendAndReadOne(juce::MidiMessage::pitchWheel(1, 16383)).value, 1.0f, 1e-6f);

    // Channel pressure 0 / 127 -> 0.0 / 1.0.
    EXPECT_NEAR(h.sendAndReadOne(juce::MidiMessage::channelPressureChange(1, 0)).value, 0.0f, 1e-6f);
    EXPECT_NEAR(h.sendAndReadOne(juce::MidiMessage::channelPressureChange(1, 127)).value, 1.0f, 1e-6f);

    EXPECT_EQ(h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 10, 1)).kind, RemoteEventKind::absolute);
}

// ============================================================================
// Relative encodings
// ============================================================================

TEST(MidiRemoteEngineDecodeTest, RelTwosDecodesSignedDeltas) {
    DecodeHarness h;
    h.addControl("rt", MessageType::cc, 1, 50, Encoding::relTwos);
    h.finalize();

    const auto check = [&](int ccValue, float expectedDelta) {
        const auto event = h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 50, ccValue));
        EXPECT_EQ(event.kind, RemoteEventKind::relativeDelta);
        EXPECT_NEAR(event.value, expectedDelta, 1e-6f);
    };

    check(1, 1.0f / 127.0f);
    check(127, -1.0f / 127.0f);
    check(63, 63.0f / 127.0f);
    check(64, -64.0f / 127.0f);
}

TEST(MidiRemoteEngineDecodeTest, RelBinOffsetDecodesAroundSixtyFour) {
    DecodeHarness h;
    h.addControl("rb", MessageType::cc, 1, 51, Encoding::relBinOffset);
    h.finalize();

    const auto check = [&](int ccValue, float expectedDelta) {
        const auto event = h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 51, ccValue));
        EXPECT_EQ(event.kind, RemoteEventKind::relativeDelta);
        EXPECT_NEAR(event.value, expectedDelta, 1e-6f);
    };

    check(65, 1.0f / 127.0f);
    check(63, -1.0f / 127.0f);
    check(64, 0.0f);
}

TEST(MidiRemoteEngineDecodeTest, RelSignMagDecodesSignBit) {
    DecodeHarness h;
    h.addControl("rs", MessageType::cc, 1, 52, Encoding::relSignMag);
    h.finalize();

    const auto check = [&](int ccValue, float expectedDelta) {
        const auto event = h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 52, ccValue));
        EXPECT_EQ(event.kind, RemoteEventKind::relativeDelta);
        EXPECT_NEAR(event.value, expectedDelta, 1e-6f);
    };

    check(0x01, 1.0f / 127.0f);
    check(0x41, -1.0f / 127.0f);
}

// ============================================================================
// Buttons -- note, CC, program change
// ============================================================================

TEST(MidiRemoteEngineDecodeTest, ButtonLikeControlsDecodeNoteCcAndProgramChange) {
    DecodeHarness h;
    h.addControl("note-btn", MessageType::note, 1, 20, Encoding::abs7, ControlKind::button);
    h.addControl("cc-btn", MessageType::cc, 1, 30, Encoding::abs7, ControlKind::button);
    h.addControl("pc-btn", MessageType::programChange, 1, 5, Encoding::abs7, ControlKind::button);
    h.finalize();

    // Note-on velocity > 0 -> press; note-on velocity 0 -> release (classified as a note-off);
    // a real note-off -> release.
    {
        const auto press = h.sendAndReadOne(juce::MidiMessage::noteOn(1, 20, (juce::uint8)100));
        EXPECT_EQ(press.kind, RemoteEventKind::buttonPress);
        EXPECT_NEAR(press.value, 100.0f / 127.0f, 1e-6f);

        const auto zeroVelocity = h.sendAndReadOne(juce::MidiMessage::noteOn(1, 20, (juce::uint8)0));
        EXPECT_EQ(zeroVelocity.kind, RemoteEventKind::buttonRelease);
        EXPECT_NEAR(zeroVelocity.value, 0.0f, 1e-6f);

        const auto realOff = h.sendAndReadOne(juce::MidiMessage::noteOff(1, 20));
        EXPECT_EQ(realOff.kind, RemoteEventKind::buttonRelease);
        EXPECT_NEAR(realOff.value, 0.0f, 1e-6f);
    }

    // CC >= 64 -> press; CC < 64 -> release.
    {
        const auto press127 = h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 30, 127));
        EXPECT_EQ(press127.kind, RemoteEventKind::buttonPress);
        EXPECT_NEAR(press127.value, 1.0f, 1e-6f);

        const auto press64 = h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 30, 64));
        EXPECT_EQ(press64.kind, RemoteEventKind::buttonPress);
        EXPECT_NEAR(press64.value, 64.0f / 127.0f, 1e-6f);

        const auto release63 = h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 30, 63));
        EXPECT_EQ(release63.kind, RemoteEventKind::buttonRelease);
        EXPECT_NEAR(release63.value, 0.0f, 1e-6f);

        const auto release0 = h.sendAndReadOne(juce::MidiMessage::controllerEvent(1, 30, 0));
        EXPECT_EQ(release0.kind, RemoteEventKind::buttonRelease);
        EXPECT_NEAR(release0.value, 0.0f, 1e-6f);
    }

    // Program change is always a press, at value 1.0.
    {
        const auto pc = h.sendAndReadOne(juce::MidiMessage::programChange(1, 5));
        EXPECT_EQ(pc.kind, RemoteEventKind::buttonPress);
        EXPECT_NEAR(pc.value, 1.0f, 1e-6f);
    }
}

// ============================================================================
// Channel matching -- exact channel wins, channel 0 means "any"
// ============================================================================

TEST(MidiRemoteEngineDecodeTest, ChannelZeroMeansAnyChannelButAnExactChannelIsExclusive) {
    DecodeHarness h;
    h.addControl("any-ch", MessageType::cc, /*channel=*/0, 40, Encoding::abs7);
    h.addControl("ch3-only", MessageType::cc, /*channel=*/3, 41, Encoding::abs7);
    h.finalize();

    // A control registered on channel 0 ("any") matches a message on any real channel.
    EXPECT_TRUE(h.send(juce::MidiMessage::controllerEvent(5, 40, 100)));
    auto anyChannelEvents = h.drainActivity();
    ASSERT_EQ(anyChannelEvents.size(), 1u);
    EXPECT_NE(anyChannelEvents.front().slotIndex, -1);
    EXPECT_NEAR(anyChannelEvents.front().value, 100.0f / 127.0f, 1e-6f);

    // A control registered on channel 3 does NOT match the same number on channel 4 -- exact
    // channel is exclusive, and there is no channel-0 fallback entry for this number to catch it.
    EXPECT_FALSE(h.send(juce::MidiMessage::controllerEvent(4, 41, 100)));
    auto mismatchEvents = h.drainActivity();
    ASSERT_EQ(mismatchEvents.size(), 1u);
    EXPECT_EQ(mismatchEvents.front().slotIndex, -1) << "an exact-channel control must not catch a different channel";
}

// ============================================================================
// Unassigned-but-known control: activity fires, nothing is ever consumed
// (docs/control/midi-remote.md#are-mapped-messages-consumed-or-also-forwarded-to-the-graph)
// ============================================================================

TEST(MidiRemoteEngineDecodeTest, UnassignedControlProducesActivityButIsNeverConsumed) {
    DecodeHarness h;
    h.addControl("assigned", MessageType::cc, 1, 60, Encoding::abs7);
    h.finalize();

    // CC 61 on the same known source has no assignment at all.
    EXPECT_FALSE(h.send(juce::MidiMessage::controllerEvent(1, 61, 77)));
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().slotIndex, -1);
    EXPECT_EQ(events.front().kind, RemoteEventKind::absolute);
    EXPECT_NEAR(events.front().value, 77.0f / 127.0f, 1e-6f);
}

// ============================================================================
// Ineligible messages: dropped before any lookup, nothing queued, never consumed
// ============================================================================

TEST(MidiRemoteEngineDecodeTest, IneligibleMessagesAreDroppedEntirely) {
    DecodeHarness h;
    h.addControl("assigned", MessageType::cc, 1, 70, Encoding::abs7);
    h.finalize();

    // MIDI clock, active sensing (raw 0xFE, no static factory exists), sysex, and poly (per-note)
    // aftertouch -- distinct from channel pressure, which IS eligible -- must all be dropped before
    // handleMessage ever looks at the source or the lookup table: no activity, no ring event.
    const juce::uint8 sysexPayload[] = {0x01, 0x02, 0x03};
    const std::vector<juce::MidiMessage> ineligible = {
        juce::MidiMessage::midiClock(),
        juce::MidiMessage(0xFE), // active sensing
        juce::MidiMessage::createSysExMessage(sysexPayload, (int)std::size(sysexPayload)),
        juce::MidiMessage::aftertouchChange(1, 60, 100), // poly/per-note aftertouch
    };

    for (const auto& message : ineligible) {
        EXPECT_FALSE(h.send(message));
        EXPECT_TRUE(h.drainActivity().empty()) << "an ineligible message must never reach the activity ring";
    }
}
