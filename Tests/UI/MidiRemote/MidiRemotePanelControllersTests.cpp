// MidiRemotePanelControllersTests.cpp -- FRO134: the panel's controller-level flows -- Add
// controller (create + open + select), Templates, Import/Export round trip, the "+ Add controller"
// footer's Hosted rule, and the Inspector's encoder auto-detect / name / kind / encoding edits
// reaching the profile AND the assignments' denormalised copies. Suite names contain "MidiRemote"
// per the ship-task --gtest_filter convention.
#include "MidiRemotePanelTestFixture.h"

using midiremote_test::MidiRemotePanelLiveRefreshTest;
using synth::midi::hostSourceKey;
using synth::ui::AddControllerPopover;
using synth::ui::MidiRemotePanelComponent;

namespace {

juce::Component* findById(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findById(*child, id))
            return found;
    return nullptr;
}

class MidiRemotePanelControllersTest : public MidiRemotePanelLiveRefreshTest {
protected:
    juce::String create(AddControllerPopover::StartWith start, const juce::String& templateId = {},
                        const juce::String& deviceId = hostSourceKey()) {
        AddControllerPopover::Choice choice;
        choice.deviceIdentifier = deviceId;
        choice.deviceName = "Test device";
        choice.profileName = "Test device";
        choice.startWith = start;
        choice.templateId = templateId;
        return panel_.createControllerFromChoice(choice);
    }
    // A scratch file OUTSIDE the profile store's directory (a *.json inside it would load as a profile).
    juce::File scratchFile(const juce::String& name) {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("agentsynth-midiremote-scratch-" + juce::Uuid().toString());
        dir.createDirectory();
        scratchDirs_.add(dir);
        return dir.getChildFile(name);
    }
    void TearDown() override {
        for (const auto& dir : scratchDirs_)
            dir.deleteRecursively();
        MidiRemotePanelLiveRefreshTest::TearDown();
    }
    juce::Array<juce::File> scratchDirs_;

    const synth::ControllerProfile* find(const juce::String& id) {
        for (const auto& p : controller_->getProfiles())
            if (p.id == id)
                return &p;
        return nullptr;
    }
};

} // namespace

// ---- Add controller -----------------------------------------------------------------------------------

TEST_F(MidiRemotePanelControllersTest, AddWithEmptyCreatesAnEmptyStoredProfileAndSelectsItWithoutDetect) {
    const auto id = create(AddControllerPopover::StartWith::empty);
    ASSERT_FALSE(id.isEmpty());
    const auto* profile = find(id);
    ASSERT_NE(profile, nullptr);
    EXPECT_TRUE(profile->controls.empty());
    EXPECT_EQ(profile->input.identifier, hostSourceKey());
    EXPECT_FALSE(panel_.isDetectActive());
    EXPECT_EQ(synth::ControllerProfileStore(root_).loadAll().profiles.size(), 1u) << "written to disk";
    EXPECT_EQ(panel_.getControllersListRowCountForTest(), 1);
}

TEST_F(MidiRemotePanelControllersTest, AddWithATemplateStartsFromItsLayoutAndDoesNotEnterDetect) {
    const auto id = create(AddControllerPopover::StartWith::templateLayout, "template-transport-strip");
    const auto* profile = find(id);
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->controls.size(), 4u);
    EXPECT_FALSE(panel_.isDetectActive());
}

TEST_F(MidiRemotePanelControllersTest, AddRefusesADeviceThatAlreadyHasAProfileAndAnEmptyIdentifier) {
    ASSERT_FALSE(create(AddControllerPopover::StartWith::empty).isEmpty());
    // Same device again: the popover greys it; the panel must not create a second profile either
    // if a caller bypasses that.
    EXPECT_TRUE(create(AddControllerPopover::StartWith::empty, {}, "").isEmpty());
}

TEST_F(MidiRemotePanelControllersTest, TheAddButtonIsHiddenInHostedModeWhereTheListIsOnlyHostMidi) {
    // The fixture's engine is HostMode::Hosted.
    auto* button = findById(panel_, "addControllerButton");
    ASSERT_NE(button, nullptr);
    panel_.rebuildFromProfiles();
    EXPECT_FALSE(button->isVisible());
}

// ---- Templates ----------------------------------------------------------------------------------------------

TEST_F(MidiRemotePanelControllersTest, ATemplateAppliesToAnEmptyProfile) {
    const auto id = create(AddControllerPopover::StartWith::empty);
    EXPECT_EQ(panel_.applyTemplateToSelectedProfile("template-8-knobs"), 8);
    EXPECT_EQ(find(id)->controls.size(), 8u);
    EXPECT_EQ(synth::ControllerProfileStore(root_).loadAll().profiles[0].controls.size(), 8u);
}

