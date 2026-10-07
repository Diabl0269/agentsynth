#include "../App/MainComponent/MainComponentTestFixture.h"
#include "../TestSettingsHelpers.h"
#include "AudioEngine/AudioEngine.h"
#include "ShortcutManager/AppCommands.h"
#include "Telemetry/TelemetryService.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

// Topic: MainComponent's usage statistics wiring: nothing is recorded or written while the setting is off, the
// setting switches recording on and off live, and each user action reaches its counter once.

namespace {
using synth::telemetry::Feature;

class TelemetryWiringTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        keyGuard =
            std::make_unique<synth::test::PersistedKeysGuard>(juce::StringArray{synth::kShareUsageStatsSettingKey});
        cleanFiles();
        setShareSetting(false);
    }
    void TearDown() override {
        keyGuard.reset();
        cleanFiles();
        MainComponentTest::TearDown();
    }

    static void cleanFiles() {
        synth::telemetry::TelemetryIdStore().erase();
        synth::telemetry::TelemetryRecorder::defaultQueueFile().deleteFile();
    }

    static void setShareSetting(bool on) {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        props.getUserSettings()->setValue(synth::kShareUsageStatsSettingKey, on);
        props.getUserSettings()->saveIfNeeded();
    }

    static int featureCount(synth::telemetry::TelemetryService& service, Feature feature) {
        const auto& today = service.getRecorder().getToday();
        return today ? today->features[static_cast<size_t>(feature)] : 0;
    }

    static int moduleCount(synth::telemetry::TelemetryService& service, const char* name) {
        const auto& today = service.getRecorder().getToday();
        return today ? today->modules[static_cast<size_t>(synth::telemetry::moduleIndexForFactoryName(name))] : 0;
    }

    std::unique_ptr<synth::test::PersistedKeysGuard> keyGuard;
};
} // namespace

TEST_F(TelemetryWiringTest, OffByDefaultRecordsNothingAndLeavesNoFiles) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto* service = mc.getTelemetryServiceForTest();
    ASSERT_NE(service, nullptr);
    EXPECT_FALSE(service->getRecorder().isEnabled());

    mc.getGraphEditor().addModuleAtCanvasPosition("Oscillator", {100, 100}, {});
    EXPECT_FALSE(service->getRecorder().getToday().has_value());
    EXPECT_TRUE(synth::telemetry::TelemetryIdStore().load().isEmpty());
    EXPECT_FALSE(synth::telemetry::TelemetryRecorder::defaultQueueFile().existsAsFile());
}

TEST_F(TelemetryWiringTest, LaunchingWithTheSettingOffRemovesALeftoverId) {
    synth::telemetry::TelemetryIdStore().create();
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_TRUE(synth::telemetry::TelemetryIdStore().load().isEmpty());
}

TEST_F(TelemetryWiringTest, LaunchingOptedInCountsTheSessionAndUsesOneId) {
    setShareSetting(true);
    MainComponent mc(std::make_unique<MockProvider>());
    auto* service = mc.getTelemetryServiceForTest();
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->getRecorder().isEnabled());
    EXPECT_EQ(service->getRecorder().getToday()->sessions, 1);
    EXPECT_EQ(service->getRecorder().getTelemetryId(), synth::telemetry::TelemetryIdStore().load());
}

TEST_F(TelemetryWiringTest, AddingAModuleCountsItsTypeOnce) {
    setShareSetting(true);
    MainComponent mc(std::make_unique<MockProvider>());
    auto* service = mc.getTelemetryServiceForTest();
    mc.getGraphEditor().addModuleAtCanvasPosition("Oscillator", {100, 100}, {});
    mc.getGraphEditor().addModuleAtCanvasPosition("Amp Env", {300, 100}, {});
    EXPECT_EQ(moduleCount(*service, "Oscillator"), 1);
    EXPECT_EQ(moduleCount(*service, "Amp Env"), 1);
}

TEST_F(TelemetryWiringTest, AGraphLoadedFromAProjectCountsNoModules) {
    setShareSetting(true);
    MainComponent mc(std::make_unique<MockProvider>());
    auto* service = mc.getTelemetryServiceForTest();
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("telemetry-wiring", ".agsproj", false);
    ASSERT_TRUE(mc.saveProjectForTest(dir));
    EXPECT_EQ(featureCount(*service, Feature::PresetSaved), 1);
    ASSERT_TRUE(mc.openProjectForTest(dir));
    EXPECT_EQ(featureCount(*service, Feature::ProjectOpened), 1);
    int modules = 0;
    for (int count : service->getRecorder().getToday()->modules)
        modules += count;
    EXPECT_EQ(modules, 0) << "loading a project builds modules without a user adding them";
    dir.deleteRecursively();
}

TEST_F(TelemetryWiringTest, ExportingAPatchCountsASave) {
    setShareSetting(true);
    MainComponent mc(std::make_unique<MockProvider>());
    auto* service = mc.getTelemetryServiceForTest();
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("telemetry-patch", ".json", false);
    mc.exportPatchOnlyForTest(file);
    EXPECT_EQ(featureCount(*service, Feature::PresetSaved), 1);
    file.deleteFile();
}

TEST_F(TelemetryWiringTest, StartingTheTransportCountsTimelineUseOnlyWhenItActuallyStarts) {
    setShareSetting(true);
    MainComponent mc(std::make_unique<MockProvider>());
    mc.getAudioEngine().suspendDeviceCallback();
    auto* service = mc.getTelemetryServiceForTest();
    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::transportPlay, false));
    EXPECT_EQ(featureCount(*service, Feature::TimelineUsed), 1);
}

TEST_F(TelemetryWiringTest, TheSettingSwitchesRecordingOnAndOffWhileRunning) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto* service = mc.getTelemetryServiceForTest();
    ASSERT_FALSE(service->getRecorder().isEnabled());

    auto& settings = *mc.getAppPropertiesForTest().getUserSettings();
    synth::telemetry::TelemetryIdStore().create(); // what the Preferences toggle does first
    settings.setValue(synth::kShareUsageStatsSettingKey, true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    ASSERT_TRUE(service->getRecorder().isEnabled());
    mc.getGraphEditor().addModuleAtCanvasPosition("Oscillator", {100, 100}, {});
    EXPECT_EQ(moduleCount(*service, "Oscillator"), 1);

    settings.setValue(synth::kShareUsageStatsSettingKey, false);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    EXPECT_FALSE(service->getRecorder().isEnabled());
    EXPECT_FALSE(service->getRecorder().getToday().has_value());
    EXPECT_TRUE(synth::telemetry::TelemetryIdStore().load().isEmpty());
}
