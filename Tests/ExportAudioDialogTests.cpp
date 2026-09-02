// synth::ui::ExportAudioDialog — the "Export Audio..." options dialog. Pure UI: it never touches
// AudioEngine/TimelineDoc/BounceRunner, so these tests drive the real controls and read back the
// BounceOptions/destination Export would send, with no audio device and no message loop needed.

#include "../Source/UI/ExportAudioDialog.h"
#include <gtest/gtest.h>

using synth::BounceFormat;
using synth::BounceOptions;
using synth::BounceResult;
using synth::ui::ExportAudioDialog;

namespace {
constexpr double kArrangementEndBeat = 16.0;
constexpr double kLoopStartBeat = 4.0;
constexpr double kLoopEndBeat = 12.0;
constexpr double kBpm = 120.0; // 0.5 s/beat, 2 s/bar

ExportAudioDialog makeDialog(double arrangementEndBeat = kArrangementEndBeat, bool hasLoopRange = false,
                             double loopStartBeat = 0.0, double loopEndBeat = 0.0, bool projectIsSaved = true,
                             const juce::String& fileNameBase = "Default") {
    return ExportAudioDialog(arrangementEndBeat, hasLoopRange, loopStartBeat, loopEndBeat, kBpm, projectIsSaved,
                             juce::File::getSpecialLocation(juce::File::tempDirectory), fileNameBase);
}
} // namespace

