#include "../UniqueTempDir.h"
#include "UnhandledExceptionLog.h"
#include <gtest/gtest.h>
#include <stdexcept>

// An exception escaping a message-thread callback used to abort with no message at all. These cover the
// record the app's unhandledException() override leaves behind so the next crash names its what().
TEST(UnhandledExceptionLogTest, DescriptionCarriesWhatAndTheCatchSite) {
    const std::out_of_range e("vector::_M_range_check");
    const auto text = synth::describeUnhandledException(&e, "/src/juce_MessageManager.cpp", 180);
    EXPECT_TRUE(text.contains("vector::_M_range_check"));
    EXPECT_TRUE(text.contains("juce_MessageManager.cpp:180"));
}

TEST(UnhandledExceptionLogTest, NonStdThrowIsNamedUnknown) {
    EXPECT_TRUE(synth::describeUnhandledException(nullptr, "a.cpp", 1).contains("unknown exception"));
}

TEST(UnhandledExceptionLogTest, RecordAppendsOneLinePerExceptionToTheFile) {
    const auto dir =
        synth::test::uniqueTempFile("UnhandledExceptionLogTest").getChildFile("run_" + juce::Uuid().toString());
    const auto log = dir.getChildFile("logs").getChildFile("unhandled-exceptions.log");

    const std::runtime_error first("boom one");
    synth::recordUnhandledException(log, &first, "x.cpp", 7);
    synth::recordUnhandledException(log, nullptr, "y.cpp", 9);

    const auto lines = juce::StringArray::fromLines(log.loadFileAsString().trim());
    ASSERT_EQ(lines.size(), 2);
    EXPECT_TRUE(lines[0].contains("boom one"));
    EXPECT_TRUE(lines[1].contains("unknown exception"));
    dir.deleteRecursively();
}

TEST(UnhandledExceptionLogTest, RecordWithNoFileOnlyLogsAndDoesNotThrow) {
    const std::runtime_error e("no file");
    EXPECT_NO_THROW(synth::recordUnhandledException(juce::File(), &e, "z.cpp", 3));
}
