#include "TestPaths.h"
#include "UniqueTempDir.h"
#include <regex>
#include <set>

// Regression test for FRO1120: concurrent test shards shared one temp folder. A test that names its scratch folder with
// JUCE's pick-a-free-name call (which reserves nothing) can collide with the same test in another shard process, so
// every test goes through UniqueTempDir.h instead.

namespace {

// Split so this file does not match its own scan.
const juce::String kBannedCall = juce::String("getNonexistent") + "ChildFile(";
// A helper that takes a caller's name and puts it straight under the temp folder: every caller of it, and every
// instance of a parameterised test across the shards, shares that one file.
const juce::String kBannedFixedNameHelper = juce::String("tempDirectory).getChild") + "File(name)";

// A string LITERAL name straight under the temp folder (any whitespace or line break before the call): a fixed path
// every CI shard process shares, so one shard's teardown deletes the file another is reading.
const std::regex kBannedLiteralName(R"(tempDirectory\)\s*\.getChildFile\(\s*"[^"]*"\s*\))");

} // namespace

TEST(UniqueTempDir, TwoCallsNeverShareAFolderAndTheFolderExists) {
    const auto a = synth::test::makeUniqueTempDir("UniqueTempDirTest");
    const auto b = synth::test::makeUniqueTempDir("UniqueTempDirTest");
    EXPECT_NE(a, b);
    EXPECT_TRUE(a.isDirectory());
    EXPECT_TRUE(b.isDirectory());
    EXPECT_EQ(a.getParentDirectory(), juce::File::getSpecialLocation(juce::File::tempDirectory));
    a.deleteRecursively();
    b.deleteRecursively();
}

TEST(UniqueTempDir, TwoFilesOfOneNameNeverSharePathAndKeepTheStemAndExtension) {
    // Regression test for FRO1931: a bounce test's instances in two CI shards wrote one fixed-name wav, and one
    // read back the other's output.
    const auto a = synth::test::uniqueTempFile("agentsynth_take.wav");
    const auto b = synth::test::uniqueTempFile("agentsynth_take.wav");
    EXPECT_NE(a, b);
    EXPECT_TRUE(a.hasFileExtension(".wav"));
    EXPECT_TRUE(a.getFileName().startsWith("agentsynth_take_"));
    EXPECT_FALSE(a.exists());
    EXPECT_EQ(synth::test::uniqueTempFile("agentsynth_blocker").getFileExtension(), juce::String());
}

TEST(UniqueTempDir, PathHelperKeepsTheExtensionAndCreatesNothing) {
    const auto p = synth::test::uniqueTempPath("UniqueTempDirTest", ".agsproj");
    EXPECT_TRUE(p.hasFileExtension(".agsproj"));
    EXPECT_FALSE(p.exists());
}

TEST(UniqueTempDir, NoTestNamesATempFolderWithAFixedNameAndAFreeNamePick) {
    const juce::File testsRoot(TESTS_ROOT_DIR);
    ASSERT_TRUE(testsRoot.isDirectory());

    std::set<juce::String> offenders;
    int scanned = 0;
    for (const auto& f : testsRoot.findChildFiles(juce::File::findFiles, true, "*.cpp;*.h")) {
        if (f.getFileName() == "UniqueTempDir.h" || f.getFileName() == "UniqueTempDirTests.cpp")
            continue;
        ++scanned;
        const auto text = f.loadFileAsString();
        if (text.contains(kBannedCall) || text.contains(kBannedFixedNameHelper) ||
            std::regex_search(text.toStdString(), kBannedLiteralName))
            offenders.insert(f.getRelativePathFrom(testsRoot));
    }
    EXPECT_GT(scanned, 100) << "the scan found no test sources; is TESTS_ROOT_DIR right?";
    for (const auto& name : offenders)
        ADD_FAILURE() << name << " picks a temp name with " << kBannedCall << " or " << kBannedFixedNameHelper
                      << " or a literal file name under the temp folder -- use synth::test::makeUniqueTempDir / "
                         "uniqueTempPath / uniqueTempFile from Tests/UniqueTempDir.h";
}