TEST_F(MidiRemotePanelControllersTest, ATemplateMergesIntoANonEmptyProfileExistingMessageKeysWin) {
    const auto id = create(AddControllerPopover::StartWith::detect);
    remoteEngine_.handleMessage(hostSourceKey(), juce::MidiMessage::controllerEvent(1, 21, 10));
    panel_.refreshActivity();
    ASSERT_EQ(find(id)->controls.size(), 1u);
    const auto keptId = find(id)->controls[0].id;

    // The 8-knob template's CC 21 is channel "any"; the detected one is channel 1 -- a different
    // message key, so nothing is skipped and eight are added.
    EXPECT_EQ(panel_.applyTemplateToSelectedProfile("template-8-knobs"), 8);
    EXPECT_EQ(find(id)->controls.size(), 9u);
    EXPECT_EQ(find(id)->controls[0].id, keptId);
    // Applying it again finds every key already present.
    EXPECT_EQ(panel_.applyTemplateToSelectedProfile("template-8-knobs"), 0);
}

TEST_F(MidiRemotePanelControllersTest, TemplateWithNoControllerOrAnUnknownIdReportsMinusOne) {
    EXPECT_EQ(panel_.applyTemplateToSelectedProfile("template-8-knobs"), -1);
    create(AddControllerPopover::StartWith::empty);
    EXPECT_EQ(panel_.applyTemplateToSelectedProfile("no-such-template"), -1);
}

// ---- Import / export ---------------------------------------------------------------------------------------

TEST_F(MidiRemotePanelControllersTest, ExportThenDeleteThenImportRoundTripsTheProfile) {
    const auto id = create(AddControllerPopover::StartWith::templateLayout, "template-8-knobs");
    const auto file = scratchFile("exported.json");
    ASSERT_TRUE(controller_->exportProfile(id, file));
    ASSERT_TRUE(controller_->deleteProfile(id));
    ASSERT_EQ(controller_->getProfiles().size(), 0u);

    EXPECT_EQ(panel_.importControllerFileNow(file, false), MidiRemotePanelComponent::ImportOutcome::imported);
    const auto* imported = find(id);
    ASSERT_NE(imported, nullptr);
    EXPECT_EQ(imported->controls.size(), 8u);
    EXPECT_EQ(imported->name, "Test device");
    EXPECT_EQ(synth::ControllerProfileStore(root_).loadAll().profiles.size(), 1u);
}

TEST_F(MidiRemotePanelControllersTest, ImportOfAKnownIdConflictsUntilTheUserAgreesToReplace) {
    const auto id = create(AddControllerPopover::StartWith::empty);
    const auto file = scratchFile("exported.json");
    ASSERT_TRUE(controller_->exportProfile(id, file));

    auto renamed = *find(id);
    renamed.name = "Renamed locally";
    ASSERT_TRUE(controller_->updateProfile(renamed));

    EXPECT_EQ(panel_.importControllerFileNow(file, false), MidiRemotePanelComponent::ImportOutcome::conflict);
    EXPECT_EQ(find(id)->name, "Renamed locally") << "a conflict changes nothing";

    // The prompt flow: cancel keeps, OK replaces.
    std::function<void(bool)> answer;
    bool cancellable = false;
    panel_.setPromptHookForTest([&](const juce::String&, const juce::String&, bool c, std::function<void(bool)> done) {
        cancellable = c;
        answer = std::move(done);
    });
    panel_.importControllerFile(file);
    ASSERT_TRUE(answer);
    EXPECT_TRUE(cancellable);
    answer(false);
    EXPECT_EQ(find(id)->name, "Renamed locally");

    panel_.importControllerFile(file);
    answer(true);
    EXPECT_EQ(find(id)->name, "Test device") << "replaced by the imported document";
}

TEST_F(MidiRemotePanelControllersTest, ImportOfANonProfileFileIsInvalidAndSaysSo) {
    const auto bad = scratchFile("bad.json");
    ASSERT_TRUE(bad.replaceWithText("{\"hello\": 1}"));
    EXPECT_EQ(panel_.importControllerFileNow(bad, false), MidiRemotePanelComponent::ImportOutcome::invalid);

    juce::String shown;
    panel_.setPromptHookForTest(
        [&](const juce::String&, const juce::String& message, bool, std::function<void(bool)>) { shown = message; });
    panel_.importControllerFile(bad);
    EXPECT_TRUE(shown.contains("isn't a valid controller profile"));
    EXPECT_TRUE(controller_->getProfiles().empty());
}

