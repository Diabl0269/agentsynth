#include "../Source/GeneralFeedbackStore.h"
#include <gtest/gtest.h>

namespace synth {

class GeneralFeedbackStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("GeneralFeedbackStoreTest_" + juce::Uuid().toString())
                       .getChildFile("general_feedback.jsonl");
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

TEST_F(GeneralFeedbackStoreTest, RecordCreatesParentDirectoryAndFile) {
    GeneralFeedbackStore store(tempFile);
    store.record("the piano roll is great");
    EXPECT_TRUE(tempFile.existsAsFile());
}

TEST_F(GeneralFeedbackStoreTest, RecordsTextAndCategory) {
    GeneralFeedbackStore store(tempFile);
    store.record("crashes on save", GeneralFeedbackStore::Category::Bug);
    store.record("please add a spectrum analyzer", GeneralFeedbackStore::Category::Feature);
    store.record("just saying hi", GeneralFeedbackStore::Category::Other);

    auto entries = lines();
    ASSERT_EQ(entries.size(), 3);

    auto first = juce::JSON::parse(entries[0]);
    EXPECT_EQ(first.getDynamicObject()->getProperty("category").toString(), "bug");
    EXPECT_EQ(first.getDynamicObject()->getProperty("text").toString(), "crashes on save");

    auto second = juce::JSON::parse(entries[1]);
    EXPECT_EQ(second.getDynamicObject()->getProperty("category").toString(), "feature");

    auto third = juce::JSON::parse(entries[2]);
    EXPECT_EQ(third.getDynamicObject()->getProperty("category").toString(), "other");
}

TEST_F(GeneralFeedbackStoreTest, DefaultCategoryIsOther) {
    GeneralFeedbackStore store(tempFile);
    store.record("no category given");

    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    EXPECT_EQ(entry->getProperty("category").toString(), "other");
}

TEST_F(GeneralFeedbackStoreTest, EachCallAppendsRatherThanOverwrites) {
    GeneralFeedbackStore store(tempFile);
    for (int i = 0; i < 3; ++i)
        store.record("feedback " + juce::String(i));

    EXPECT_EQ(lines().size(), 3);
}

TEST_F(GeneralFeedbackStoreTest, IncludesTimestamp) {
    GeneralFeedbackStore store(tempFile);
    store.record("some feedback");

    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    EXPECT_TRUE(entry->getProperty("timestamp").toString().isNotEmpty());
}

} // namespace synth
