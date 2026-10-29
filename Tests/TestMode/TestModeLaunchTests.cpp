// `--automation <port>` and its companions: parsing, the policies it implies, the settings directory it redirects, and
// what startForLaunch announces.

#include "../UniqueTempDir.h"
#include "AutomationLaunch.h"
#include "Project/ProjectCommandLine.h"
#include "ProjectBundle.h"
#include "SoundBrowser/LoopStretchFile.h"
#include "TestMode/TestModeLaunch.h"
#include "UI/Layout/ReducedMotion.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <iostream>
#include <sstream>

using namespace synth;

namespace {
// TestMain points the settings at a test directory; whatever a test here changes goes back afterwards.
struct RestoreSettingsDir {
    juce::String saved = detail::settingsDirOverride();
    bool savedFolders = detail::automationUserFolders();
    ~RestoreSettingsDir() {
        setSettingsDirOverride(saved);
        setAutomationUserFolders(savedFolders);
    }
};
} // namespace

TEST(TestModeLaunch, ParsesThePortAfterTheFlag) {
    EXPECT_EQ(automationPortFromArgs({"app", "--automation", "0"}), 0);
    EXPECT_EQ(automationPortFromArgs({"--automation", "51234", "--other"}), 51234);
    EXPECT_EQ(automationPortFromArgs({"--automation", "65535"}), 65535);
}

TEST(TestModeLaunch, RefusesAMissingOrBadPort) {
    EXPECT_FALSE(automationPortFromArgs({}).has_value());
    EXPECT_FALSE(automationPortFromArgs({"--automation"}).has_value());
    EXPECT_FALSE(automationPortFromArgs({"--automation", "abc"}).has_value());
    EXPECT_FALSE(automationPortFromArgs({"--automation", "65536"}).has_value());
    EXPECT_FALSE(automationPortFromArgs({"--automation", "-1"}).has_value());
    EXPECT_FALSE(automationPortFromArgs({"--automation", "--settings-dir"}).has_value());
}

TEST(TestModeLaunch, AutomationImpliesNoAudioDeviceAndNoKeychain) {
    const juce::StringArray args{"--automation", "0"};
    EXPECT_TRUE(isAutomationLaunch(args));
    EXPECT_TRUE(isNoAudioDeviceLaunch(args, {}));
    EXPECT_TRUE(isKeychainFreeLaunch(args, {}, {}));
    EXPECT_FALSE(isAutomationLaunch({"--no-audio-device"}));
    EXPECT_FALSE(isKeychainFreeLaunch({"--fullscreen"}, {}, {}));
}

TEST(TestModeLaunch, FlagValuesAreReadAfterTheirFlag) {
    const juce::StringArray args{"--automation", "0", "--settings-dir", "/x/y", "--automation-info", "info.json"};
    EXPECT_EQ(flagValueFromArgs(args, kSettingsDirFlag), "/x/y");
    EXPECT_EQ(flagValueFromArgs(args, kAutomationInfoFlag), "info.json");
    EXPECT_EQ(flagValueFromArgs({"--settings-dir"}, kSettingsDirFlag), "");
}

TEST(TestModeLaunch, FlagValuesAreNeverTakenForAProject) {
    // Regression test: a settings path or port after a flag must not be read as a project bundle to open.
    const auto dir =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("testmode-launch-args.agsproj");
    dir.createDirectory(); // exists, but is not a bundle (no project.json)
    const juce::StringArray args{"--automation", "0", "--settings-dir", dir.getFullPathName()};
    EXPECT_FALSE(projectBundleFromCommandLine(args).has_value());
    dir.deleteRecursively();
}

TEST(TestModeLaunch, SettingsDirFlagRedirectsAndRelativeOnesAreIgnored) {
    RestoreSettingsDir restore;
    const auto before = userSettingsRootDirectory();
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("testmode-launch-settings");
    dir.deleteRecursively();

    EXPECT_EQ(applySettingsDirForLaunch({"--settings-dir", "relative/dir"}), "");
    EXPECT_EQ(userSettingsRootDirectory(), before);

    EXPECT_EQ(applySettingsDirForLaunch({"--settings-dir", dir.getFullPathName()}), dir.getFullPathName());
    EXPECT_EQ(userSettingsRootDirectory(), dir);
    EXPECT_TRUE(dir.isDirectory());
    EXPECT_NE(userSettingsOptions().getDefaultFile().getFullPathName().indexOf(dir.getFullPathName()), -1);

    dir.deleteRecursively();
}

