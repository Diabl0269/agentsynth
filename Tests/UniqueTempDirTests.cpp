#include "UniqueTempDir.h"
#include <set>

// Regression test for FRO1120: concurrent test shards shared one temp folder. A test that names its scratch folder with
// JUCE's pick-a-free-name call (which reserves nothing) can collide with the same test in another shard process, so
// every test goes through UniqueTempDir.h instead.

namespace {

// Split so this file does not match its own scan.
const juce::String kBannedCall = juce::String("getNonexistent") + "ChildFile(";

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
        if (f.loadFileAsString().contains(kBannedCall))
            offenders.insert(f.getRelativePathFrom(testsRoot));
    }
    EXPECT_GT(scanned, 100) << "the scan found no test sources; is TESTS_ROOT_DIR right?";
    for (const auto& name : offenders)
        ADD_FAILURE() << name << " picks a temp name with " << kBannedCall
                      << " -- use synth::test::makeUniqueTempDir / uniqueTempPath from Tests/UniqueTempDir.h";
}
