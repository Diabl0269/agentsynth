// ControllerSurfaceDetectTests.cpp -- FRO134 (docs/control/midi-remote-ui.md#detect-mode): Detect
// mode end to end through the real seams -- RemoteEngine::handleMessage -> the activity ring ->
// MidiRemotePanelComponent::refreshActivity() (the ONE drain) -> the profile and the surface --
// plus the toolbar's own contract and DetectModeController's decisions. Suite names contain
// "MidiRemote" per the ship-task --gtest_filter convention.
#include "MidiRemotePanelTestFixture.h"
#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceToolbar.h"
#include "UI/MidiRemote/Detect/DetectModeController.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <cstdlib>

using midiremote_test::MidiRemotePanelLiveRefreshTest;
using synth::midi::hostSourceKey;

namespace {

class MidiRemoteDetectTest : public MidiRemotePanelLiveRefreshTest {
protected:
    // "Add controller" -> Empty for the host source, then Detect switched on.
    juce::String makeSelectedController(
        synth::ui::AddControllerPopover::StartWith start = synth::ui::AddControllerPopover::StartWith::detect) {
        synth::ui::AddControllerPopover::Choice choice;
        choice.deviceIdentifier = hostSourceKey();
        choice.deviceName = "Host MIDI";
        choice.profileName = "Test pad";
        choice.startWith = start;
        return panel_.createControllerFromChoice(choice);
    }

    // The message goes through the real engine entry point; Detect must never consume it.
    void touch(const juce::MidiMessage& message) {
        EXPECT_FALSE(remoteEngine_.handleMessage(hostSourceKey(), message)) << "Detect never consumes";
    }

    const synth::ControllerProfile& profile() { return controller_->getProfiles().front(); }
};

} // namespace

TEST_F(MidiRemoteDetectTest, CreatingWithDetectEntersDetectAndSelectsTheNewController) {
    const auto id = makeSelectedController();
    ASSERT_FALSE(id.isEmpty());
    EXPECT_TRUE(panel_.isDetectActive());
    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    EXPECT_EQ(profile().id, id);
    EXPECT_TRUE(profile().controls.empty());
}

TEST_F(MidiRemoteDetectTest, UnknownMessagesAddCellsInTouchOrderWithGuessedKindAndName) {
    makeSelectedController();

    touch(juce::MidiMessage::controllerEvent(1, 22, 40));
    touch(juce::MidiMessage::controllerEvent(1, 21, 90));
    touch(juce::MidiMessage::noteOn(10, 60, (juce::uint8)100));
    panel_.refreshActivity();

    ASSERT_EQ(profile().controls.size(), 3u);
    EXPECT_EQ(profile().controls[0].name, "CC 22") << "touch order, not number order";
    EXPECT_EQ(profile().controls[1].name, "CC 21");
    EXPECT_EQ(profile().controls[2].name, "C3");
    EXPECT_EQ(profile().controls[0].kind, synth::ControlKind::knob);
    EXPECT_EQ(profile().controls[2].kind, synth::ControlKind::pad);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(profile().controls[static_cast<std::size_t>(i)].layout.col, i);
        EXPECT_EQ(profile().controls[static_cast<std::size_t>(i)].layout.row, 0);
        EXPECT_GE(panel_.getSurfaceCellValueForTest(profile().controls[static_cast<std::size_t>(i)].id), 0.0f)
            << "each has a cell on the surface";
    }
}

TEST_F(MidiRemoteDetectTest, TheFirstTouchOfANewControlIsShownOnItsFreshCell) {
    makeSelectedController();
    touch(juce::MidiMessage::controllerEvent(1, 21, 127));
    panel_.refreshActivity();
    ASSERT_EQ(profile().controls.size(), 1u);
    EXPECT_NEAR(panel_.getSurfaceCellValueForTest(profile().controls[0].id), 1.0f, 1e-6f)
        << "a rebuilt cell starts at rest; the drain's events are replayed onto it";
}

TEST_F(MidiRemoteDetectTest, ANewCellPulsesUntilTheNextMessageArrivesThenTheExistingCellLights) {
    makeSelectedController();

    touch(juce::MidiMessage::controllerEvent(1, 21, 50));
    panel_.refreshActivity();
    const auto first = profile().controls[0].id;
    EXPECT_TRUE(panel_.isSurfaceCellHighlightedForTest(first)) << "pulsing";

    // The same control again: no new cell, the pulse ends and the cell lights (a flash).
    touch(juce::MidiMessage::controllerEvent(1, 21, 60));
    panel_.refreshActivity();
    EXPECT_EQ(profile().controls.size(), 1u);
    EXPECT_TRUE(panel_.isSurfaceCellHighlightedForTest(first)) << "lit";

    // A different control: the first stops pulsing for good once its flash has expired.
    touch(juce::MidiMessage::controllerEvent(1, 22, 60));
    panel_.refreshActivity();
    ASSERT_EQ(profile().controls.size(), 2u);
    EXPECT_TRUE(panel_.isSurfaceCellHighlightedForTest(profile().controls[1].id));
    juce::Thread::sleep(static_cast<int>(synth::ui::ControllerSurfaceCell::kDetectFlashMs) + 50);
    panel_.refreshActivity();
    EXPECT_FALSE(panel_.isSurfaceCellHighlightedForTest(first));
}

