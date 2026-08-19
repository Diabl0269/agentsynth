#include "../Source/AI/LocalHistoryStore.h"
#include <algorithm>
#include <gtest/gtest.h>

using synth::LocalConversation;
using synth::LocalConversationMessage;
using synth::LocalConversationSummary;
using synth::LocalHistoryStore;

namespace {

juce::String isoDaysAgo(double days) {
    return (juce::Time::getCurrentTime() - juce::RelativeTime::days(days)).toISO8601(true);
}

LocalConversation makeConversation(const juce::String& id, const juce::String& title, const juce::String& updatedAt) {
    LocalConversation c;
    c.id = id;
    c.title = title;
    c.createdAt = updatedAt;
    c.updatedAt = updatedAt;
    c.messages.push_back({"user", "hello", updatedAt});
    c.messages.push_back({"assistant", "hi there", updatedAt});
    return c;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Pure JSON transforms — no filesystem
// ---------------------------------------------------------------------------------------

TEST(LocalHistoryStoreTransform, ConversationRoundTripsThroughVar) {
    auto original = makeConversation("abc-123", "My Patch Chat", "2026-08-01T12:00:00.000Z");

    auto v = LocalHistoryStore::conversationToVar(original);
    LocalConversation parsed;
    ASSERT_TRUE(LocalHistoryStore::conversationFromVar(v, parsed));

    EXPECT_EQ(parsed.id, "abc-123");
    EXPECT_EQ(parsed.title, "My Patch Chat");
    EXPECT_EQ(parsed.createdAt, "2026-08-01T12:00:00.000Z");
    EXPECT_EQ(parsed.updatedAt, "2026-08-01T12:00:00.000Z");
    ASSERT_EQ(parsed.messages.size(), 2u);
    EXPECT_EQ(parsed.messages[0].role, "user");
    EXPECT_EQ(parsed.messages[0].content, "hello");
    EXPECT_EQ(parsed.messages[1].role, "assistant");
    EXPECT_EQ(parsed.messages[1].content, "hi there");
}

TEST(LocalHistoryStoreTransform, ConversationFromVarRejectsNonObject) {
    LocalConversation out;
    EXPECT_FALSE(LocalHistoryStore::conversationFromVar(juce::var(), out));
    EXPECT_FALSE(LocalHistoryStore::conversationFromVar(juce::var(42), out));
    EXPECT_FALSE(LocalHistoryStore::conversationFromVar(juce::JSON::parse("{ not json"), out));
}

TEST(LocalHistoryStoreTransform, ConversationFromVarRejectsMissingOrEmptyId) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("title", "No id here");
    LocalConversation out;
    EXPECT_FALSE(LocalHistoryStore::conversationFromVar(juce::var(obj.get()), out));

    obj->setProperty("id", "");
    EXPECT_FALSE(LocalHistoryStore::conversationFromVar(juce::var(obj.get()), out));
}

TEST(LocalHistoryStoreTransform, SummaryFromVarExtractsHeaderFieldsOnly) {
    auto conversation = makeConversation("id-1", "Title", "2026-08-01T00:00:00.000Z");
    auto v = LocalHistoryStore::conversationToVar(conversation);

    LocalConversationSummary summary;
    ASSERT_TRUE(LocalHistoryStore::summaryFromVar(v, summary));
    EXPECT_EQ(summary.id, "id-1");
    EXPECT_EQ(summary.title, "Title");
    EXPECT_EQ(summary.updatedAt, "2026-08-01T00:00:00.000Z");
}

TEST(LocalHistoryStoreTransform, SummaryFromVarRejectsMissingId) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("title", "No id");
    LocalConversationSummary out;
    EXPECT_FALSE(LocalHistoryStore::summaryFromVar(juce::var(obj.get()), out));
}

// ---------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------

class LocalHistoryStorePersistence : public ::testing::Test {
protected:
    void SetUp() override {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-history-tests");
        dir.deleteRecursively();
        dir.createDirectory();
    }

    void TearDown() override { dir.deleteRecursively(); }

    juce::File dir;
};

TEST_F(LocalHistoryStorePersistence, SaveThenGetRoundTrips) {
    auto conversation = makeConversation("conv-1", "Bass Patch", isoDaysAgo(0));
    ASSERT_TRUE(LocalHistoryStore::save(dir, conversation, LocalHistoryStore::kRetainForever));

    auto file = LocalHistoryStore::fileForId(dir, "conv-1");
    EXPECT_TRUE(file.existsAsFile());

    LocalConversation loaded;
    ASSERT_TRUE(LocalHistoryStore::get(dir, "conv-1", loaded));
    EXPECT_EQ(loaded.title, "Bass Patch");
    ASSERT_EQ(loaded.messages.size(), 2u);
}

