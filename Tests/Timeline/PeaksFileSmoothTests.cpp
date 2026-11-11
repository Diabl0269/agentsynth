// PeaksFileSmoothTests.cpp — the pure column reducers a waveform is drawn from: smoothToColumns (no steps when
// there are fewer buckets than columns), the unchanged zoomed-out path, and sampleColumns (exact columns from raw
// samples) agreeing with the peaks at the one-bucket-per-pixel switch-over.

#include "Timeline/PeaksFile.h"
#include "WaveformTestSignals.h"
#include <gtest/gtest.h>

using synth::PeaksFile;
using namespace waveform_test;

TEST(PeaksFileSmooth, FewerBucketsThanColumnsHasNoFlatSteps) {
    // Regression test for FRO1733: zoomed in, one bucket repeated across many columns drew square steps.
    const auto data = peaksOf(makeNotes(44100.0));
    const int count = (int)data.buckets.size();
    constexpr int kColumns = 1000;
    ASSERT_LT(count, kColumns);

    const auto stepped = PeaksFile::aggregateToColumns(data.buckets, 1, 0, count, kColumns);
    const auto smooth = PeaksFile::smoothToColumns(data.buckets, 1, 0, count, kColumns);
    ASSERT_EQ(smooth.size(), (std::size_t)kColumns);

    // Before the first and after the last bucket centre the end value holds, by design: leave those edges out.
    const std::size_t edge = (std::size_t)(kColumns / count + 1);
    std::vector<float> steppedTop, smoothTop, smoothBottom;
    for (std::size_t i = edge; i + edge < stepped.size(); ++i) {
        steppedTop.push_back(stepped[i].second);
        smoothTop.push_back(smooth[i].second);
        smoothBottom.push_back(smooth[i].first);
    }
    EXPECT_GE(longestRun(steppedTop, 1e-7), 4) << "the old path repeats a bucket across columns";
    EXPECT_LE(longestRun(smoothTop, 1e-7), 2);
    EXPECT_LE(longestRun(smoothBottom, 1e-7), 2);
}

TEST(PeaksFileSmooth, ChangingNoteSineHasNoStepsAtAnyZoomedInRatio) {
    const auto data = peaksOf(makeNotes(48000.0, 0.9));
    const int count = (int)data.buckets.size();
    for (const int columns : {count + 1, count * 3, count * 7, count * 20}) {
        const auto smooth = PeaksFile::smoothToColumns(data.buckets, 1, 0, count, columns);
        std::vector<float> top;
        const std::size_t edge = (std::size_t)(columns / count + 1);
        for (std::size_t i = edge; i + edge < smooth.size(); ++i)
            top.push_back(smooth[i].second);
        EXPECT_LE(longestRun(top, 1e-7), 2) << columns << " columns over " << count << " buckets";
    }
}

TEST(PeaksFileSmooth, BodyStaysInsideTheOutline) {
    const auto data = peaksOf(makeNotes(44100.0));
    const int count = (int)data.buckets.size();
    const auto outline = PeaksFile::smoothToColumns(data.buckets, 1, 0, count, 900);
    const auto body = PeaksFile::smoothRmsToColumns(data.rms, 1, 0, count, 900);
    ASSERT_EQ(body.size(), outline.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        EXPECT_GE(body[i], 0.0f);
        EXPECT_LE(body[i], std::max(outline[i].second, -outline[i].first) + 1e-4f) << "column " << i;
    }
}

TEST(PeaksFileSmooth, InterpolatesBetweenBucketCentres) {
    const std::vector<std::pair<float, float>> buckets{{-1.0f, 1.0f}, {-0.5f, 0.5f}};
    const auto columns = PeaksFile::smoothToColumns(buckets, 1, 0, 2, 8);
    ASSERT_EQ(columns.size(), 8u);
    // The anchor columns (nearest each bucket centre, 2 and 6) carry the bucket exactly; the end values hold outside.
    EXPECT_FLOAT_EQ(columns[0].second, 1.0f);
    EXPECT_FLOAT_EQ(columns[2].second, 1.0f);
    EXPECT_FLOAT_EQ(columns[6].second, 0.5f);
    EXPECT_FLOAT_EQ(columns[7].second, 0.5f);
    for (std::size_t i = 1; i < columns.size(); ++i)
        EXPECT_LE(columns[i].second, columns[i - 1].second + 1e-6f) << "monotone between the two centres";
    EXPECT_NEAR(columns[3].second, 0.875f, 1e-4f);
}

TEST(PeaksFileSmooth, EveryBucketsPeakIsDrawnWhenZoomedIn) {
    // The downsample-skip kind: a lone loud bucket must reach its own height in some column at any zoom.
    std::vector<std::pair<float, float>> buckets(40, {-0.1f, 0.1f});
    buckets[17] = {-0.95f, 0.9f};
    for (const int columns : {41, 57, 100, 333, 1000}) {
        const auto drawn = PeaksFile::smoothToColumns(buckets, 1, 0, 40, columns);
        float top = 0.0f, bottom = 0.0f;
        for (const auto& c : drawn) {
            top = std::max(top, c.second);
            bottom = std::min(bottom, c.first);
        }
        EXPECT_FLOAT_EQ(top, 0.9f) << columns << " columns";
        EXPECT_FLOAT_EQ(bottom, -0.95f) << columns << " columns";
    }
}

