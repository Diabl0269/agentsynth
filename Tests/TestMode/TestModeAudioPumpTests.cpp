// audioPump and feedVisual on a main window whose engine renders through the silent device
// (docs/development/test-mode.md#audio-pump).

#include "../App/MainComponent/MainComponentTestFixture.h"
#include "../TestWait.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/SilentAudioDevice.h"
#include "AutomationLaunch.h"
#include "TestMode/TestModeFeedVisual.h"
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

class AudioPumpTest : public MainComponentTest {
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

TEST_F(AudioPumpTest, RunReportsTheDeviceBlockCounterAndStopChangesNothing) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    const auto started = run("audioPump", args({{"run", true}}));
    ASSERT_EQ(started.state, StepResult::State::done) << started.error;
    EXPECT_TRUE((bool)started.value["running"]);

    const auto before = (juce::int64)started.value["blocks"];
    synth::test::waitUntil(
        [&] { return (juce::int64)run("audioPump", args({{"run", true}})).value["blocks"] > before; });
    const auto stopped = run("audioPump", args({{"run", false}}));
    ASSERT_EQ(stopped.state, StepResult::State::done) << stopped.error;
    EXPECT_TRUE((bool)stopped.value["running"]) << "the device keeps running, as in the app";
    EXPECT_GT((juce::int64)stopped.value["blocks"], before);
    EXPECT_TRUE(mc_->getAudioEngine().isReceivingDeviceCallbacks());
}

TEST_F(AudioPumpTest, ANoteIsAcceptedThroughTheMidiSeam) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    EXPECT_EQ(run("audioPump", args({{"note", 60}, {"down", true}})).state, StepResult::State::done);
    EXPECT_EQ(run("audioPump", args({{"note", 60}, {"down", false}})).state, StepResult::State::done);
    EXPECT_EQ(run("audioPump", args({{"down", true}})).state, StepResult::State::failed);
}

TEST_F(AudioPumpTest, FeedVisualDetachesTheDeviceCallbackAndRunReattachesIt) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    auto& engine = mc_->getAudioEngine();
    ASSERT_TRUE(engine.isReceivingDeviceCallbacks());

    const auto fed = run("feedVisual", args({{"id", "card:Filter"}, {"kind", "sine"}}));
    ASSERT_EQ(fed.state, StepResult::State::done) << fed.error;
    EXPECT_TRUE((bool)fed.value["audioSuspended"]);
    EXPECT_FALSE(engine.isReceivingDeviceCallbacks()) << "the display holds: nothing else writes the buffers";

    ASSERT_EQ(run("audioPump", args({{"run", true}})).state, StepResult::State::done);
    EXPECT_TRUE(engine.isReceivingDeviceCallbacks());
}

TEST_F(AudioPumpTest, RunDoesNotReattachADetachFeedVisualDidNotMake) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    auto& engine = mc_->getAudioEngine();
    ASSERT_TRUE(engine.suspendDeviceCallback()); // as a bounce does
    EXPECT_FALSE(synth::testmode::feedVisualDetachedDevice());

    ASSERT_EQ(run("audioPump", args({{"run", true}})).state, StepResult::State::done);
    EXPECT_FALSE(engine.isReceivingDeviceCallbacks()) << "run:true must leave a bounce's detach alone";

    engine.resumeDeviceCallback();
    EXPECT_TRUE(engine.isReceivingDeviceCallbacks());
}

TEST_F(AudioPumpTest, RunReattachesOnlyWhatFeedVisualDetached) {
    if (!show())
        GTEST_SKIP() << "no native window in this environment";
    auto& engine = mc_->getAudioEngine();
    ASSERT_EQ(run("feedVisual", args({{"id", "card:Filter"}, {"kind", "sine"}})).state, StepResult::State::done);
    EXPECT_TRUE(synth::testmode::feedVisualDetachedDevice());

    ASSERT_EQ(run("audioPump", args({{"run", true}})).state, StepResult::State::done);
    EXPECT_TRUE(engine.isReceivingDeviceCallbacks());
    EXPECT_FALSE(synth::testmode::feedVisualDetachedDevice()) << "the re-attach clears the note";

    engine.suspendDeviceCallback(); // a later detach by someone else
    ASSERT_EQ(run("audioPump", args({{"run", true}})).state, StepResult::State::done);
    EXPECT_FALSE(engine.isReceivingDeviceCallbacks());
    engine.resumeDeviceCallback();
}
