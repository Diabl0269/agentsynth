// Concern: snippet name sanitisation, the drag-payload encoding, and on-disk persistence
// (save/load/list/delete).
#include "SnippetManagerTestHelpers.h"
#include "UserSettings.h"

TEST(SnippetName, TrimsAndKeepsOrdinaryNames) {
    EXPECT_EQ(SnippetManager::sanitiseName("  My Supersaw Lead  "), "My Supersaw Lead");
}

TEST(SnippetName, StripsPathSeparatorsSoANameCannotEscapeTheDirectory) {
    auto cleaned = SnippetManager::sanitiseName("../../etc/passwd");
    EXPECT_FALSE(cleaned.contains("/"));
    EXPECT_FALSE(cleaned.contains("\\"));
    EXPECT_FALSE(cleaned.startsWithChar('.'));
}

TEST(SnippetName, RejectsNamesThatSanitiseToNothing) {
    EXPECT_TRUE(SnippetManager::sanitiseName("").isEmpty());
    EXPECT_TRUE(SnippetManager::sanitiseName("   ").isEmpty());
    EXPECT_TRUE(SnippetManager::sanitiseName("...").isEmpty());
}

TEST(SnippetName, CapsLength) {
    auto cleaned = SnippetManager::sanitiseName(juce::String::repeatedString("a", 500));
    EXPECT_LE(cleaned.length(), SnippetManager::kMaxNameLength);
    EXPECT_GT(cleaned.length(), 0);
}

TEST(SnippetPayload, RoundTripsThroughTheDragChannel) {
    const juce::String name = "My Supersaw Lead";
    auto payload = SnippetManager::payloadForName(name);

    EXPECT_TRUE(SnippetManager::isSnippetPayload(payload));
    EXPECT_EQ(SnippetManager::nameFromPayload(payload), name);
}

TEST(SnippetPayload, PlainModuleNamesAreNotSnippetPayloads) {
    EXPECT_FALSE(SnippetManager::isSnippetPayload("Oscillator"));
    EXPECT_TRUE(SnippetManager::nameFromPayload("Oscillator").isEmpty());
}

// ---------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------

class SnippetPersistence : public ::testing::Test {
protected:
    void SetUp() override {
        dir = synth::userSettingsRootDirectory().getChildFile("agentsynth-snippet-tests");
        dir.deleteRecursively();
        dir.createDirectory();

        juce::AudioProcessorGraph graph;
        auto osc = graph.addNode(std::make_unique<OscillatorModule>());
        osc->properties.set("x", 0);
        osc->properties.set("y", 0);
        auto filter = graph.addNode(std::make_unique<FilterModule>());
        filter->properties.set("x", 300);
        filter->properties.set("y", 0);
        graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
        sample = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID}, "Sample");
    }

    void TearDown() override { dir.deleteRecursively(); }

    juce::File dir;
    juce::var sample;
};

TEST_F(SnippetPersistence, SaveThenLoadRoundTrips) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Sample", sample));

    auto file = SnippetManager::fileForName(dir, "Sample");
    ASSERT_TRUE(file.existsAsFile());
    EXPECT_TRUE(file.getFileName().endsWith(SnippetManager::kFileExtension));

    auto loaded = SnippetManager::loadSnippet(file);
    ASSERT_TRUE(loaded.isObject());
    EXPECT_EQ(SnippetManager::getSnippetName(loaded), "Sample");
    EXPECT_EQ(SnippetManager::getModuleCount(loaded), 2);
}

TEST_F(SnippetPersistence, SavedSnippetInsertsFromDisk) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Sample", sample));
    auto loaded = SnippetManager::loadSnippet(SnippetManager::fileForName(dir, "Sample"));

    juce::AudioProcessorGraph target;
    auto added = SnippetManager::insertSnippet(loaded, target, {100, 100});

    EXPECT_EQ(added.size(), 2u);
    EXPECT_EQ(target.getConnections().size(), 1);
}

TEST_F(SnippetPersistence, SaveRewritesTheNameToTheSanitisedForm) {
    // The sidebar label and the filename must never disagree.
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "  Padded Name  ", sample));

    auto loaded = SnippetManager::loadSnippet(SnippetManager::fileForName(dir, "Padded Name"));
    ASSERT_TRUE(loaded.isObject());
    EXPECT_EQ(SnippetManager::getSnippetName(loaded), "Padded Name");
}

TEST_F(SnippetPersistence, RefusesToSaveAnEmptySnippetOrAnUnusableName) {
    juce::AudioProcessorGraph empty;
    auto emptySnippet = SnippetManager::extractSnippet(empty, {}, "Nothing");

    EXPECT_FALSE(SnippetManager::saveSnippet(dir, "Nothing", emptySnippet));
    EXPECT_FALSE(SnippetManager::saveSnippet(dir, "   ", sample));
    EXPECT_TRUE(SnippetManager::listSnippets(dir).isEmpty());
}

TEST_F(SnippetPersistence, ListSnippetsReportsNameAndModuleCountSortedByName) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Zeta", sample));
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "alpha", sample));

    auto list = SnippetManager::listSnippets(dir);
    ASSERT_EQ(list.size(), 2);
    EXPECT_EQ(list[0].name, "alpha") << "sorted case-insensitively";
    EXPECT_EQ(list[1].name, "Zeta");
    EXPECT_EQ(list[0].moduleCount, 2);
}

TEST_F(SnippetPersistence, ListSnippetsSkipsUnreadableFiles) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Good", sample));
    dir.getChildFile(juce::String("Broken") + SnippetManager::kFileExtension).replaceWithText("{ not json");

    auto list = SnippetManager::listSnippets(dir);
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list[0].name, "Good");
}

TEST_F(SnippetPersistence, ListSnippetsOnMissingDirectoryIsEmpty) {
    auto missing = dir.getChildFile("does-not-exist");
    EXPECT_TRUE(SnippetManager::listSnippets(missing).isEmpty());
}

TEST_F(SnippetPersistence, DeleteRemovesTheFile) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Sample", sample));
    ASSERT_EQ(SnippetManager::listSnippets(dir).size(), 1);

    EXPECT_TRUE(SnippetManager::deleteSnippet(dir, "Sample"));
    EXPECT_TRUE(SnippetManager::listSnippets(dir).isEmpty());
    EXPECT_FALSE(SnippetManager::deleteSnippet(dir, "Sample")) << "deleting twice reports failure";
}

TEST_F(SnippetPersistence, LoadingAMissingFileYieldsAVoidVar) {
    EXPECT_FALSE(SnippetManager::loadSnippet(dir.getChildFile("nope.agsnip")).isObject());
}