TEST_F(LocalHistoryStorePersistence, SaveRefusesEmptyId) {
    LocalConversation conversation;
    conversation.id = "";
    conversation.title = "Nothing";
    EXPECT_FALSE(LocalHistoryStore::save(dir, conversation, LocalHistoryStore::kRetainForever));
}

TEST_F(LocalHistoryStorePersistence, GetOnMissingIdReturnsFalse) {
    LocalConversation out;
    EXPECT_FALSE(LocalHistoryStore::get(dir, "does-not-exist", out));
}

TEST_F(LocalHistoryStorePersistence, ListSortsSummariesByUpdatedAtDescending) {
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("older", "Older", isoDaysAgo(5)),
                                        LocalHistoryStore::kRetainForever));
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("newer", "Newer", isoDaysAgo(0)),
                                        LocalHistoryStore::kRetainForever));

    auto list = LocalHistoryStore::list(dir);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].id, "newer");
    EXPECT_EQ(list[1].id, "older");
}

TEST_F(LocalHistoryStorePersistence, ListSkipsUnreadableFiles) {
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("good", "Good", isoDaysAgo(0)),
                                        LocalHistoryStore::kRetainForever));
    dir.getChildFile("broken.json").replaceWithText("{ not json");

    auto list = LocalHistoryStore::list(dir);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, "good");
}

TEST_F(LocalHistoryStorePersistence, ListOnMissingDirectoryIsEmpty) {
    auto missing = dir.getChildFile("does-not-exist");
    EXPECT_TRUE(LocalHistoryStore::list(missing).empty());
}

TEST_F(LocalHistoryStorePersistence, DeleteOneRemovesTheFile) {
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("conv-1", "T", isoDaysAgo(0)),
                                        LocalHistoryStore::kRetainForever));
    EXPECT_TRUE(LocalHistoryStore::deleteOne(dir, "conv-1"));
    EXPECT_FALSE(LocalHistoryStore::fileForId(dir, "conv-1").existsAsFile());
    EXPECT_FALSE(LocalHistoryStore::deleteOne(dir, "conv-1")) << "second delete of an already-gone id is a no-op";
}

TEST_F(LocalHistoryStorePersistence, DeleteAllRemovesEveryFileAndReturnsCount) {
    ASSERT_TRUE(
        LocalHistoryStore::save(dir, makeConversation("a", "A", isoDaysAgo(0)), LocalHistoryStore::kRetainForever));
    ASSERT_TRUE(
        LocalHistoryStore::save(dir, makeConversation("b", "B", isoDaysAgo(0)), LocalHistoryStore::kRetainForever));

    EXPECT_EQ(LocalHistoryStore::deleteAll(dir), 2);
    EXPECT_TRUE(LocalHistoryStore::list(dir).empty());
}

// ---------------------------------------------------------------------------------------
// Retention: age-based pruning, one case per offered choice, plus "forever"
// ---------------------------------------------------------------------------------------

class LocalHistoryStoreRetention : public ::testing::TestWithParam<int> {
protected:
    void SetUp() override {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("agentsynth-history-retention-tests");
        dir.deleteRecursively();
        dir.createDirectory();
    }

    void TearDown() override { dir.deleteRecursively(); }

    juce::File dir;
};

TEST_P(LocalHistoryStoreRetention, PrunesOnlyConversationsOlderThanTheConfiguredWindow) {
    const int retentionDays = GetParam();

    // Set up both conversations with retentionDays=forever so this SETUP save doesn't itself
    // prune anything based on age yet.
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("outside-window", "Old", isoDaysAgo(retentionDays + 5)),
                                        LocalHistoryStore::kRetainForever));
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("inside-window", "Recent", isoDaysAgo(retentionDays - 5)),
                                        LocalHistoryStore::kRetainForever));

    // A fresh save WITH the real retentionDays applies pruning across the whole directory.
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("trigger", "Trigger", isoDaysAgo(0)), retentionDays));

    auto ids = LocalHistoryStore::list(dir);
    std::vector<juce::String> remaining;
    for (auto& s : ids)
        remaining.push_back(s.id);

    EXPECT_EQ(std::find(remaining.begin(), remaining.end(), juce::String("outside-window")), remaining.end())
        << "conversation older than the retention window should have been pruned";
    EXPECT_NE(std::find(remaining.begin(), remaining.end(), juce::String("inside-window")), remaining.end())
        << "conversation inside the retention window should survive";
    EXPECT_NE(std::find(remaining.begin(), remaining.end(), juce::String("trigger")), remaining.end());
}