TEST(TestModeLaunch, AutomationWithoutASettingsDirGetsAFreshOneEachTime) {
    RestoreSettingsDir restore;
    const auto first = applySettingsDirForLaunch({"--automation", "0"});
    const auto second = applySettingsDirForLaunch({"--automation", "0"});
    EXPECT_TRUE(first.isNotEmpty());
    EXPECT_NE(first, second);
    EXPECT_TRUE(juce::File(first).isDirectory());
    EXPECT_EQ(applySettingsDirForLaunch({"--no-audio-device"}), "");
    juce::File(first).deleteRecursively();
    juce::File(second).deleteRecursively();
}

TEST(TestModeLaunch, StartAnnouncesThePortWritesTheInfoFileAndSilencesAnimation) {
    ASSERT_NE(synth::ui::animationMode(), synth::ui::AnimationMode::off);
    const auto info =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("testmode-launch-info.json");
    info.deleteFile();

    std::ostringstream captured;
    auto* oldBuf = std::cout.rdbuf(captured.rdbuf());
    juce::String error;
    testmode::LaunchOptions options;
    options.infoFile = info;
    auto server = testmode::startForLaunch(options, error);
    std::cout.rdbuf(oldBuf);

    ASSERT_NE(server, nullptr) << error;
    EXPECT_GT(server->getPort(), 0);
    EXPECT_EQ(synth::ui::animationMode(), synth::ui::AnimationMode::off);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);

    const auto line = juce::String(captured.str()).trim();
    EXPECT_TRUE(line.startsWith("AGENTSYNTH_AUTOMATION_PORT " + juce::String(server->getPort()) + " ")) << line;
    const auto json = juce::JSON::parse(info);
    EXPECT_EQ(static_cast<int>(json["port"]), server->getPort());
    EXPECT_GT(static_cast<int>(json["pid"]), 0);
    info.deleteFile();
}

TEST(TestModeLaunch, KeepAnimationsLeavesTheModeAlone) {
    std::ostringstream captured;
    auto* oldBuf = std::cout.rdbuf(captured.rdbuf());
    juce::String error;
    testmode::LaunchOptions options;
    options.keepAnimations = true;
    auto server = testmode::startForLaunch(options, error);
    std::cout.rdbuf(oldBuf);
    ASSERT_NE(server, nullptr) << error;
    EXPECT_NE(synth::ui::animationMode(), synth::ui::AnimationMode::off);
}

TEST(TestModeLaunch, OnlyAnAutomationLaunchRedirectsTheProjectsFolderNotTheTestsOverride) {
    RestoreSettingsDir restore;
    // TestMain's override is in force, yet the projects folder keeps its normal place.
    ASSERT_TRUE(hasSettingsDirOverride());
    ASSERT_FALSE(automationUserFolders());
    EXPECT_FALSE(ProjectBundle::getDefaultProjectsDirectory().isAChildOf(userSettingsRootDirectory()));

    const auto dir = synth::test::makeUniqueTempDir("agsauto");
    applySettingsDirForLaunch({"--automation", "0", "--settings-dir", dir.getFullPathName()});
    EXPECT_TRUE(automationUserFolders());
    EXPECT_TRUE(ProjectBundle::getDefaultProjectsDirectory().isAChildOf(dir));
    EXPECT_TRUE(synth::soundbrowser::stretchedLoopCacheFolder().isAChildOf(dir));

    // --settings-dir alone does not turn the redirect on.
    setAutomationUserFolders(false);
    applySettingsDirForLaunch({"--settings-dir", dir.getFullPathName()});
    EXPECT_FALSE(automationUserFolders());
    dir.deleteRecursively();
}
