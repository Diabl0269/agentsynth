// MidiRemotePreferencesTests.cpp -- FRO136 (docs/control/midi-remote-ui.md#settings): the two
// Preferences keys reach the things they govern live, through the settings-file broadcast
// MainComponent already listens to -- Default takeover into RemoteEngine (Core never reads
// settings), and the badge switch into the MIDI Learn badge painter every surface shares.
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "MidiRemote/MidiRemotePreferences.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UserSettings.h"

#include <gtest/gtest.h>
#include <tuple>
#include <vector>

namespace {

class MockProviderMRPref : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMRPref"; }
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
    MainComponent mc(std::make_unique<MockProviderMRPref>());
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

TEST(MidiRemotePreferencesTests, TheStoredTakeoverIsAppliedAtLaunch) {
    {
        MainComponent seed(std::make_unique<MockProviderMRPref>());
        MidiRemotePrefsKeysGuard guard(*seed.getAppPropertiesForTest().getUserSettings());
        seed.getAppPropertiesForTest().getUserSettings()->setValue(synth::kMidiRemoteDefaultTakeoverSettingKey,
                                                                   "pickup");
        seed.getAppPropertiesForTest().getUserSettings()->saveIfNeeded();

        MainComponent mc(std::make_unique<MockProviderMRPref>());
        EXPECT_EQ(mc.getRemoteEngineForTest().getDefaultTakeover(), synth::Takeover::pickup);
    }
}

TEST(MidiRemotePreferencesTests, BadgeSwitchReachesThePainterLive) {
    MainComponent mc(std::make_unique<MockProviderMRPref>());
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