TEST(ExportAudioDialogTest, DefaultsToWholeArrangementWhenClipsExist) {
    auto dialog = makeDialog();
    const auto options = dialog.getOptionsForTest();
    EXPECT_DOUBLE_EQ(options.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(options.endBeat, kArrangementEndBeat);
    EXPECT_EQ(options.format, BounceFormat::Wav);
    EXPECT_DOUBLE_EQ(options.sampleRate, 48000.0);
    EXPECT_EQ(options.bitDepth, 24);
}

TEST(ExportAudioDialogTest, DefaultsToSelectionWhenNoClipsButLoopIsSet) {
    auto dialog = makeDialog(/*arrangementEndBeat=*/0.0, /*hasLoopRange=*/true, kLoopStartBeat, kLoopEndBeat);
    const auto options = dialog.getOptionsForTest();
    EXPECT_DOUBLE_EQ(options.startBeat, kLoopStartBeat);
    EXPECT_DOUBLE_EQ(options.endBeat, kLoopEndBeat);
}

TEST(ExportAudioDialogTest, SelectionIgnoredWhenNoLoopRangeEvenIfToggled) {
    auto dialog = makeDialog();
    // Selection is disabled with no loop range - programmatically toggling it (setEnabled only
    // gates USER interaction) must still fall back to the arrangement rather than send an empty
    // [0,0] range into BounceOptions validation.
    dialog.setUseSelectionForTest(true);
    const auto options = dialog.getOptionsForTest();
    EXPECT_DOUBLE_EQ(options.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(options.endBeat, kArrangementEndBeat);
}

TEST(ExportAudioDialogTest, SwitchingToAiffStepsDownA32BitSelection) {
    auto dialog = makeDialog();
    dialog.setBitDepthForTest(32);
    ASSERT_EQ(dialog.getOptionsForTest().bitDepth, 32);

    // AIFF has no 32-bit float variant (see BounceOptions::format) - the now-invalid selection
    // must not survive the format switch.
    dialog.setFormatForTest(BounceFormat::Aiff);
    const auto options = dialog.getOptionsForTest();
    EXPECT_EQ(options.format, BounceFormat::Aiff);
    EXPECT_NE(options.bitDepth, 32);
}

TEST(ExportAudioDialogTest, SampleRateAndDestinationRoundTrip) {
    auto dialog = makeDialog();
    dialog.setSampleRateForTest(96000.0);
    EXPECT_DOUBLE_EQ(dialog.getOptionsForTest().sampleRate, 96000.0);

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("chosen.wav");
    dialog.setDestinationForTest(file);
    EXPECT_EQ(dialog.getDestinationForTest(), file);
}

TEST(ExportAudioDialogTest, TriggerExportFiresOnExportWithTheCurrentOptionsAndDestination) {
    auto dialog = makeDialog(kArrangementEndBeat, /*hasLoopRange=*/true, kLoopStartBeat, kLoopEndBeat);
    dialog.setFormatForTest(BounceFormat::Aiff);
    dialog.setBitDepthForTest(16);
    dialog.setSampleRateForTest(44100.0);
    dialog.setUseSelectionForTest(true);
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("triggered.aiff");
    dialog.setDestinationForTest(file);

    bool fired = false;
    BounceOptions seen;
    juce::File seenFile;
    dialog.onExport = [&](BounceOptions options, juce::File destination) {
        fired = true;
        seen = options;
        seenFile = destination;
    };

    dialog.triggerExportForTest();

    ASSERT_TRUE(fired);
    EXPECT_EQ(seen.format, BounceFormat::Aiff);
    EXPECT_EQ(seen.bitDepth, 16);
    EXPECT_DOUBLE_EQ(seen.sampleRate, 44100.0);
    EXPECT_DOUBLE_EQ(seen.startBeat, kLoopStartBeat);
    EXPECT_DOUBLE_EQ(seen.endBeat, kLoopEndBeat);
    EXPECT_EQ(seenFile, file);
}

TEST(ExportAudioDialogTest, ProgressPageReflectsReportedFractionAndCompletion) {
    auto dialog = makeDialog();
    dialog.showProgressPage();
    dialog.reportProgress(0.5);

    bool closeRequested = false;
    dialog.onRequestClose = [&] { closeRequested = true; };

    BounceResult result;
    result.ok = true;
    result.message = "Bounced 100 samples.";
    dialog.reportComplete(result);
    // reportComplete repurposes the progress page's Cancel button into Close - firing onRequestClose
    // is exercised end to end via MainComponent, not reachable from here without a live button
    // click; this test only pins that reportComplete does not itself request a close.
    EXPECT_FALSE(closeRequested);
}

// ---- P8-5 follow-up: destination folder + file name field ----

TEST(ExportAudioDialogTest, DestinationDefaultsToTheGivenFolderAndFileNameBase) {
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto dialog = makeDialog(kArrangementEndBeat, false, 0.0, 0.0, true, "My Project");
    EXPECT_EQ(dialog.getDestinationForTest(), folder.getChildFile("My Project.wav"));
}

TEST(ExportAudioDialogTest, SwitchingFormatKeepsTheTypedFileName) {
    auto dialog = makeDialog(kArrangementEndBeat, false, 0.0, 0.0, true, "My Project");
    dialog.setFormatForTest(BounceFormat::Aiff);
    EXPECT_EQ(dialog.getDestinationForTest().getFileName(), "My Project.aiff");
}

// ---- P8-5 follow-up: tail Seconds/Bars ----

TEST(ExportAudioDialogTest, TailDefaultsToZeroSeconds) {
    auto dialog = makeDialog();
    EXPECT_DOUBLE_EQ(dialog.getOptionsForTest().tailSeconds, 0.0);
}

TEST(ExportAudioDialogTest, TailInBarsConvertsToSecondsAtTheGivenBpm) {
    auto dialog = makeDialog();
    dialog.setTailUnitBarsForTest(true);
    dialog.setTailValueForTest(2.0); // 2 bars at 120 bpm, 4/4 -> 2 s/bar -> 4 s
    EXPECT_DOUBLE_EQ(dialog.getOptionsForTest().tailSeconds, 4.0);
}

TEST(ExportAudioDialogTest, SwitchingBackToSecondsPreservesTheValue) {
    auto dialog = makeDialog();
    dialog.setTailUnitBarsForTest(true);
    dialog.setTailValueForTest(1.0); // 1 bar at 120 bpm -> 2 s
    dialog.setTailUnitBarsForTest(false);
    EXPECT_NEAR(dialog.getOptionsForTest().tailSeconds, 2.0, 1e-9);
}

// ---- P8-5 follow-up: destination-exists collision prompt ----

TEST(ExportAudioDialogTest, ExportProceedsDirectlyWhenDestinationDoesNotExist) {
    auto dialog = makeDialog();
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ExportAudioDialogTest_NoCollision.wav");
    file.deleteFile();
    dialog.setDestinationForTest(file);

    bool promptShown = false;
    dialog.collisionPromptForTest = [&](std::function<void(int)>) { promptShown = true; };
    bool fired = false;
    dialog.onExport = [&](BounceOptions, juce::File) { fired = true; };

    dialog.triggerExportForTest();

    EXPECT_FALSE(promptShown);
    EXPECT_TRUE(fired);
}

TEST(ExportAudioDialogTest, OverwriteChoiceExportsToTheSameDestination) {
    auto dialog = makeDialog();
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ExportAudioDialogTest_Overwrite.wav");
    file.replaceWithText("existing");
    dialog.setDestinationForTest(file);

    dialog.collisionPromptForTest = [](std::function<void(int)> onChoice) { onChoice(1); }; // Overwrite
    juce::File seenFile;
    dialog.onExport = [&](BounceOptions, juce::File destination) { seenFile = destination; };

    dialog.triggerExportForTest();

    EXPECT_EQ(seenFile, file);
    file.deleteFile();
}

TEST(ExportAudioDialogTest, SaveAsCopyChoiceUniquifiesTheFileName) {
    auto dialog = makeDialog();
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto file = dir.getChildFile("ExportAudioDialogTest_Copy.wav");
    file.replaceWithText("existing");
    dialog.setDestinationForTest(file);

    dialog.collisionPromptForTest = [](std::function<void(int)> onChoice) { onChoice(2); }; // Save as Copy
    juce::File seenFile;
    dialog.onExport = [&](BounceOptions, juce::File destination) { seenFile = destination; };

    dialog.triggerExportForTest();

    EXPECT_EQ(seenFile, dir.getChildFile("ExportAudioDialogTest_Copy 2.wav"));
    file.deleteFile();
}

TEST(ExportAudioDialogTest, CancelChoiceDoesNotFireOnExport) {
    auto dialog = makeDialog();
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ExportAudioDialogTest_Cancel.wav");
    file.replaceWithText("existing");
    dialog.setDestinationForTest(file);

    dialog.collisionPromptForTest = [](std::function<void(int)> onChoice) { onChoice(0); }; // Cancel
    bool fired = false;
    dialog.onExport = [&](BounceOptions, juce::File) { fired = true; };

    dialog.triggerExportForTest();

    EXPECT_FALSE(fired);
    file.deleteFile();
}