// ---- Inspector edits reach the profile and every assignment copy -------------------------------------------

TEST_F(MidiRemotePanelControllersTest, EncoderAutoDetectSetsTheEncodingKindAndTheAssignmentCopy) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    pump();
    const auto nodeUuid = node_->properties["uuid"].toString();
    ASSERT_TRUE(panel_.selectAssignmentForParameter(nodeUuid, "cutoff"));
    ASSERT_EQ(doc_.assignments.size(), 1u);
    const auto control = controller_->getProfiles().front().controls.front();
    ASSERT_EQ(control.encoding, synth::Encoding::abs7);

    std::function<void(bool)> answer;
    int prompts = 0;
    panel_.setPromptHookForTest([&](const juce::String&, const juce::String&, bool, std::function<void(bool)> done) {
        ++prompts;
        answer = std::move(done);
    });
    panel_.refreshActivity(); // drain what the learn left on the activity ring, as the live tick would
    panel_.beginEncoderAutoDetect(control);
    ASSERT_EQ(prompts, 1) << "'turn it left...'";

    // Two's complement: 127 going left, 1 going right. The events are sent WHILE the prompt is up.
    remoteEngine_.handleMessage(hostSourceKey(), juce::MidiMessage::controllerEvent(1, 20, 127));
    remoteEngine_.handleMessage(hostSourceKey(), juce::MidiMessage::controllerEvent(1, 20, 126));
    panel_.refreshActivity();
    answer(true);
    ASSERT_EQ(prompts, 2) << "'now right...'";
    remoteEngine_.handleMessage(hostSourceKey(), juce::MidiMessage::controllerEvent(1, 20, 1));
    remoteEngine_.handleMessage(hostSourceKey(), juce::MidiMessage::controllerEvent(1, 20, 2));
    panel_.refreshActivity();
    answer(true);

    const auto& edited = controller_->getProfiles().front().controls.front();
    EXPECT_EQ(edited.encoding, synth::Encoding::relTwos);
    EXPECT_EQ(edited.kind, synth::ControlKind::encoder) << "a relative result retypes a plain knob";
    EXPECT_EQ(doc_.assignments.front().specEncoding, synth::Encoding::relTwos)
        << "the engine reads the assignment's copy";

    // And the engine really decodes it relatively now.
    remoteEngine_.drain();
    remoteEngine_.handleMessage(hostSourceKey(), juce::MidiMessage::controllerEvent(1, 20, 127));
    std::vector<synth::midi::RemoteEvent> events;
    remoteEngine_.drainActivity([&](const juce::String&, const synth::midi::RemoteEvent& e) { events.push_back(e); });
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().kind, synth::midi::RemoteEventKind::relativeDelta);
}

TEST_F(MidiRemotePanelControllersTest, AutoDetectThatCannotTellSaysSoAndChangesNothing) {
    const auto id = create(AddControllerPopover::StartWith::detect);
    remoteEngine_.handleMessage(hostSourceKey(), juce::MidiMessage::controllerEvent(1, 21, 10));
    panel_.refreshActivity();
    const auto control = find(id)->controls.front();

    std::function<void(bool)> answer;
    juce::String lastMessage;
    panel_.setPromptHookForTest([&](const juce::String&, const juce::String& m, bool, std::function<void(bool)> done) {
        lastMessage = m;
        answer = std::move(done);
    });
    panel_.beginEncoderAutoDetect(control);
    answer(true); // no left turns at all
    answer(true); // no right turns either
    EXPECT_TRUE(lastMessage.contains("Couldn't tell"));
    EXPECT_EQ(find(id)->controls.front().encoding, synth::Encoding::abs7);
}

TEST_F(MidiRemotePanelControllersTest, CancellingTheFirstPromptEndsAutoDetect) {
    const auto id = create(AddControllerPopover::StartWith::detect);
    remoteEngine_.handleMessage(hostSourceKey(), juce::MidiMessage::controllerEvent(1, 21, 10));
    panel_.refreshActivity();

    std::function<void(bool)> answer;
    panel_.setPromptHookForTest([&](const juce::String&, const juce::String&, bool, std::function<void(bool)> done) {
        answer = std::move(done);
    });
    panel_.beginEncoderAutoDetect(find(id)->controls.front());
    answer(false);
    EXPECT_EQ(panel_.getEncoderAutoDetectForTest().phase(), synth::midi::EncoderAutoDetect::Phase::idle);
}
