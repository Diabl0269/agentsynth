// MIDI/audio thread: the "any channel" fallback never claims per-note expression on a member channel of a
// profile's FORCED MPE zone
// (docs/control/midi-remote-mpe.md#2-coexistence-rule-member-channels-are-the-synths-not-the-controllers-layers). Every
// test drives RemoteEngine::handleMessage(): a mapped message is consumed (returns true), an unmapped or skipped one
// passes through (false). Suite names contain "MidiRemote" per the ship-task --gtest_filter convention.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"

#include <gtest/gtest.h>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "dev";

struct MpeHarness {
    RemoteEngine engine;
    std::vector<Control> controls;
    std::vector<Assignment> assignments;
    MpeSettings mpe;

    void addControl(const juce::String& id, MessageType type, int channel, int number) {
        Control control;
        control.id = id;
        control.name = id;
        control.message.type = type;
        control.message.channel = channel;
        control.message.number = number;
        controls.push_back(control);

        Assignment assignment;
        assignment.id = id + "-assign";
        assignment.control.profileId = "profile";
        assignment.control.controlId = id;
        assignment.spec = control.message;
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
        profile.mpe = mpe;
        engine.setProfiles({profile});
        engine.setSources({juce::String(kSource)});
        engine.setAssignments(assignments);
    }

    bool send(const juce::MidiMessage& message) { return engine.handleMessage(kSource, message); }
};

MpeSettings makeMpe(MpeSettings::Mode mode, int lower = 15, int upper = 15) {
    MpeSettings mpe;
    mpe.mode = mode;
    mpe.lowerMembers = lower;
    mpe.upperMembers = upper;
    return mpe;
}

// Every any-channel expression assignment, as an MPE player would trip over.
void addAnyChannelExpression(MpeHarness& h) {
    h.addControl("pb", MessageType::pitchBend, 0, 0);
    h.addControl("cp", MessageType::channelPressure, 0, 0);
    h.addControl("slide", MessageType::cc, 0, 74);
    h.addControl("note", MessageType::note, 0, 60);
}

} // namespace

TEST(MidiRemoteEngineMpeTest, ForcedMaskCoversLowerUpperAndBothZones) {
    EXPECT_EQ(makeMpe(MpeSettings::Mode::automatic).forcedMemberMask(), 0u);
    EXPECT_EQ(makeMpe(MpeSettings::Mode::off).forcedMemberMask(), 0u);
    // Lower 15: channels 2..16.
    EXPECT_EQ(makeMpe(MpeSettings::Mode::lower).forcedMemberMask(), 0xfffeu);
    // Lower 4: channels 2..5.
    EXPECT_EQ(makeMpe(MpeSettings::Mode::lower, 4).forcedMemberMask(), 0b11110u);
    // Upper 3: channels 15, 14, 13.
    EXPECT_EQ(makeMpe(MpeSettings::Mode::upper, 15, 3).forcedMemberMask(), (1u << 14) | (1u << 13) | (1u << 12));
    // Both 7 + 7: channels 2..8 and 9..15, never the managers 1 and 16.
    EXPECT_EQ(makeMpe(MpeSettings::Mode::both, 7, 7).forcedMemberMask(), 0x7ffeu);
}

TEST(MidiRemoteEngineMpeTest, LowerZoneAnyChannelExpressionOnMemberChannelsPassesThrough) {
    MpeHarness h;
    h.mpe = makeMpe(MpeSettings::Mode::lower);
    addAnyChannelExpression(h);
    h.finalize();

    for (int channel = 2; channel <= 16; ++channel) {
        EXPECT_FALSE(h.send(juce::MidiMessage::pitchWheel(channel, 9000))) << "pitch bend ch " << channel;
        EXPECT_FALSE(h.send(juce::MidiMessage::channelPressureChange(channel, 40))) << "pressure ch " << channel;
        EXPECT_FALSE(h.send(juce::MidiMessage::controllerEvent(channel, 74, 30))) << "CC 74 ch " << channel;
        EXPECT_FALSE(h.send(juce::MidiMessage::noteOn(channel, 60, 0.5f))) << "note ch " << channel;
    }
}

