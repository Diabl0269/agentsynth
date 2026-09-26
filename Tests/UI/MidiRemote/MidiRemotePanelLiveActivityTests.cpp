// MidiRemotePanelLiveActivityTests.cpp -- FRO272: a hardware move reaches the panel's surface on the
// next activity tick, without a dock tab switch. Drives the same path MainComponent::timerCallback
// does: RemoteEngine::handleMessage (MIDI thread) -> drain (message-thread apply) ->
// MidiRemotePanelComponent::refreshActivity -> ControllerSurfaceCell::noteActivity.
#include "MidiRemotePanelTestFixture.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace {

constexpr int kMappedCc = 20;
constexpr int kUnmappedCc = 21;

} // namespace

class MidiRemotePanelLiveActivityTest : public MidiRemotePanelLiveRefreshTest {
protected:
    // Learns CC 20 onto the filter's cutoff and shows that controller's surface, the state a user
    // is in after mapping a knob with the panel open.
    void learnAndShowMappedKnob() {
        controller_->arm(node_->nodeID, "cutoff");
        send(juce::MidiMessage::controllerEvent(1, kMappedCc, 64));
        settle();
        pump();
        ASSERT_EQ(doc_.assignments.size(), 1u);
        profileId_ = doc_.assignments.front().control.profileId;
        mappedControlId_ = doc_.assignments.front().control.controlId;
        const juce::String nodeUuid = node_->properties["uuid"].toString();
        ASSERT_TRUE(panel_.selectAssignmentForParameter(nodeUuid, "cutoff"));
    }

    // Adds a second, unassigned knob on CC 21 to the same profile (what Detect or a template does).
    void addUnmappedKnob() {
        const auto& profiles = controller_->getProfiles();
        auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == profileId_; });
        ASSERT_NE(it, profiles.end());
        auto profile = *it;
        synth::Control knob;
        knob.id = "unmapped-knob";
        knob.name = "Knob 2";
        knob.kind = synth::ControlKind::knob;
        knob.message.type = synth::MessageType::cc;
        knob.message.channel = 1;
        knob.message.number = kUnmappedCc;
        knob.layout.col = 1;
        knob.layout.row = 0;
        profile.controls.push_back(knob);
        ASSERT_TRUE(controller_->updateProfile(profile));
        pump();
        panel_.selectForTest(profileId_, mappedControlId_);
        ASSERT_NE(panel_.findSurfaceCellForTest("unmapped-knob"), nullptr);
    }

    // One MainComponent 10 Hz tick's worth of work for the MIDI Remote tab.
    void tick() {
        remoteEngine_.drain();
        panel_.refreshActivity();
        pump();
    }

    juce::String profileId_;
    juce::String mappedControlId_;
};

TEST_F(MidiRemotePanelLiveActivityTest, MappedKnobMoveShowsOnTheSurfaceWithoutATabSwitch) {
    learnAndShowMappedKnob();

    for (int value : {10, 40, 100, 127}) {
        send(juce::MidiMessage::controllerEvent(1, kMappedCc, value));
        tick();
        EXPECT_NEAR(panel_.getSurfaceCellValueForTest(mappedControlId_), value / 127.0f, 0.02f)
            << "after moving the mapped knob to " << value;
    }
}

TEST_F(MidiRemotePanelLiveActivityTest, UnmappedKnobMoveShowsOnTheSurfaceWithoutATabSwitch) {
    learnAndShowMappedKnob();
    addUnmappedKnob();

    for (int value : {5, 90, 30}) {
        send(juce::MidiMessage::controllerEvent(1, kUnmappedCc, value));
        tick();
        EXPECT_NEAR(panel_.getSurfaceCellValueForTest("unmapped-knob"), value / 127.0f, 0.02f)
            << "after moving the unmapped knob to " << value;
    }
}

// A burst of moves between two ticks (a fast twist at 10 Hz) must end on the last position.
TEST_F(MidiRemotePanelLiveActivityTest, FastTwistBetweenTicksEndsOnTheLastPosition) {
    learnAndShowMappedKnob();

    for (int value = 0; value <= 127; value += 3)
        send(juce::MidiMessage::controllerEvent(1, kMappedCc, value));
    send(juce::MidiMessage::controllerEvent(1, kMappedCc, 111));
    tick();
    EXPECT_NEAR(panel_.getSurfaceCellValueForTest(mappedControlId_), 111 / 127.0f, 0.02f);
}

// Two profiles bound to the same device (an imported copy of a profile, say): the one the user has
// selected must move live, even when it is not the first profile listed for that device.
TEST_F(MidiRemotePanelLiveActivityTest, SecondProfileOnTheSameDeviceMovesLive) {
    learnAndShowMappedKnob();

    synth::ControllerProfile copy;
    copy.id = "second-profile-same-device";
    copy.name = "Imported copy";
    copy.input.identifier = synth::midi::hostSourceKey();
    synth::Control knob;
    knob.id = "copy-knob";
    knob.name = "Knob";
    knob.kind = synth::ControlKind::knob;
    knob.message.type = synth::MessageType::cc;
    knob.message.channel = 1;
    knob.message.number = kUnmappedCc;
    copy.controls.push_back(knob);
    ASSERT_TRUE(controller_->addProfile(copy));
    pump();
    panel_.selectForTest(copy.id, knob.id);
    ASSERT_NE(panel_.findSurfaceCellForTest(knob.id), nullptr);

    send(juce::MidiMessage::controllerEvent(1, kUnmappedCc, 99));
    tick();
    EXPECT_NEAR(panel_.getSurfaceCellValueForTest(knob.id), 99 / 127.0f, 0.02f)
        << "before FRO272 only the first profile on a device ever received its activity";
}

// The parameter moving under a mapped knob fires the panel's live refresh (a rebuild). The rebuild
// must not reset the cell to a stale value between moves.
TEST_F(MidiRemotePanelLiveActivityTest, MovesInterleavedWithPumpsStayCurrent) {
    learnAndShowMappedKnob();

    for (int value : {20, 70, 120, 50}) {
        send(juce::MidiMessage::controllerEvent(1, kMappedCc, value));
        remoteEngine_.drain();
        pump();
        panel_.refreshActivity();
        pump();
        EXPECT_NEAR(panel_.getSurfaceCellValueForTest(mappedControlId_), value / 127.0f, 0.02f)
            << "after moving the mapped knob to " << value;
    }
}