TEST(PeaksFileSmooth, ZoomedOutPathIsUnchanged) {
    // At least as many buckets as columns: smoothToColumns IS aggregateToColumns, bit for bit.
    const auto data = peaksOf(makeNotes(44100.0));
    const int count = (int)data.buckets.size();
    for (const int columns : {1, 7, count / 2, count}) {
        EXPECT_EQ(PeaksFile::smoothToColumns(data.buckets, 1, 0, count, columns),
                  PeaksFile::aggregateToColumns(data.buckets, 1, 0, count, columns));
        EXPECT_EQ(PeaksFile::smoothRmsToColumns(data.rms, 1, 0, count, columns),
                  PeaksFile::aggregateRmsToColumns(data.rms, 1, 0, count, columns));
    }
    // Stereo folds the same way too.
    std::vector<std::pair<float, float>> stereo{{-0.2f, 0.3f}, {-0.9f, 0.1f}, {-0.1f, 0.4f}, {-0.3f, 0.8f}};
    EXPECT_EQ(PeaksFile::smoothToColumns(stereo, 2, 0, 2, 2), PeaksFile::aggregateToColumns(stereo, 2, 0, 2, 2));
    const auto folded = PeaksFile::smoothToColumns(stereo, 2, 0, 2, 4);
    EXPECT_FLOAT_EQ(folded[0].first, -0.9f) << "channels fold as min of mins before blending";
}

TEST(PeaksFileSmooth, SampleColumnsMatchThePeaksAtOneBucketPerPixel) {
    // The switch-over: at 256 samples per column the exact columns agree with the peaks. A wrong file-to-sample
    // mapping (e.g. the transport rate) would shift the changing notes by many buckets and fail this.
    const auto mono = makeNotes(44100.0);
    const auto data = peaksOf(mono);
    const int count = (int)data.buckets.size();
    juce::AudioBuffer<float> block(1, (int)mono.size());
    block.copyFrom(0, 0, mono.data(), (int)mono.size());

    const auto exact = PeaksFile::sampleColumns(block, 0, 0.0, 256.0, count - 1);
    const auto peaks = PeaksFile::aggregateToColumns(data.buckets, 1, 0, count - 1, count - 1);
    ASSERT_EQ(exact.minMax.size(), peaks.size());
    for (std::size_t i = 0; i < peaks.size(); ++i) {
        EXPECT_NEAR(exact.minMax[i].first, peaks[i].first, 0.06f) << "column " << i;
        EXPECT_NEAR(exact.minMax[i].second, peaks[i].second, 0.06f) << "column " << i;
        EXPECT_NEAR(exact.rms[i], data.rms[i], 0.03f) << "column " << i;
    }

    // Shifted by 5 buckets (a wrong start) the same comparison must visibly fail, so the check has teeth.
    const auto shifted = PeaksFile::sampleColumns(block, 0, 5.0 * 256.0, 256.0, count - 8);
    float worst = 0.0f;
    for (std::size_t i = 0; i < shifted.minMax.size(); ++i)
        worst = std::max(worst, std::abs(shifted.minMax[i].second - peaks[i].second));
    EXPECT_GT(worst, 0.1f);
}

TEST(PeaksFileSmooth, SampleColumnsFoldChannelsAndReadSilenceOutsideTheBlock) {
    juce::AudioBuffer<float> block(2, 8);
    block.clear();
    block.setSample(0, 2, 0.5f);
    block.setSample(1, 2, -0.75f);
    const auto columns = PeaksFile::sampleColumns(block, 100, 100.0, 4.0, 3); // [100,104] [104,108] [108,112]
    ASSERT_EQ(columns.minMax.size(), 3u);
    EXPECT_FLOAT_EQ(columns.minMax[0].first, -0.75f);
    EXPECT_FLOAT_EQ(columns.minMax[0].second, 0.5f);
    EXPECT_FLOAT_EQ(columns.minMax[2].first, 0.0f);
    EXPECT_FLOAT_EQ(columns.minMax[2].second, 0.0f);
    EXPECT_GT(columns.rms[0], 0.0f);
}

TEST(PeaksFileSmooth, ReadAudioSamplesReturnsTheRequestedFramesAndSilencePastTheEnd) {
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_read_samples.wav");
    const auto mono = makeNotes(48000.0, 0.1);
    ASSERT_TRUE(writeWav(file, 48000.0, mono));
    juce::AudioBuffer<float> out;
    ASSERT_TRUE(PeaksFile::readAudioSamples(file, 100, 200, out));
    ASSERT_EQ(out.getNumSamples(), 200);
    EXPECT_NEAR(out.getSample(0, 50), mono[150], 1e-4f);
    ASSERT_TRUE(PeaksFile::readAudioSamples(file, (std::int64_t)mono.size() - 10, 50, out));
    EXPECT_EQ(out.getNumSamples(), 50);
    EXPECT_EQ(out.getSample(0, 40), 0.0f);
    EXPECT_FALSE(PeaksFile::readAudioSamples(file.getSiblingFile("agentsynth_no_such.wav"), 0, 10, out));
    file.deleteFile();
}
