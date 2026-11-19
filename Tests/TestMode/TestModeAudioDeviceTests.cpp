// audioDevice on a main window whose engine renders through the silent device
// (docs/development/test-mode.md#audio-device): rate, block size and latency changes, a restart, and `require`.

#include "../App/MainComponent/MainComponentTestFixture.h"
#include "../TestWait.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/SilentAudioDevice.h"
#include "AutomationLaunch.h"
#include "TestModeRig.h"
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>

using namespace testmode_rig;
using synth::testmode::StepResult;

namespace {

#ifdef _WIN32
void setEnv(const char* name, const char* value) { _putenv_s(name, value); }
void clearEnv(const char* name) { _putenv_s(name, ""); }
#else
void setEnv(const char* name, const char* value) { setenv(name, value, 1); }
void clearEnv(const char* name) { unsetenv(name); }
#endif

class AudioDeviceTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        setEnv(synth::kNoAudioDeviceEnvVar, "1");
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        window_ = std::make_unique<juce::DocumentWindow>("test", juce::Colours::black, 0, false);
        window_->setContentNonOwned(mc_.get(), true);
    }
    void TearDown() override {
        window_.reset();
        mc_.reset();
        clearEnv(synth::kNoAudioDeviceEnvVar);
        MainComponentTest::TearDown();
    }
    bool show() {
        if (nativeWindowsAbortHere())
            return false;
        window_->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        window_->setVisible(true);
        return window_->isOnDesktop();
    }
    std::unique_ptr<MainComponent> mc_;
    std::unique_ptr<juce::DocumentWindow> window_;
};

} // namespace

TEST_F(AudioDeviceTest, RateAndBlockChangesAreSeenByTheDeviceAndTheEngine) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    const auto done = run(
        "audioDevice", args({{"rate", 48000}, {"block", 128}, {"require", args({{"rate", 48000}, {"block", 128}})}}));
    ASSERT_EQ(done.state, StepResult::State::done) << done.error;
    EXPECT_EQ((double)done.value["rate"], 48000.0);
    EXPECT_EQ((int)done.value["block"], 128);
    EXPECT_EQ((double)done.value["engineRate"], 48000.0);
    EXPECT_EQ((int)done.value["engineBlock"], 128);
    EXPECT_TRUE(mc_->getAudioEngine().isReceivingDeviceCallbacks());
}

TEST_F(AudioDeviceTest, ALatencyChangeIsSeenByTheEngineAfterTheForcedRestart) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    auto& engine = mc_->getAudioEngine();
    EXPECT_EQ(engine.getOutputLatencySamples(), 0);
    const auto before = (int)run("audioDevice", args({})).value["restarts"];
    const auto done = run("audioDevice", args({{"latency", 480}, {"require", args({{"latency", 480}})}}));
    ASSERT_EQ(done.state, StepResult::State::done) << done.error;
    EXPECT_EQ((int)done.value["latency"], 480);
    EXPECT_EQ((int)done.value["engineLatency"], 480);
    EXPECT_EQ((int)done.value["restarts"], before + 1);

    const auto again = run("audioDevice", args({{"rate", 48000}, {"require", args({{"latency", 480}})}}));
    EXPECT_EQ(again.state, StepResult::State::done) << "later devices keep the latency: " << again.error;
}

TEST_F(AudioDeviceTest, ARestartKeepsBlocksAdvancingAndCountsIt) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    const auto before = (int)run("audioDevice", args({})).value["restarts"];
    const auto done = run("audioDevice", args({{"restart", true}}));
    ASSERT_EQ(done.state, StepResult::State::done) << done.error;
    EXPECT_EQ((int)done.value["restarts"], before + 1);
    EXPECT_TRUE(mc_->getAudioEngine().isReceivingDeviceCallbacks());
    const auto blocks = (juce::int64)done.value["blocks"];
    synth::test::waitUntil([&] { return (juce::int64)run("audioDevice", args({})).value["blocks"] > blocks + 2; },
                           60000);
    EXPECT_GT((juce::int64)run("audioDevice", args({})).value["blocks"], blocks + 2);
}

TEST_F(AudioDeviceTest, ARequireThatTheDeviceDoesNotMeetFails) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    const auto failed = run("audioDevice", args({{"require", args({{"rate", 96000}})}}));
    EXPECT_EQ(failed.state, StepResult::State::failed);
    EXPECT_TRUE(failed.error.contains("rate wanted 96000")) << failed.error;
}

TEST_F(AudioDeviceTest, BadArgumentsFail) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    EXPECT_EQ(run("audioDevice", args({{"sampleRate", 48000}})).state, StepResult::State::failed);
    EXPECT_EQ(run("audioDevice", args({{"rate", 0}})).state, StepResult::State::failed);
    EXPECT_EQ(run("audioDevice", args({{"block", "big"}})).state, StepResult::State::failed);
    EXPECT_EQ(run("audioDevice", args({{"latency", -1}})).state, StepResult::State::failed);
    EXPECT_EQ(run("audioDevice", args({{"require", args({{"bogus", 1}})}})).state, StepResult::State::failed);
    EXPECT_EQ(run("audioDevice", args({})).state, StepResult::State::done) << "an empty audioDevice only reports";
}
