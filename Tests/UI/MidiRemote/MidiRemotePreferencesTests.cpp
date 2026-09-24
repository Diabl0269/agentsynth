// MidiRemotePreferencesTests.cpp -- FRO136 (docs/control/midi-remote-ui.md#settings): the two
// Preferences keys reach the things they govern live, through the settings-file broadcast
// MainComponent already listens to -- Default takeover into RemoteEngine (Core never reads
// settings), and the badge switch into the MIDI Learn badge painter every surface shares.
#include "../../FakeAudioIODevice.h"
#include "MainComponent/MainComponent.h"
#include "MidiRemote/ContinuousTarget.h"
#include "MidiRemote/MidiRemotePreferences.h"
#include "MidiRemoteMockProvider.h"
#include "MidiRemotePanelTestFixture.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UserSettings.h"

#include <gtest/gtest.h>
#include <tuple>
#include <vector>

namespace {

// MainComponent reads and writes the developer's real settings file, so the two keys go back exactly
// as they were (including "absent"), and the process-wide badge switch goes back to its default.
class MidiRemotePrefsKeysGuard {
public:
    explicit MidiRemotePrefsKeysGuard(juce::PropertiesFile& settings)
        : settings_(settings) {
        for (const char* key : {synth::kMidiRemoteDefaultTakeoverSettingKey, synth::kMidiRemoteShowBadgesSettingKey})
            saved_.emplace_back(key, settings.containsKey(key) ? settings.getValue(key) : juce::String(),
                                settings.containsKey(key));
    }
    ~MidiRemotePrefsKeysGuard() {
        for (const auto& [key, value, present] : saved_) {
            if (present)
                settings_.setValue(key, value);
            else
                settings_.removeValue(key);
        }
        settings_.saveIfNeeded();
        synth::ui::midilearn::setMappedBadgesVisible(true);
    }

private:
    juce::PropertiesFile& settings_;
    std::vector<std::tuple<juce::String, juce::String, bool>> saved_;
};

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); }

} // namespace

TEST(MidiRemotePreferencesTests, DefaultTakeoverReachesTheEngineLive) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    mc.setSize(1200, 800);
    auto& props = mc.getAppPropertiesForTest();
    MidiRemotePrefsKeysGuard guard(*props.getUserSettings());

    props.getUserSettings()->removeValue(synth::kMidiRemoteDefaultTakeoverSettingKey);
    mc.applyMidiRemotePreferences();
    EXPECT_EQ(mc.getRemoteEngineForTest().getDefaultTakeover(), synth::Takeover::scale) << "no key: Scale";

    PreferencesSettingsTab tab(props);
    tab.setMidiRemoteDefaultTakeover(synth::Takeover::jump);
    pump();
    EXPECT_EQ(mc.getRemoteEngineForTest().getDefaultTakeover(), synth::Takeover::jump)
        << "a Preferences change reaches the engine without a restart";

    tab.setMidiRemoteDefaultTakeover(synth::Takeover::pickup);
    pump();
    EXPECT_EQ(mc.getRemoteEngineForTest().getDefaultTakeover(), synth::Takeover::pickup);
}

// FRO236 (docs/control/midi-remote.md#continuous-targets): drives the REAL
// MainComponentRemoteActionInvoker wireMidiRemoteEngine() installs (rather than a test fake --
// see MidiLearnControllerTests.cpp/RemoteEngineNodeCommandE2ETests.cpp's own ToggleSoloInvoker for
// why those need one and this doesn't: there is no ChannelStripModule/undo bracket to fake here,
// just the transport) -- a mapped knob's CC really moves AudioEngine's own TransportService.
TEST(MidiRemotePreferencesTests, BpmContinuousTargetReachesTheRealTransportThroughTheInvoker) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    mc.setSize(1200, 800);

    // setBpm() only POSTS a command; only tick(), from the audio thread's device callback, drains it
    // into getPositionSnapshot().bpm (same fix MainComponentLayoutTests.cpp's
    // StatusBarPlayStopButtonDrivesTheSameTransport uses for play/stop).
    auto& audioEngine = mc.getAudioEngine();
    audioEngine.suspendDeviceCallback();
    synth::test::FakeAudioIODevice fake(2, 2);
    audioEngine.audioDeviceAboutToStart(&fake);
    const auto driveOneBlock = [&audioEngine] {
        constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;
        std::vector<float> left((std::size_t)kBlockSize, 0.0f), right((std::size_t)kBlockSize, 0.0f);
        std::vector<float> outLeft((std::size_t)kBlockSize, 0.0f), outRight((std::size_t)kBlockSize, 0.0f);
        const float* inputs[] = {left.data(), right.data()};
        float* outputs[] = {outLeft.data(), outRight.data()};
        audioEngine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
    };

    auto& engine = mc.getRemoteEngineForTest();
    engine.setSources({"test-source"});

    synth::Control control;
    control.id = "c";
    control.name = "c";
    control.kind = synth::ControlKind::knob;
    control.message.type = synth::MessageType::cc;
    control.message.channel = 1;
    control.message.number = 20;

    synth::ControllerProfile profile;
    profile.id = "p";
    profile.name = "p";
    profile.input.identifier = "test-source";
    profile.controls = {control};

    synth::Assignment a;
    a.id = "a1";
    a.control.profileId = "p";
    a.control.controlId = "c";
    a.spec = control.message;
    a.target.kind = synth::Target::Kind::continuous;
    a.target.continuous.kind = synth::ContinuousTargetKind::bpm;
    a.takeover = synth::Takeover::jump;

    engine.setProfiles({profile});
    engine.setAssignments({a});
    engine.reconcile(mc.getAudioEngine().getGraph());

    ASSERT_TRUE(engine.handleMessage("test-source", juce::MidiMessage::controllerEvent(1, 20, 0)));
    engine.drain();
    driveOneBlock();

    EXPECT_NEAR(audioEngine.getTransport().getPositionSnapshot().bpm, synth::kRemoteBpmWindowMin, 1e-2)
        << "CC 0 with Jump must reach the real transport's BPM through the real invoker";

    audioEngine.audioDeviceStopped();
}

