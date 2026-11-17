// A MainComponent started as an automation launch (AGENTSYNTH_NO_AUDIO_DEVICE) renders on the silent device and never
// reads or writes the stored audio device setup (docs/development/test-mode.md#flags).

#include "AudioEngine/SilentAudioDevice.h"
#include "AutomationLaunch.h"
#include "MainComponentTestFixture.h"
#include "UserSettings.h"
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>

namespace {

constexpr const char* kStateKey = "audioDeviceState";
constexpr const char* kSentinel = "<DEVICESETUP deviceType=\"CoreAudio\" audioOutputDeviceName=\"Elsewhere\"/>";

#ifdef _WIN32
void setEnv(const char* name, const char* value) { _putenv_s(name, value); }
void clearEnv(const char* name) { _putenv_s(name, ""); }
#else
void setEnv(const char* name, const char* value) { setenv(name, value, 1); }
void clearEnv(const char* name) { unsetenv(name); }
#endif

class MainComponentSilentDeviceTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        {
            juce::ApplicationProperties props;
            props.setStorageParameters(synth::userSettingsOptions());
            if (auto* settings = props.getUserSettings()) {
                originalState_ = settings->getValue(kStateKey, juce::String());
                settings->setValue(kStateKey, kSentinel);
                settings->saveIfNeeded();
            }
        }
        setEnv(synth::kNoAudioDeviceEnvVar, "1");
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    }
    void TearDown() override {
        mc_.reset();
        clearEnv(synth::kNoAudioDeviceEnvVar);
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* settings = props.getUserSettings()) {
            if (originalState_.isEmpty())
                settings->removeValue(kStateKey);
            else
                settings->setValue(kStateKey, originalState_);
            settings->saveIfNeeded();
        }
        MainComponentTest::TearDown();
    }

    std::unique_ptr<MainComponent> mc_;
    juce::String originalState_;
};

} // namespace

TEST_F(MainComponentSilentDeviceTest, AnAutomationLaunchGetsTheSilentDeviceAndIgnoresTheStoredSetup) {
    auto& engine = mc_->getAudioEngine();
    EXPECT_TRUE(engine.usesSilentAudioDevice());
    EXPECT_FALSE(engine.hasSavedDeviceState()) << "the stored setup is never read into the engine";
    EXPECT_NE(dynamic_cast<synth::SilentAudioIODevice*>(engine.getDeviceManager().getCurrentAudioDevice()), nullptr);
    EXPECT_TRUE(engine.isReceivingDeviceCallbacks());
}

TEST_F(MainComponentSilentDeviceTest, ADeviceChangeIsNeverPersisted) {
    auto& manager = mc_->getAudioEngine().getDeviceManager();
    auto setup = manager.getAudioDeviceSetup();
    setup.bufferSize = setup.bufferSize == 256 ? 512 : 256;
    EXPECT_TRUE(manager.setAudioDeviceSetup(setup, true).isEmpty()); // an explicit choice, which broadcasts a state
    manager.sendSynchronousChangeMessage();

    auto* settings = mc_->getAppPropertiesForTest().getUserSettings();
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->getValue(kStateKey, juce::String()), juce::String(kSentinel));
}
