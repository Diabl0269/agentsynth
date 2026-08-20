#include "../Source/AI/PatchFeedbackStore.h"
#include <gtest/gtest.h>

namespace synth {

class PatchFeedbackStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("PatchFeedbackStoreTest_" + juce::Uuid().toString())
                       .getChildFile("patch_feedback.jsonl");
    }

    void TearDown() override { tempFile.getParentDirectory().deleteRecursively(); }

    juce::StringArray lines() const {
        juce::StringArray result;
        result.addLines(tempFile.loadFileAsString());
        result.removeEmptyStrings();
        return result;
    }

    juce::File tempFile;
};

TEST_F(PatchFeedbackStoreTest, RecordCreatesParentDirectoryAndFile) {
    PatchFeedbackStore store(tempFile);
    store.record(R"({"mode":"merge","nodes":[],"connections":[]})", PatchFeedbackStore::Rating::Up);
    EXPECT_TRUE(tempFile.existsAsFile());
}

TEST_F(PatchFeedbackStoreTest, RecordsRatingAsUpOrDown) {
    PatchFeedbackStore store(tempFile);
    store.record(R"({"mode":"merge","nodes":[],"connections":[]})", PatchFeedbackStore::Rating::Up);
    store.record(R"({"mode":"merge","nodes":[],"connections":[]})", PatchFeedbackStore::Rating::Down);

    auto entries = lines();
    ASSERT_EQ(entries.size(), 2);

    auto first = juce::JSON::parse(entries[0]);
    EXPECT_EQ(first.getDynamicObject()->getProperty("rating").toString(), "up");

    auto second = juce::JSON::parse(entries[1]);
    EXPECT_EQ(second.getDynamicObject()->getProperty("rating").toString(), "down");
}

TEST_F(PatchFeedbackStoreTest, OmitsCommentWhenEmptyIncludesWhenPresent) {
    PatchFeedbackStore store(tempFile);
    store.record(R"({"nodes":[]})", PatchFeedbackStore::Rating::Up);
    store.record(R"({"nodes":[]})", PatchFeedbackStore::Rating::Down, "too much reverb");

    auto entries = lines();
    ASSERT_EQ(entries.size(), 2);

    // Keep the parsed `var` alive for the lifetime of the raw DynamicObject* it owns —
    // chaining `.getDynamicObject()` straight off a temporary leaves a dangling pointer the
    // instant the full-expression ends and the temporary's refcount drops to zero.
    auto firstVar = juce::JSON::parse(entries[0]);
    auto* first = firstVar.getDynamicObject();
    EXPECT_FALSE(first->hasProperty("comment"));

    auto secondVar = juce::JSON::parse(entries[1]);
    auto* second = secondVar.getDynamicObject();
    EXPECT_EQ(second->getProperty("comment").toString(), "too much reverb");
}

TEST_F(PatchFeedbackStoreTest, EmbedsParsedPatchJsonWhenValid) {
    PatchFeedbackStore store(tempFile);
    store.record(R"({"mode":"merge","nodes":[{"id":1,"type":"Reverb"}],"connections":[]})",
                 PatchFeedbackStore::Rating::Up);

    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    ASSERT_TRUE(entry->hasProperty("patch"));
    auto* patchObj = entry->getProperty("patch").getDynamicObject();
    ASSERT_NE(patchObj, nullptr);
    EXPECT_EQ(patchObj->getProperty("mode").toString(), "merge");
}

TEST_F(PatchFeedbackStoreTest, FallsBackToRawStringOnMalformedJson) {
    PatchFeedbackStore store(tempFile);
    store.record("not valid json {{{", PatchFeedbackStore::Rating::Down);

    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    EXPECT_FALSE(entry->hasProperty("patch"));
    EXPECT_EQ(entry->getProperty("patchRaw").toString(), "not valid json {{{");
}

TEST_F(PatchFeedbackStoreTest, EachCallAppendsRatherThanOverwrites) {
    PatchFeedbackStore store(tempFile);
    for (int i = 0; i < 3; ++i)
        store.record(R"({"nodes":[]})", PatchFeedbackStore::Rating::Up);

    EXPECT_EQ(lines().size(), 3);
}

TEST_F(PatchFeedbackStoreTest, IncludesTimestamp) {
    PatchFeedbackStore store(tempFile);
    store.record(R"({"nodes":[]})", PatchFeedbackStore::Rating::Up);

    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    EXPECT_TRUE(entry->getProperty("timestamp").toString().isNotEmpty());
}

// P6-9: conversationId/messageId are the server-side ids a rating corresponds to, when known.
TEST_F(PatchFeedbackStoreTest, IncludesConversationAndMessageIdWhenProvided) {
    PatchFeedbackStore store(tempFile);
    store.record(R"({"nodes":[]})", PatchFeedbackStore::Rating::Up, "great patch", "conv-123", "msg-456");

    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->getProperty("conversationId").toString(), "conv-123");
    EXPECT_EQ(entry->getProperty("messageId").toString(), "msg-456");
}

// Old call sites (no conversationId/messageId args) and offline/free-tier entries must keep the
// exact pre-P6-9 shape — no empty-string "conversationId"/"messageId" fields ever appear.
TEST_F(PatchFeedbackStoreTest, OmitsConversationAndMessageIdWhenNotProvided) {
    PatchFeedbackStore store(tempFile);
    store.record(R"({"nodes":[]})", PatchFeedbackStore::Rating::Down);

    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->hasProperty("conversationId"));
    EXPECT_FALSE(entry->hasProperty("messageId"));
}

} // namespace synth
