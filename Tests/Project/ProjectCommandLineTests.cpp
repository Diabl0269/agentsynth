// Concern: which project bundle (if any) a command line asks the app to open.
#include "Plugin/Hosting/PluginScanService.h"
#include "Project/ProjectCommandLine.h"
#include "ProjectBundle.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

using synth::projectBundleFromCommandLine;

class ProjectCommandLineTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = synth::userSettingsRootDirectory().getChildFile("agentsynth-commandline-tests");
        root.deleteRecursively();
        root.createDirectory();
        bundle = makeBundle("My Song");
    }
    void TearDown() override { root.deleteRecursively(); }

    // A bundle as far as ProjectBundle::isBundle is concerned: a .agsproj directory holding project.json.
    juce::File makeBundle(const juce::String& name) {
        const auto dir = root.getChildFile(name + synth::ProjectBundle::kBundleExtension);
        dir.createDirectory();
        dir.getChildFile(synth::ProjectBundle::kProjectFileName).replaceWithText("{}");
        return dir;
    }

    juce::File root;
    juce::File bundle;
};

TEST_F(ProjectCommandLineTest, AbsoluteBundlePathIsReturned) {
    const auto found = projectBundleFromCommandLine(juce::StringArray{bundle.getFullPathName()});
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, bundle);
}

TEST_F(ProjectCommandLineTest, NoArgumentsOrFlagsOnlyOpenNothing) {
    EXPECT_FALSE(projectBundleFromCommandLine(juce::StringArray{}).has_value());
    EXPECT_FALSE(projectBundleFromCommandLine(juce::StringArray{"-psn_0_12345", "--verbose"}).has_value());
    EXPECT_FALSE(projectBundleFromCommandLine(juce::String()).has_value());
}

TEST_F(ProjectCommandLineTest, FlagsAreSkippedAndAnyFlagValueIsNotAProject) {
    const auto found = projectBundleFromCommandLine(
        juce::StringArray{"-NSDocumentRevisionsDebugMode", "YES", "--flag", bundle.getFullPathName()});
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, bundle);
}

TEST_F(ProjectCommandLineTest, AnArgumentThatIsNotABundleOpensNothing) {
    EXPECT_FALSE(projectBundleFromCommandLine(juce::StringArray{root.getFullPathName()}).has_value());
    EXPECT_FALSE(projectBundleFromCommandLine(juce::StringArray{"missing.agsproj"}, root).has_value());

    // Right extension and directory, but no project.json inside.
    const auto empty = root.getChildFile("Empty.agsproj");
    empty.createDirectory();
    EXPECT_FALSE(projectBundleFromCommandLine(juce::StringArray{empty.getFullPathName()}).has_value());

    // A plain patch file is not a project.
    const auto patch = root.getChildFile("patch.json");
    patch.replaceWithText("{}");
    EXPECT_FALSE(projectBundleFromCommandLine(juce::StringArray{patch.getFullPathName()}).has_value());
}

TEST_F(ProjectCommandLineTest, RelativePathResolvesAgainstTheWorkingDirectory) {
    const auto found = projectBundleFromCommandLine(juce::StringArray{bundle.getFileName()}, root);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, bundle);
}

TEST_F(ProjectCommandLineTest, TheFirstBundleWins) {
    const auto other = makeBundle("Other");
    const auto found =
        projectBundleFromCommandLine(juce::StringArray{other.getFullPathName(), bundle.getFullPathName()});
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, other);
}

TEST_F(ProjectCommandLineTest, PluginScanChildArgumentsNeverOpenAProject) {
    EXPECT_FALSE(projectBundleFromCommandLine(juce::StringArray{synth::PluginScanService::kScanArgvFlag, "VST3",
                                                                bundle.getFullPathName(), "token"})
                     .has_value());
}

TEST_F(ProjectCommandLineTest, RawCommandLineStringHandlesQuotedPathsWithSpaces) {
    ASSERT_TRUE(bundle.getFullPathName().contains(" "));
    const auto found = projectBundleFromCommandLine("\"" + bundle.getFullPathName() + "\"");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, bundle);

    const auto unquoted = projectBundleFromCommandLine("-flag \"" + bundle.getFullPathName() + "\"");
    ASSERT_TRUE(unquoted.has_value());
    EXPECT_EQ(*unquoted, bundle);
}
