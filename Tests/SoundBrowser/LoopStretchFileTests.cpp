// The stretched copy of a loop on disk: its length, its reuse, and the cases that hand the original back.
#include "SoundBrowser/LoopStretchFile.h"
#include "SoundBrowserTestKit.h"
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace synth::soundbrowser;

namespace {

struct Folder {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("loop-stretch-file-" + juce::Uuid().toDashedString());
    ~Folder() { dir.deleteRecursively(); }
};

int lengthOf(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    return reader == nullptr ? -1 : (int)reader->lengthInSamples;
}

} // namespace

TEST(LoopStretchFileTest, ALoopAtAnotherTempoIsWrittenAsAWavOfTheStretchedLength) {
    Folder f;
    const auto source = f.dir.getChildFile("loop.wav");
    sound_browser_test::writeSignal(source, 4.0, 44100.0, 0.5f, 220.0, 2);
    const auto copy = stretchedLoopFile(source, {120.0, 96.0, 0}, f.dir.getChildFile("cache"));
    ASSERT_NE(copy, source);
    EXPECT_TRUE(copy.hasFileExtension("wav"));
    EXPECT_EQ(copy.getParentDirectory(), f.dir.getChildFile("cache"));
    EXPECT_TRUE(copy.getFileName().startsWith("loop-96bpm"));
    EXPECT_NEAR(lengthOf(copy), 5.0 * 44100.0, 2.0);
}

TEST(LoopStretchFileTest, TheSameLoopAtTheSameTempoIsRenderedOnce) {
    Folder f;
    const auto source = f.dir.getChildFile("loop.wav");
    sound_browser_test::writeSignal(source, 2.0, 44100.0, 0.5f, 220.0, 1);
    const auto cache = f.dir.getChildFile("cache");
    const auto first = stretchedLoopFile(source, {120.0, 96.0, 0}, cache);
    const auto stamp = first.getLastModificationTime();
    juce::Thread::sleep(20);
    EXPECT_EQ(stretchedLoopFile(source, {120.0, 96.0, 0}, cache), first);
    EXPECT_EQ(first.getLastModificationTime(), stamp) << "reused, not written again";
    EXPECT_NE(stretchedLoopFile(source, {120.0, 100.0, 0}, cache), first) << "another tempo is another copy";
    EXPECT_NE(stretchedLoopFile(source, {120.0, 96.0, 2}, cache), first) << "another shift is another copy";
}

TEST(LoopStretchFileTest, TwoFilesWithOneNameKeepTheirOwnCopies) {
    Folder f;
    const auto a = f.dir.getChildFile("a/loop.wav");
    const auto b = f.dir.getChildFile("b/loop.wav");
    sound_browser_test::writeSignal(a, 2.0, 44100.0, 0.5f, 220.0, 1);
    sound_browser_test::writeSignal(b, 3.0, 44100.0, 0.5f, 330.0, 1);
    const auto cache = f.dir.getChildFile("cache");
    EXPECT_NE(stretchedLoopFile(a, {120.0, 96.0, 0}, cache), stretchedLoopFile(b, {120.0, 96.0, 0}, cache));
}

TEST(LoopStretchFileTest, NothingToStretchOrNothingToReadGivesTheOriginalBack) {
    Folder f;
    const auto source = f.dir.getChildFile("loop.wav");
    sound_browser_test::writeSignal(source, 2.0, 44100.0, 0.5f, 220.0, 1);
    const auto cache = f.dir.getChildFile("cache");
    EXPECT_EQ(stretchedLoopFile(source, {120.0, 120.0, 0}, cache), source);
    EXPECT_EQ(stretchedLoopFile(source, {0.0, 96.0, 0}, cache), source) << "unknown tempo";
    EXPECT_EQ(stretchedLoopFile(f.dir.getChildFile("gone.wav"), {120.0, 96.0, 0}, cache),
              f.dir.getChildFile("gone.wav"));
    const auto junk = f.dir.getChildFile("junk.wav");
    junk.replaceWithText("not audio");
    EXPECT_EQ(stretchedLoopFile(junk, {120.0, 96.0, 0}, cache), junk);
    EXPECT_FALSE(cache.getChildFile("junk-96bpm.wav").exists());
}
