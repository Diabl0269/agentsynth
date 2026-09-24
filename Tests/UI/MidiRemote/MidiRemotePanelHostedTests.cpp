// MidiRemotePanelHostedTests.cpp -- FRO136 (docs/control/midi-remote-ui.md#plugin-build): the panel in
// HostMode::Hosted (the plugin build). Host MIDI is the one live controller and is always listed; the
// device UI is hidden; profiles for real devices are listed as "standalone only" and inert; Detect
// works from the host's own MIDI buffer. The fixture's AudioEngine is Hosted. Suite names contain
// "MidiRemote" per the ship-task --gtest_filter convention.
#include "MidiRemotePanelTestFixture.h"

#include "MidiRemote/ControllerProfileStore.h"

using midiremote_test::MidiRemotePanelLiveRefreshTest;
using synth::midi::hostSourceKey;
using synth::ui::ControllersListComponent;

namespace {

juce::Component* findById(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findById(*child, id))
            return found;
    return nullptr;
}

synth::Control makeControl(const juce::String& id, int number, const juce::String& name) {
    synth::Control c;
    c.id = id;
    c.name = name;
    c.message.type = synth::MessageType::cc;
    c.message.channel = 1;
    c.message.number = number;
    return c;
}

constexpr const char* kHostRowId = "host-midi";

class MidiRemotePanelHostedTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        panel_.setSize(900, 400);
        panel_.rebuildFromProfiles();
    }
    void TearDown() override {
        engine_->setRemoteMessageSink(nullptr);
        MidiRemotePanelLiveRefreshTest::TearDown();
    }

    synth::ControllerProfile realDeviceProfile() {
        synth::ControllerProfile p;
        p.id = "usb-1";
        p.name = "Launchkey Mini";
        p.input.identifier = "usb-launchkey";
        p.input.name = "Launchkey Mini MK3";
        p.controls = {makeControl("k", 21, "Knob 1")};
        return p;
    }
    const synth::ControllerProfile* hostProfile() {
        for (const auto& p : controller_->getProfiles())
            if (p.input.identifier == hostSourceKey())
                return &p;
        return nullptr;
    }
    const ControllersListComponent& list() { return panel_.getControllersListForTest(); }
};

} // namespace

TEST_F(MidiRemotePanelHostedTest, HostMidiIsListedBeforeAnyProfileExistsAndNothingIsWrittenYet) {
    EXPECT_EQ(list().getRowCountForTest(), 1);
    EXPECT_EQ(list().getRowDisplayNameForTest(kHostRowId), "Host MIDI");
    EXPECT_TRUE(controller_->getProfiles().empty()) << "listing the row must not create a profile";
    EXPECT_TRUE(synth::ControllerProfileStore(root_).loadAll().profiles.empty());
}

TEST_F(MidiRemotePanelHostedTest, SelectingTheHostMidiRowCreatesItsProfileUnderTheHostSourceKeyAndKeepsOneRow) {
    panel_.selectForTest(kHostRowId, "");

    ASSERT_NE(hostProfile(), nullptr);
    EXPECT_EQ(hostProfile()->id, kHostRowId);
    EXPECT_EQ(hostProfile()->name, "Host MIDI");
    EXPECT_EQ(synth::ControllerProfileStore(root_).loadAll().profiles.size(), 1u) << "persisted like any profile";

    panel_.rebuildFromProfiles();
    EXPECT_EQ(list().getRowCountForTest(), 1) << "the virtual row became the real one, not a second row";
    EXPECT_EQ(list().getSelectedProfileId(), kHostRowId);
}

TEST_F(MidiRemotePanelHostedTest, AProfileMadeByLearnSuppliesTheHostMidiRowSoNoSecondOneAppears) {
    synth::ControllerProfile learned;
    learned.id = "learned-uuid";
    learned.name = "Host MIDI";
    learned.input.identifier = hostSourceKey();
    ASSERT_TRUE(controller_->addProfile(learned));
    panel_.rebuildFromProfiles();

    EXPECT_EQ(list().getRowCountForTest(), 1);
    EXPECT_EQ(list().getRowDisplayNameForTest("learned-uuid"), "Host MIDI");
}

TEST_F(MidiRemotePanelHostedTest, DeviceUiIsHiddenAndRealDeviceProfilesAreStandaloneOnlyAndInert) {
    ASSERT_TRUE(controller_->addProfile(realDeviceProfile()));
    panel_.rebuildFromProfiles();

    auto* addButton = findById(panel_, "addControllerButton");
    ASSERT_NE(addButton, nullptr);
    EXPECT_FALSE(addButton->isVisible());

    EXPECT_EQ(list().getRowCountForTest(), 2) << "Host MIDI plus the standalone-only profile";
    EXPECT_EQ(list().getRowDisplayNameForTest("usb-1"), "Launchkey Mini (standalone only)");
    EXPECT_EQ(list().getRowDisplayNameForTest(kHostRowId), "Host MIDI");
    EXPECT_TRUE(list().getTooltipForProfile("usb-1").contains("do not fire inside a host"));

    // Viewable, but Detect is not offered on something that can never hear the host.
    panel_.selectForTest("usb-1", "");
    panel_.setDetectActive(true);
    EXPECT_FALSE(panel_.isDetectActive());
}

TEST_F(MidiRemotePanelHostedTest, AnOrphanControllerRowSaysAStandaloneAssignmentDoesNotFireInsideAHost) {
    auto orphan = realDeviceProfile();
    orphan.id = "orphan-1";
    ASSERT_TRUE(controller_->addProfile(orphan));
    ASSERT_EQ(controller_->assignControl("orphan-1", "k", synth::midi::PickTarget::parameter(node_->nodeID, "cutoff")),
              synth::midi::AssignStatus::assigned);
    ASSERT_TRUE(controller_->deleteProfile("orphan-1"));
    panel_.rebuildFromProfiles();

    EXPECT_TRUE(list().getTooltipForProfile("orphan-1").contains("does not fire inside a host"));
    panel_.selectForTest("orphan-1", "");
    ASSERT_TRUE(panel_.isOrphanViewShownForTest());
    EXPECT_TRUE(panel_.getOrphanViewForTest().getBodyTextForTest().contains("does not fire here"));
}

// The real path: the host's MidiBuffer goes through AudioEngine::processHostBlock -> RemoteEngine (as the
// Host MIDI source) -> the panel's activity drain -> a new cell.
TEST_F(MidiRemotePanelHostedTest, DetectWorksFromAHostBuffer) {
    engine_->prepareForHost(44100.0, 256, 0, 2);
    engine_->setRemoteMessageSink(&remoteEngine_);
    controller_->refreshSources();

    panel_.selectForTest(kHostRowId, "");
    panel_.setDetectActive(true);
    ASSERT_TRUE(panel_.isDetectActive());

    juce::AudioBuffer<float> audio(2, 256);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 64), 10);
    engine_->processHostBlock(audio, midi);
    remoteEngine_.drain();
    panel_.refreshActivity();

    ASSERT_NE(hostProfile(), nullptr);
    ASSERT_EQ(hostProfile()->controls.size(), 1u) << "Detect turned the host's CC 21 into a control";
    EXPECT_EQ(hostProfile()->controls[0].message.number, 21);
    EXPECT_EQ(synth::ControllerProfileStore(root_).loadAll().profiles[0].controls.size(), 1u);

    engine_->releaseFromHost();
}