TEST_F(MidiRemoteDetectTest, DetectNeverAssignsAnything) {
    makeSelectedController();
    touch(juce::MidiMessage::controllerEvent(1, 21, 50));
    touch(juce::MidiMessage::noteOn(1, 36, (juce::uint8)90));
    panel_.refreshActivity();

    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_TRUE(doc_.controllers.empty());
    EXPECT_TRUE(profile().actions.empty());
    EXPECT_FALSE(undo_.canUndo()) << "profile edits are not project undo steps";
}

TEST_F(MidiRemoteDetectTest, ANoteOffOrAProgramChangeNeverCreatesACell) {
    makeSelectedController();
    touch(juce::MidiMessage::noteOff(1, 60));
    touch(juce::MidiMessage::programChange(1, 5));
    panel_.refreshActivity();
    EXPECT_TRUE(profile().controls.empty());
}

TEST_F(MidiRemoteDetectTest, LeavingDetectKeepsEverythingAndStopsAddingControls) {
    makeSelectedController();
    touch(juce::MidiMessage::controllerEvent(1, 21, 50));
    panel_.refreshActivity();
    ASSERT_EQ(profile().controls.size(), 1u);

    panel_.setDetectActive(false);
    EXPECT_FALSE(panel_.isDetectActive());
    EXPECT_EQ(profile().controls.size(), 1u) << "leaving Detect keeps what it found";
    EXPECT_FALSE(panel_.isSurfaceCellHighlightedForTest(profile().controls[0].id)) << "the pulse ends";

    touch(juce::MidiMessage::controllerEvent(1, 22, 50));
    panel_.refreshActivity();
    EXPECT_EQ(profile().controls.size(), 1u);
}

TEST_F(MidiRemoteDetectTest, DetectedControlsSurviveInTheStoreAndReloadIntoANewController) {
    makeSelectedController();
    touch(juce::MidiMessage::controllerEvent(1, 21, 50));
    panel_.refreshActivity();

    const auto reloaded = synth::ControllerProfileStore(root_).loadAll();
    ASSERT_EQ(reloaded.profiles.size(), 1u);
    ASSERT_EQ(reloaded.profiles[0].controls.size(), 1u);
    EXPECT_EQ(reloaded.profiles[0].controls[0].name, "CC 21");
}

TEST_F(MidiRemoteDetectTest, SelectingAnotherControllerTurnsDetectOff) {
    makeSelectedController();
    ASSERT_TRUE(panel_.isDetectActive());

    synth::ui::AddControllerPopover::Choice other;
    other.deviceIdentifier = "another-device";
    other.deviceName = "Another";
    other.profileName = "Another";
    other.startWith = synth::ui::AddControllerPopover::StartWith::empty;
    ASSERT_FALSE(panel_.createControllerFromChoice(other).isEmpty());
    EXPECT_FALSE(panel_.isDetectActive()) << "an Empty controller does not start Detect, and the first one's ended";
}

// A control the engine already routes (assigned) still lights the surface -- Detect does not
// interfere with the existing activity path.
TEST_F(MidiRemoteDetectTest, AnAssignedControlStillMovesItsCellWhileDetectIsOn) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    pump();
    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    const auto nodeUuid = node_->properties["uuid"].toString();
    ASSERT_TRUE(panel_.selectAssignmentForParameter(nodeUuid, "cutoff"));
    panel_.setDetectActive(true);

    const auto controlId = doc_.assignments.front().control.controlId;
    remoteEngine_.handleMessage(hostSourceKey(), juce::MidiMessage::controllerEvent(1, 20, 127));
    panel_.refreshActivity();
    EXPECT_EQ(profile().controls.size(), 1u) << "a known control adds nothing";
    EXPECT_NEAR(panel_.getSurfaceCellValueForTest(controlId), 1.0f, 1e-6f);
}

// ---- DetectModeController, headless ------------------------------------------------------------------

TEST(MidiRemoteDetectModeControllerTest, InactiveDoesNothing) {
    synth::ui::DetectModeController detect;
    synth::ControllerProfile profile;
    synth::midi::RemoteEvent event;
    event.specType = static_cast<std::uint8_t>(synth::MessageType::cc);
    event.specChannel = 1;
    event.specNumber = 21;
    const auto step = detect.handleEvent(profile, event);
    EXPECT_FALSE(step.controlAdded);
    EXPECT_TRUE(profile.controls.empty());
}