INSTANTIATE_TEST_SUITE_P(EachOfferedRetentionChoice, LocalHistoryStoreRetention, ::testing::Values(30, 90, 180, 365));

TEST_F(LocalHistoryStorePersistence, ForeverRetentionSkipsAgePruning) {
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("ancient", "Ancient", isoDaysAgo(3650)),
                                        LocalHistoryStore::kRetainForever));
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("trigger", "Trigger", isoDaysAgo(0)),
                                        LocalHistoryStore::kRetainForever));

    auto list = LocalHistoryStore::list(dir);
    EXPECT_EQ(list.size(), 2u) << "kRetainForever must not age-prune even a decade-old conversation";
}

TEST_F(LocalHistoryStorePersistence, UnparseableUpdatedAtIsKeptRatherThanTreatedAsAncient) {
    LocalConversation corrupt;
    corrupt.id = "corrupt";
    corrupt.title = "Corrupt timestamp";
    corrupt.createdAt = "not-a-date";
    corrupt.updatedAt = "not-a-date";
    ASSERT_TRUE(LocalHistoryStore::save(dir, corrupt, LocalHistoryStore::kRetainForever));

    // A save with a real (short) retention window would prune anything genuinely old; the corrupt
    // timestamp must not be swept up in that.
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("trigger", "Trigger", isoDaysAgo(0)), 30));

    auto list = LocalHistoryStore::list(dir);
    bool corruptSurvived = false;
    for (auto& s : list)
        if (s.id == "corrupt")
            corruptSurvived = true;
    EXPECT_TRUE(corruptSurvived) << "an unparseable updatedAt must not be treated as infinitely old";
}

TEST_F(LocalHistoryStorePersistence, OutOfRangeRetentionValueFallsBackToDefault) {
    // A hand-edited settings value of 0 (or any value outside the offered set) must not prune
    // everything — it should behave as kDefaultRetentionDays (180) instead.
    ASSERT_TRUE(LocalHistoryStore::save(dir,
                                        makeConversation("within-default", "Within default window", isoDaysAgo(10)),
                                        LocalHistoryStore::kRetainForever));

    ASSERT_TRUE(
        LocalHistoryStore::save(dir, makeConversation("trigger", "Trigger", isoDaysAgo(0)), /*retentionDays=*/0));

    auto list = LocalHistoryStore::list(dir);
    bool survived = false;
    for (auto& s : list)
        if (s.id == "within-default")
            survived = true;
    EXPECT_TRUE(survived)
        << "an out-of-range retentionDays (0) must fall back to the 180-day default, not prune everything";
}

// ---------------------------------------------------------------------------------------
// Hard cap — independent of the retention setting
// ---------------------------------------------------------------------------------------

TEST_F(LocalHistoryStorePersistence, HardCapAppliesEvenWithForeverRetention) {
    // Write kHardCapFiles + 10 conversation files DIRECTLY to disk (bypassing save()'s own
    // pruning for setup, which would otherwise make this an O(n^2) directory scan per file and
    // blow the test time budget). Each gets a distinct, strictly increasing updatedAt so the
    // oldest ones are unambiguous.
    const int extra = 10;
    const int total = LocalHistoryStore::kHardCapFiles + extra;
    for (int i = 0; i < total; ++i) {
        auto conversation = makeConversation("item-" + juce::String(i), "T", isoDaysAgo((double)(total - i)));
        auto file = LocalHistoryStore::fileForId(dir, conversation.id);
        ASSERT_TRUE(file.replaceWithText(juce::JSON::toString(LocalHistoryStore::conversationToVar(conversation))));
    }
    ASSERT_EQ(LocalHistoryStore::list(dir).size(), (size_t)total);

    // One real save(), with "forever" retention so age-pruning contributes nothing — only the
    // hard cap should trim the directory.
    ASSERT_TRUE(LocalHistoryStore::save(dir, makeConversation("newest", "Newest", isoDaysAgo(0)),
                                        LocalHistoryStore::kRetainForever));

    auto list = LocalHistoryStore::list(dir);
    EXPECT_EQ(list.size(), (size_t)LocalHistoryStore::kHardCapFiles)
        << "hard cap must be enforced even though retention is 'forever'";

    // The newest item and the most-recently-updated of the pre-seeded ones must survive; the
    // very oldest pre-seeded ones must have been pruned first.
    bool newestSurvived = false, oldestSurvived = false;
    for (auto& s : list) {
        if (s.id == "newest")
            newestSurvived = true;
        if (s.id == "item-0")
            oldestSurvived = true;
    }
    EXPECT_TRUE(newestSurvived);
    EXPECT_FALSE(oldestSurvived) << "the oldest conversations should be the ones dropped by the hard cap";
}
