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
} // namespace

TEST(ExportAudioDialogTest, DefaultsToWholeArrangementWhenClipsExist) {
    ExportAudioDialog dialog(kArrangementEndBeat, /*hasLoopRange=*/false, 0.0, 0.0, juce::File());
    const auto options = dialog.getOptionsForTest();
    EXPECT_DOUBLE_EQ(options.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(options.endBeat, kArrangementEndBeat);
    EXPECT_EQ(options.format, BounceFormat::Wav);
    EXPECT_DOUBLE_EQ(options.sampleRate, 48000.0);
    EXPECT_EQ(options.bitDepth, 24);
}

TEST(ExportAudioDialogTest, DefaultsToSelectionWhenNoClipsButLoopIsSet) {
    ExportAudioDialog dialog(/*arrangementEndBeat=*/0.0, /*hasLoopRange=*/true, kLoopStartBeat, kLoopEndBeat,
                             juce::File());
    const auto options = dialog.getOptionsForTest();
    EXPECT_DOUBLE_EQ(options.startBeat, kLoopStartBeat);
    EXPECT_DOUBLE_EQ(options.endBeat, kLoopEndBeat);
}

TEST(ExportAudioDialogTest, SelectionIgnoredWhenNoLoopRangeEvenIfToggled) {
    ExportAudioDialog dialog(kArrangementEndBeat, /*hasLoopRange=*/false, 0.0, 0.0, juce::File());
    // Selection is disabled with no loop range - programmatically toggling it (setEnabled only
    // gates USER interaction) must still fall back to the arrangement rather than send an empty
    // [0,0] range into BounceOptions validation.
    dialog.setUseSelectionForTest(true);
    const auto options = dialog.getOptionsForTest();
    EXPECT_DOUBLE_EQ(options.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(options.endBeat, kArrangementEndBeat);
}

TEST(ExportAudioDialogTest, SwitchingToAiffStepsDownA32BitSelection) {
    ExportAudioDialog dialog(kArrangementEndBeat, /*hasLoopRange=*/false, 0.0, 0.0, juce::File());
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
    ExportAudioDialog dialog(kArrangementEndBeat, /*hasLoopRange=*/false, 0.0, 0.0, juce::File());
    dialog.setSampleRateForTest(96000.0);
    EXPECT_DOUBLE_EQ(dialog.getOptionsForTest().sampleRate, 96000.0);

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("chosen.wav");
    dialog.setDestinationForTest(file);
    EXPECT_EQ(dialog.getDestinationForTest(), file);
}

TEST(ExportAudioDialogTest, TriggerExportFiresOnExportWithTheCurrentOptionsAndDestination) {
    ExportAudioDialog dialog(kArrangementEndBeat, /*hasLoopRange=*/true, kLoopStartBeat, kLoopEndBeat, juce::File());
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
    ExportAudioDialog dialog(kArrangementEndBeat, /*hasLoopRange=*/false, 0.0, 0.0, juce::File());
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