TEST(MidiRemoteDetectModeControllerTest, AddThenLightAndThePulseFollowsTheNewestControl) {
    synth::ui::DetectModeController detect;
    detect.setActive(true);
    synth::ControllerProfile profile;
    synth::midi::RemoteEvent event;
    event.specType = static_cast<std::uint8_t>(synth::MessageType::cc);
    event.specChannel = 1;
    event.specNumber = 21;

    auto step = detect.handleEvent(profile, event);
    ASSERT_TRUE(step.controlAdded);
    EXPECT_EQ(detect.getPulsingControlId(), profile.controls[0].id);

    step = detect.handleEvent(profile, event);
    EXPECT_FALSE(step.controlAdded);
    EXPECT_EQ(step.litControlId, profile.controls[0].id);
    EXPECT_TRUE(detect.getPulsingControlId().isEmpty()) << "the next message ends the pulse";

    detect.setActive(false);
    EXPECT_TRUE(detect.getPulsingControlId().isEmpty());
}

// ---- Toolbar ------------------------------------------------------------------------------------------------

namespace {
juce::Component* findById(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findById(*child, id))
            return found;
    return nullptr;
}
} // namespace

TEST(MidiRemoteSurfaceToolbarTest, HintRowAppearsOnlyInDetectWithTheDocsExactText) {
    synth::ui::ControllerSurfaceToolbar toolbar;
    toolbar.setSize(600, 60);
    toolbar.setProfileSelected(true);
    auto* hint = dynamic_cast<juce::Label*>(findById(toolbar, "detectHintLabel"));
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isVisible());
    const int idleHeight = toolbar.getPreferredHeight();

    toolbar.setDetectOn(true);
    EXPECT_TRUE(hint->isVisible());
    EXPECT_GT(toolbar.getPreferredHeight(), idleHeight);
    EXPECT_EQ(hint->getText(), "Touch each knob, fader and button once. Rename or retype them afterwards. "
                               "Turn an encoder left then right to detect its encoding.");
}

TEST(MidiRemoteSurfaceToolbarTest, DetectButtonTogglesAndReportsAndNeedsASelectedController) {
    synth::ui::ControllerSurfaceToolbar toolbar;
    auto* detect = dynamic_cast<juce::TextButton*>(findById(toolbar, "detectButton"));
    ASSERT_NE(detect, nullptr);
    EXPECT_FALSE(detect->isEnabled()) << "nothing selected";

    toolbar.setProfileSelected(true);
    EXPECT_TRUE(detect->isEnabled());
    std::vector<bool> reported;
    toolbar.onDetectToggled = [&](bool on) { reported.push_back(on); };
    detect->setToggleState(true, juce::dontSendNotification);
    detect->onClick();
    ASSERT_EQ(reported.size(), 1u);
    EXPECT_TRUE(reported[0]);
    EXPECT_TRUE(toolbar.isDetectOn());

    toolbar.setProfileSelected(false);
    EXPECT_FALSE(toolbar.isDetectOn()) << "losing the selection ends Detect";
}

// ---- Visual proxy ---------------------------------------------------------------------------------------------

// The whole panel mid-Detect (toolbar with the hint row, the Controllers list's "+ Add controller",
// freshly detected cells, one pulsing) rendered with the real theme, for a human/agent to look at:
// MIDI_PANEL_PNG=<path>. Always asserts the render has content.
TEST_F(MidiRemoteDetectTest, PanelRendersToPngWhileDetecting) {
    makeSelectedController();
    for (int cc : {21, 22, 23, 24, 25})
        touch(juce::MidiMessage::controllerEvent(1, cc, 20 + cc));
    touch(juce::MidiMessage::noteOn(10, 60, (juce::uint8)100));
    touch(juce::MidiMessage::noteOn(10, 62, (juce::uint8)100));
    panel_.refreshActivity();

    synth::theme::AppLookAndFeel lf;
    panel_.setLookAndFeel(&lf);
    panel_.setSize(940, 300);
    panel_.resized();

    juce::Image img(juce::Image::ARGB, panel_.getWidth(), panel_.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(panel_.paintEntireComponent(g, true));
    panel_.setLookAndFeel(nullptr);

    const char* pngPath = std::getenv("MIDI_PANEL_PNG");
    if (pngPath == nullptr || juce::String(pngPath).isEmpty())
        GTEST_SKIP() << "set MIDI_PANEL_PNG=<path> to write the rendered panel";
    juce::File outFile(pngPath);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    juce::FileOutputStream stream(outFile);
    ASSERT_TRUE(stream.openedOk());
    juce::PNGImageFormat png;
    ASSERT_TRUE(png.writeImageToStream(img, stream));
}