TEST(MidiRemotePreferencesTests, TheStoredTakeoverIsAppliedAtLaunch) {
    {
        MainComponent seed(std::make_unique<MidiRemoteMockProvider>());
        MidiRemotePrefsKeysGuard guard(*seed.getAppPropertiesForTest().getUserSettings());
        seed.getAppPropertiesForTest().getUserSettings()->setValue(synth::kMidiRemoteDefaultTakeoverSettingKey,
                                                                   "pickup");
        seed.getAppPropertiesForTest().getUserSettings()->saveIfNeeded();

        MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
        EXPECT_EQ(mc.getRemoteEngineForTest().getDefaultTakeover(), synth::Takeover::pickup);
    }
}

TEST(MidiRemotePreferencesTests, BadgeSwitchReachesThePainterLive) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    mc.setSize(1200, 800);
    auto& props = mc.getAppPropertiesForTest();
    MidiRemotePrefsKeysGuard guard(*props.getUserSettings());

    props.getUserSettings()->removeValue(synth::kMidiRemoteShowBadgesSettingKey);
    mc.applyMidiRemotePreferences();
    EXPECT_TRUE(synth::ui::midilearn::areMappedBadgesVisible()) << "default on";

    PreferencesSettingsTab tab(props);
    tab.setMidiRemoteShowBadgesEnabled(false);
    pump();
    EXPECT_FALSE(synth::ui::midilearn::areMappedBadgesVisible());

    tab.setMidiRemoteShowBadgesEnabled(true);
    pump();
    EXPECT_TRUE(synth::ui::midilearn::areMappedBadgesVisible());
}

// ---- The painter itself ----------------------------------------------------------------------------------

namespace {
// Alpha of the pixel inside the 6 px dot at the top-right of a 20 x 20 control.
float dotAlphaAfter(bool viaDot) {
    juce::Image image(juce::Image::ARGB, 20, 20, true);
    juce::Graphics g(image);
    if (viaDot)
        synth::ui::midilearn::paintMidiMappedDot(g, {0, 0, 20, 20}, juce::Colours::red);
    else
        synth::ui::midilearn::paintMidiMappedBadge(g, {0, 0, 20, 20}, juce::Colours::red);
    return image.getPixelAt(17, 3).getFloatAlpha();
}
} // namespace

TEST(MidiRemotePreferencesTests, MappedBadgePaintsOnlyWhileTheSwitchIsOnButTheSurfaceDotAlwaysPaints) {
    struct Reset {
        ~Reset() { synth::ui::midilearn::setMappedBadgesVisible(true); }
    } reset;

    synth::ui::midilearn::setMappedBadgesVisible(true);
    EXPECT_GT(dotAlphaAfter(false), 0.5f);

    synth::ui::midilearn::setMappedBadgesVisible(false);
    EXPECT_EQ(dotAlphaAfter(false), 0.0f) << "switch off: no badge on the mapped control";
    EXPECT_GT(dotAlphaAfter(true), 0.5f) << "the panel's own surface cells are not decoration";
}

namespace {
ModuleComponent* findCard(juce::Component& root) {
    for (auto* child : root.getChildren()) {
        if (auto* card = dynamic_cast<ModuleComponent*>(child))
            return card;
        if (auto* found = findCard(*child))
            return found;
    }
    return nullptr;
}
} // namespace

// A module card paints from a cached image that only its own repaint() invalidates, so flipping the badge
// switch must reach each card directly: repainting the canvas around it would leave every badge as it was
// (found by checking the running app, not by the flag tests above).
TEST_F(MidiRemotePanelLiveRefreshTest, FlippingTheBadgeSwitchInvalidatesEachCardsCachedImage) {
    graphEditor_->setSize(800, 600);
    ModuleComponent* card = findCard(*graphEditor_);
    ASSERT_NE(card, nullptr);
    auto* cache = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(card->getCachedComponentImage());
    ASSERT_NE(cache, nullptr) << "cards are cached; if this changes, the test's premise has too";

    juce::Image target(juce::Image::ARGB, card->getWidth(), card->getHeight(), true);
    juce::Graphics g(target);
    cache->paint(g);
    const int rastersAfterFirstPaint = cache->getRasterCountForTest();
    cache->paint(g);
    ASSERT_EQ(cache->getRasterCountForTest(), rastersAfterFirstPaint) << "a second paint reuses the image";

    graphEditor_->repaintMidiLearnBadges();
    cache->paint(g);
    EXPECT_EQ(cache->getRasterCountForTest(), rastersAfterFirstPaint + 1) << "the card re-rasters after the flip";
}
