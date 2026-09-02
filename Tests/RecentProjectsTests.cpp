#include "../Source/RecentProjects.h"
#include "../Source/UserSettings.h"
#include <gtest/gtest.h>
#include <juce_data_structures/juce_data_structures.h>

// P8-3: the Load menu's "Recent Projects" list. See docs/architecture.md's "Recent projects"
// subsection for the owner-drives-persistence shape this mirrors from PluginScanService.

using synth::RecentProjects;

namespace {

// A directory that actually exists on disk, so pruneMissing() (which checks isDirectory()) keeps
// it — matches what a real .agsproj bundle directory looks like to this class.
juce::File makeBundleDir(const juce::File& root, const juce::String& name) {
    auto dir = root.getChildFile(name);
    dir.createDirectory();
    return dir;
}

} // namespace

class RecentProjectsTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempRoot =
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-recent-projects-tests");
        tempRoot.deleteRecursively();
        tempRoot.createDirectory();
    }
    void TearDown() override { tempRoot.deleteRecursively(); }

    juce::File tempRoot;
};

TEST_F(RecentProjectsTest, AddProjectPutsTheNewestEntryFirst) {
    RecentProjects recent;
    auto a = makeBundleDir(tempRoot, "A.agsproj");
    auto b = makeBundleDir(tempRoot, "B.agsproj");

    recent.addProject(a);
    recent.addProject(b);

    const auto entries = recent.getEntries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0], b);
    EXPECT_EQ(entries[1], a);
}

TEST_F(RecentProjectsTest, ReAddingAnExistingEntryMovesItToTheFrontWithoutDuplicating) {
    RecentProjects recent;
    auto a = makeBundleDir(tempRoot, "A.agsproj");
    auto b = makeBundleDir(tempRoot, "B.agsproj");
    auto c = makeBundleDir(tempRoot, "C.agsproj");

    recent.addProject(a);
    recent.addProject(b);
    recent.addProject(c);
    recent.addProject(a); // re-open A: should move back to the front, not add a second entry

    const auto entries = recent.getEntries();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0], a);
    EXPECT_EQ(entries[1], c);
    EXPECT_EQ(entries[2], b);
}

TEST_F(RecentProjectsTest, AddProjectIgnoresAnEmptyFile) {
    RecentProjects recent;
    recent.addProject(juce::File());
    EXPECT_TRUE(recent.getEntries().empty());
}

TEST_F(RecentProjectsTest, ListIsCappedAtMaxEntriesDroppingTheOldest) {
    RecentProjects recent;
    std::vector<juce::File> added;
    for (int i = 0; i < RecentProjects::kMaxEntries + 3; ++i) {
        auto dir = makeBundleDir(tempRoot, "Project" + juce::String(i) + ".agsproj");
        added.push_back(dir);
        recent.addProject(dir);
    }

    const auto entries = recent.getEntries();
    ASSERT_EQ(entries.size(), (size_t)RecentProjects::kMaxEntries);
    // Most-recent-first: the last kMaxEntries added, newest first.
    for (int i = 0; i < RecentProjects::kMaxEntries; ++i)
        EXPECT_EQ(entries[(size_t)i], added[added.size() - 1 - (size_t)i]);
}

TEST_F(RecentProjectsTest, PruneMissingDropsEntriesWhoseDirectoryIsGone) {
    RecentProjects recent;
    auto kept = makeBundleDir(tempRoot, "Kept.agsproj");
    auto deleted = makeBundleDir(tempRoot, "Deleted.agsproj");

    recent.addProject(deleted);
    recent.addProject(kept);
    deleted.deleteRecursively();

    const auto removed = recent.pruneMissing();
    EXPECT_EQ(removed, 1);

    const auto entries = recent.getEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0], kept);
}

TEST_F(RecentProjectsTest, PruneMissingIsANoOpWhenEverythingStillExists) {
    RecentProjects recent;
    recent.addProject(makeBundleDir(tempRoot, "A.agsproj"));
    recent.addProject(makeBundleDir(tempRoot, "B.agsproj"));

    EXPECT_EQ(recent.pruneMissing(), 0);
    EXPECT_EQ(recent.getEntries().size(), 2u);
}

TEST_F(RecentProjectsTest, ClearEmptiesTheList) {
    RecentProjects recent;
    recent.addProject(makeBundleDir(tempRoot, "A.agsproj"));
    recent.clear();
    EXPECT_TRUE(recent.getEntries().empty());
}

TEST_F(RecentProjectsTest, XmlRoundTripPreservesOrder) {
    RecentProjects recent;
    recent.addProject(makeBundleDir(tempRoot, "A.agsproj"));
    recent.addProject(makeBundleDir(tempRoot, "B.agsproj"));
    recent.addProject(makeBundleDir(tempRoot, "C.agsproj"));

    auto xml = recent.toXml();
    ASSERT_NE(xml, nullptr);

    RecentProjects restored;
    restored.loadFromXml(*xml);

    EXPECT_EQ(restored.getEntries(), recent.getEntries());
}

// Same idiom Tests/PluginScanTests.cpp's persistence test and MainComponentTests.cpp's
// resetPanelKeys() use: open the real shared "Agent Synth" settings file through
// synth::userSettingsOptions() (the exact options MainComponent uses) so this proves the round
// trip through an actual juce::PropertiesFile, not just XmlElement<->XmlElement. The key is reset
// before and after so this test stays hermetic regardless of run order.
TEST_F(RecentProjectsTest, RoundTripsThroughAPropertiesFile) {
    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(synth::userSettingsOptions());
    auto* settings = appProperties.getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->removeValue(synth::kRecentProjectsSettingKey);

    RecentProjects original;
    original.addProject(makeBundleDir(tempRoot, "A.agsproj"));
    original.addProject(makeBundleDir(tempRoot, "B.agsproj"));

    if (auto xml = original.toXml()) {
        settings->setValue(synth::kRecentProjectsSettingKey, xml->toString());
        appProperties.saveIfNeeded();
    }

    RecentProjects restored;
    if (auto savedXml = juce::parseXML(settings->getValue(synth::kRecentProjectsSettingKey)))
        restored.loadFromXml(*savedXml);

    EXPECT_EQ(restored.getEntries(), original.getEntries());

    settings->removeValue(synth::kRecentProjectsSettingKey);
    appProperties.saveIfNeeded();
}