TEST(MidiRemoteEngineMpeTest, LowerZoneManagerChannelIsStillMatchedAndConsumed) {
    MpeHarness h;
    h.mpe = makeMpe(MpeSettings::Mode::lower);
    addAnyChannelExpression(h);
    h.finalize();

    EXPECT_TRUE(h.send(juce::MidiMessage::pitchWheel(1, 9000)));
    EXPECT_TRUE(h.send(juce::MidiMessage::channelPressureChange(1, 40)));
    EXPECT_TRUE(h.send(juce::MidiMessage::controllerEvent(1, 74, 30)));
    EXPECT_TRUE(h.send(juce::MidiMessage::noteOn(1, 60, 0.5f)));
}

TEST(MidiRemoteEngineMpeTest, OtherCcsOnAMemberChannelStillUseTheAnyChannelSlot) {
    MpeHarness h;
    h.mpe = makeMpe(MpeSettings::Mode::lower);
    h.addControl("mod", MessageType::cc, 0, 1);
    h.finalize();

    EXPECT_TRUE(h.send(juce::MidiMessage::controllerEvent(5, 1, 64))) << "only CC 74 is per-note expression";
}

TEST(MidiRemoteEngineMpeTest, ExactChannelAssignmentOnAMemberChannelIsStillHonoured) {
    MpeHarness h;
    h.mpe = makeMpe(MpeSettings::Mode::lower);
    h.addControl("pb5", MessageType::pitchBend, 5, 0);
    h.addControl("slide7", MessageType::cc, 7, 74);
    h.finalize();

    EXPECT_TRUE(h.send(juce::MidiMessage::pitchWheel(5, 9000)));
    EXPECT_TRUE(h.send(juce::MidiMessage::controllerEvent(7, 74, 30)));
    EXPECT_FALSE(h.send(juce::MidiMessage::pitchWheel(6, 9000)));
}

TEST(MidiRemoteEngineMpeTest, AutoAndOffKeepTheV1AnyChannelBehaviour) {
    for (const auto mode : {MpeSettings::Mode::automatic, MpeSettings::Mode::off}) {
        MpeHarness h;
        h.mpe = makeMpe(mode);
        addAnyChannelExpression(h);
        h.finalize();

        EXPECT_TRUE(h.send(juce::MidiMessage::pitchWheel(5, 9000)));
        EXPECT_TRUE(h.send(juce::MidiMessage::channelPressureChange(5, 40)));
        EXPECT_TRUE(h.send(juce::MidiMessage::controllerEvent(5, 74, 30)));
        EXPECT_TRUE(h.send(juce::MidiMessage::noteOn(5, 60, 0.5f)));
    }
}

TEST(MidiRemoteEngineMpeTest, UpperZoneSkipsOnlyItsOwnMembersCountingDownFromChannelFifteen) {
    MpeHarness h;
    h.mpe = makeMpe(MpeSettings::Mode::upper, 15, 3);
    addAnyChannelExpression(h);
    h.finalize();

    for (const int member : {13, 14, 15})
        EXPECT_FALSE(h.send(juce::MidiMessage::pitchWheel(member, 9000))) << member;
    for (const int other : {1, 2, 12, 16})
        EXPECT_TRUE(h.send(juce::MidiMessage::pitchWheel(other, 9000))) << other;
}

TEST(MidiRemoteEngineMpeTest, BothZonesSkipEveryMemberAndKeepBothManagers) {
    MpeHarness h;
    h.mpe = makeMpe(MpeSettings::Mode::both, 7, 7);
    addAnyChannelExpression(h);
    h.finalize();

    for (int channel = 2; channel <= 15; ++channel)
        EXPECT_FALSE(h.send(juce::MidiMessage::channelPressureChange(channel, 40))) << channel;
    EXPECT_TRUE(h.send(juce::MidiMessage::channelPressureChange(1, 40)));
    EXPECT_TRUE(h.send(juce::MidiMessage::channelPressureChange(16, 40)));
}

TEST(MidiRemoteEngineMpeTest, PassMappedStillLeavesMemberTrafficUnmapped) {
    // The skipped message is unmapped, so it never reaches the activity-vs-consumed decision of a slot.
    MpeHarness h;
    h.mpe = makeMpe(MpeSettings::Mode::lower);
    h.addControl("pb", MessageType::pitchBend, 0, 0);
    h.finalize();

    std::vector<RemoteEvent> events;
    h.send(juce::MidiMessage::pitchWheel(3, 9000));
    h.engine.drainActivity([&](const juce::String&, const RemoteEvent& e) { events.push_back(e); });
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().slotIndex, -1);
}
