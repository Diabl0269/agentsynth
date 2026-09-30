// Topic: the MP3 export's encoder lookup (Transport/LameLocator.h) and the BounceFormat::Mp3
// mapping in Transport/BounceExporter.h. Nothing here needs `lame` installed.

#include "Transport/BounceExporter.h"
#include "Transport/LameLocator.h"
#include <gtest/gtest.h>

namespace {
struct ScratchDir {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("agentsynth_lame_locator", "", false);
    ScratchDir() { dir.createDirectory(); }
    ~ScratchDir() { dir.deleteRecursively(); }
};

juce::String lameName() {
#if JUCE_WINDOWS
    return "lame.exe";
#else
    return "lame";
#endif
}
} // namespace

TEST(LameLocatorTest, FindsLameInAnInjectedDirectory) {
    ScratchDir empty, withLame;
    ASSERT_TRUE(withLame.dir.getChildFile(lameName()).create());

    const auto found =
        synth::findLameExecutable(juce::StringArray{empty.dir.getFullPathName(), withLame.dir.getFullPathName()});
    EXPECT_EQ(found, withLame.dir.getChildFile(lameName()));
}

TEST(LameLocatorTest, ReportsNothingWhenNoDirectoryHasLame) {
    ScratchDir empty;
    const auto found = synth::findLameExecutable(
        juce::StringArray{empty.dir.getFullPathName(), empty.dir.getChildFile("missing").getFullPathName()});
    EXPECT_FALSE(found.existsAsFile());
    EXPECT_FALSE(synth::findLameExecutable(juce::StringArray{}).existsAsFile());
}

TEST(LameLocatorTest, PrefersTheEarlierDirectory) {
    ScratchDir first, second;
    ASSERT_TRUE(first.dir.getChildFile(lameName()).create());
    ASSERT_TRUE(second.dir.getChildFile(lameName()).create());
    const auto found =
        synth::findLameExecutable(juce::StringArray{first.dir.getFullPathName(), second.dir.getFullPathName()});
    EXPECT_EQ(found.getParentDirectory(), first.dir);
}

#if !JUCE_WINDOWS
TEST(LameLocatorTest, DefaultSearchCoversPathAndTheUsualInstallLocations) {
    const auto dirs = synth::defaultLameSearchDirectories();
    EXPECT_TRUE(dirs.contains("/opt/homebrew/bin"));
    EXPECT_TRUE(dirs.contains("/usr/local/bin"));
    EXPECT_TRUE(dirs.contains("/usr/bin"));
}
#endif

TEST(LameLocatorTest, InstallHintNamesTheFix) { EXPECT_TRUE(synth::lameInstallHint().contains("brew install lame")); }

TEST(BounceFormatMp3Test, ExtensionAndBitrateToLameQualityIndex) {
    EXPECT_EQ(synth::fileExtensionFor(synth::BounceFormat::Mp3), "mp3");

    juce::LAMEEncoderAudioFormat lame{juce::File()};
    const auto options = lame.getQualityOptions();
    for (const int kbps : synth::kMp3BitratesKbps) {
        const int index = synth::lameQualityIndexForBitrate(kbps);
        ASSERT_GE(index, 0) << kbps;
        EXPECT_EQ(options[index], juce::String(kbps) + " Kb/s CBR");
    }
    EXPECT_EQ(synth::lameQualityIndexForBitrate(100), -1);
    EXPECT_EQ(synth::lameQualityIndexForBitrate(0), -1);
}

TEST(BounceFormatMp3Test, NoFormatWithoutAnEncoder) {
    EXPECT_EQ(synth::createAudioFormatFor(synth::BounceFormat::Mp3), nullptr);
    EXPECT_EQ(synth::createAudioFormatFor(synth::BounceFormat::Mp3, juce::File("/nonexistent/lame")), nullptr);
    EXPECT_NE(synth::createAudioFormatFor(synth::BounceFormat::Flac), nullptr);
}

TEST(BounceFormatMp3Test, ValidationNeedsLameAndAnOfferedBitrateButIgnoresBitDepth) {
    ScratchDir scratch;
    const auto fakeLame = scratch.dir.getChildFile(lameName());
    ASSERT_TRUE(fakeLame.create());

    synth::BounceOptions options;
    options.format = synth::BounceFormat::Mp3;
    EXPECT_TRUE(synth::validateBounceOptions(options).containsIgnoreCase("lame"))
        << "no encoder: a clear message, not a silent failure";

    options.lameExecutable = fakeLame;
    options.bitDepth = 32; // meaningless for MP3, so not an error
    EXPECT_TRUE(synth::validateBounceOptions(options).isEmpty()) << synth::validateBounceOptions(options);

    options.mp3BitrateKbps = 100;
    EXPECT_TRUE(synth::validateBounceOptions(options).containsIgnoreCase("bitrate"));

    // Other formats still reject a bad bit depth and never look at the MP3 fields.
    options.format = synth::BounceFormat::Wav;
    options.bitDepth = 12;
    EXPECT_FALSE(synth::validateBounceOptions(options).isEmpty());
}
