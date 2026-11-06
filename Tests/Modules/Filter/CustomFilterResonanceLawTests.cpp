// CustomFilterResonanceLawTests.cpp -- the Custom filter's Resonance peak against a measured resonant low-pass:
// peak height and -3 dB width at each Resonance, the peak's shape as the knob turns, the kernel the engine designs
// against the curve the card draws, and no click while Resonance sweeps. The numbers are printed so a run reads
// like the measurement table.
#include "../../ClickBoundHelpers.h"
#include "CustomFilterTestHelpers.h"
#include "Modules/Filter/CustomFilterBlend.h"
#include "Modules/Filter/CustomFilterCutoff.h"
#include "Modules/FilterModule.h"
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace synth;
using namespace synth::test;
using namespace synth::customfilter;

namespace {

/** One row of the measurement: a reference analogue-style resonant low-pass (12 and 24 dB per octave read the
 *  same), cutoff 1 kHz, the peak over the flat pass band and its -3 dB width. Rendered offline; the width is
 *  missing where the low-pass has no peak 3 dB above its own skirt. */
struct Measured {
    float resonance, peakDb, widthOctaves;
};
constexpr Measured kReference[] = {
    {0.10f, 1.95f, 0.0f},    {0.25f, 4.91f, 1.203f},  {0.50f, 9.94f, 0.498f},
    {0.75f, 17.05f, 0.204f}, {0.90f, 25.51f, 0.077f},
};

/** The bell's -3 dB width, in octaves, found by stepping out from the cutoff. */
float bellWidthOctaves(float resonance) {
    const float edge = resonanceBellDb(resonance, 0.0f) - 3.0f;
    float o = 0.0f;
    while (resonanceBellDb(resonance, o) > edge && o < 10.0f)
        o += 0.0005f;
    return 2.0f * o; // the bell is symmetric in octaves
}

CustomFilterShapes flatShape() {
    CustomFilterShapes s;
    s.shapes = {makeShape("flat", {{20.0f, 0.0f}, {20000.0f, 0.0f}}, {0.5f, 0.5f})};
    return s;
}

} // namespace

// Regression test for FRO1184: the peak was 18 * r^2 dB, under 1 dB for the knob's first quarter and then shooting up,
// where a resonant filter grows about 20 dB per unit of Resonance from the start.
TEST(CustomFilterResonanceLaw, PeakAndWidthMatchTheMeasuredResonantLowPass) {
    printf("[ FRO1184 ] resonance  peak dB (ours / measured)  -3 dB width oct (ours / measured)\n");
    for (const auto& row : kReference) {
        const float peak = resonanceBellDb(row.resonance, 0.0f);
        const float width = bellWidthOctaves(row.resonance);
        printf("            %.2f       %6.2f / %6.2f              %6.3f / %6.3f\n", row.resonance, peak, row.peakDb,
               width, row.widthOctaves);
        EXPECT_NEAR(peak, row.peakDb, 1.0f) << row.resonance;
        // Below 0.5 the measured width is set by the low-pass's own skirt more than by its peak; the bell on a
        // flat shape is wider there (1.56 octaves at 0.25 against 1.20), and as narrow from 0.5 up.
        if (row.resonance >= 0.5f)
            EXPECT_NEAR(width / row.widthOctaves, 1.0f, 0.12f) << row.resonance;
    }
    EXPECT_NEAR(resonanceBellDb(1.0f, 0.0f), kMaxResonancePeakDb, 0.01f) << "the reference self-oscillates; ours holds";
}

TEST(CustomFilterResonanceLaw, ThePeakGrowsAndNarrowsSmoothlyAtTheCutoff) {
    float lastPeak = 0.0f, lastWidth = 1.0e9f;
    for (int i = 1; i <= 100; ++i) {
        const float r = (float)i / 100.0f;
        const float peak = resonanceBellDb(r, 0.0f);
        EXPECT_GT(peak, lastPeak) << r;
        EXPECT_LT(peak - lastPeak, 1.5f) << r << ": no jump between two knob steps";
        EXPECT_GT(peak, resonanceBellDb(r, 0.01f)) << r << ": the top is at the cutoff";
        EXPECT_NEAR(resonanceBellDb(r, 0.3f), resonanceBellDb(r, -0.3f), 1e-4f) << r << ": symmetric in octaves";
        const float width = bellWidthOctaves(r);
        if (peak > 3.0f) {
            EXPECT_LT(width, lastWidth) << r;
            lastWidth = width;
        }
        lastPeak = peak;
    }
    EXPECT_GE(resonanceBellDb(0.25f, 0.0f), 3.0f) << "audible by the quarter mark";
    EXPECT_FLOAT_EQ(resonanceBellDb(0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(resonanceBellDb(7.0f, 0.0f), resonanceBellDb(1.0f, 0.0f)) << "held to 1";
    EXPECT_FLOAT_EQ(resonanceBellDb(std::nanf(""), 0.0f), 0.0f);
    EXPECT_NEAR(resonanceBellDb(1.0f, 3.0f), 0.0f, 0.1f) << "far from the cutoff the bell leaves the curve alone";
}

TEST(CustomFilterResonanceLaw, TheKernelHasThePeakTheCardDraws) {
    constexpr float kCutoff = 1000.0f;
    CustomFilterEngine engine;
    engine.setShapes(flatShape());
    printf("[ FRO1184 ] the engine's kernel at a 1 kHz cutoff on a flat shape (what is heard):\n"
           "            resonance  kernel peak dB  drawn peak dB  kernel -3 dB width oct\n");
    for (float r : {0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f}) {
        CustomFilterEngine::Controls c;
        c.cutoffHz = kCutoff;
        c.resonance = r;
        const auto ir = renderImpulse(engine, c, 16384);
        const auto drawn = responseGridDb(flatShape(), 0.5f, 0.5f, 0.0f, 1.0f, kCutoff, r);
        const float drawnPeak = *std::max_element(drawn.begin(), drawn.end());
        const auto freqs = logFrequencies(kCutoff / 4.0f, kCutoff * 4.0f, 2401); // 1/600 octave apart
        std::vector<float> db;
        for (float f : freqs)
            db.push_back(magnitudeDb(ir, f));
        const float heardPeak = *std::max_element(db.begin(), db.end());
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = 0; i < freqs.size(); ++i)
            if (db[i] >= heardPeak - 3.0f) {
                lo = lo == 0.0f ? freqs[i] : lo;
                hi = freqs[i];
            }
        const float width = heardPeak > 3.0f ? std::log2(hi / lo) : 0.0f;
        printf("            %.2f       %6.2f          %6.2f         %6.3f\n", r, heardPeak, drawnPeak, width);
        // At 1 the ring outlasts the 2048-tap kernel (its decay time is about 13 ms at 1 kHz against a 46 ms kernel
        // ending in a taper), so what is heard stops about 1.5 dB under the drawn 32 dB, more at lower cutoffs.
        EXPECT_NEAR(heardPeak, drawnPeak, r < 1.0f ? 1.0f : 2.0f) << r;
        if (width > 0.0f && r < 1.0f)
            EXPECT_NEAR(width / bellWidthOctaves(r), 1.0f, 0.15f) << r << ": the kernel holds the drawn width";
        EXPECT_NEAR(magnitudeDb(ir, kCutoff / 8.0f), 0.0f, 0.5f) << r;
    }
}

// Not a check: with AGENTSYNTH_RESONANCE_SWEEP_WAV set to a file path, writes a 110 Hz saw through the Custom filter
// (flat shape, cutoff 1 kHz) while Resonance sweeps 0 to 1 over 8 s, for listening.
TEST(CustomFilterResonanceLaw, ASawResonanceSweepCanBeWrittenForListening) {
    const auto path = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_RESONANCE_SWEEP_WAV", {});
    if (path.isEmpty())
        GTEST_SKIP() << "set AGENTSYNTH_RESONANCE_SWEEP_WAV to write the sweep";
    constexpr double kRate = 48000.0;
    constexpr int kBlockSize = 256;
    constexpr int kBlocks = (int)(8.0 * kRate) / kBlockSize;
    FilterModule m;
    m.setCustomShapes(flatShape());
    clickbound::setActual(m, "filterType", (float)FilterModule::kCustomTypeIndex);
    clickbound::setActual(m, "cutoff", 1000.0f);
    m.setPlayConfigDetails(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels(), kRate, kBlockSize);
    m.prepareToPlay(kRate, kBlockSize);
    juce::AudioBuffer<float> buffer(m.getTotalNumInputChannels(), kBlockSize);
    juce::AudioBuffer<float> out(1, kBlocks * kBlockSize);
    double phase = 0.0;
    for (int b = 0; b < kBlocks; ++b) {
        clickbound::setActual(m, "resonance", (float)b / (float)(kBlocks - 1));
        buffer.clear();
        for (int i = 0; i < kBlockSize; ++i) {
            buffer.setSample(0, i, 0.05f * (float)(2.0 * phase - 1.0));
            phase = std::fmod(phase + 110.0 / kRate, 1.0);
        }
        juce::MidiBuffer midi;
        m.processBlock(buffer, midi);
        out.copyFrom(0, b * kBlockSize, buffer, 0, 0, kBlockSize);
    }
    juce::File file(path);
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), kRate, 1, 24, {}, 0));
    ASSERT_NE(writer, nullptr);
    writer->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
}

// A Resonance sweep from 0 to 1 and a jump, on a sine at the cutoff (where the peak is tallest), make no click.
TEST(CustomFilterResonanceLaw, AResonanceSweepAndJumpStayWithinTheClickBound) {
    auto run = [](bool sweep) {
        FilterModule m;
        m.setCustomShapes(flatShape());
        clickbound::setActual(m, "filterType", (float)FilterModule::kCustomTypeIndex);
        clickbound::setActual(m, "cutoff", 440.0f);
        clickbound::setActual(m, "resonance", 0.0f);
        m.setPlayConfigDetails(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels(), clickbound::kRate,
                               clickbound::kBlock);
        m.prepareToPlay(clickbound::kRate, clickbound::kBlock);
        clickbound::Input in;
        in.audioChannels = 1;
        in.amplitude = 0.02f; // 32 dB of peak stays under full scale
        const int sweepBlocks = clickbound::kEdgeBlocks - clickbound::kTailBlocks - 4;
        return clickbound::measure(clickbound::render(m, in, {0}, [&](int b) {
            const int k = b - clickbound::kSettleBlocks;
            if (k < 0)
                return;
            const float r = sweep ? juce::jmin(1.0f, (float)(k + 1) / (float)sweepBlocks) : 1.0f;
            clickbound::setActual(m, "resonance", r);
        }));
    };
    const auto sweepSteps = run(true);
    const auto jumpSteps = run(false);
    printf("[ FRO1184 ] click check: sweep edge step %.5f, jump edge step %.5f, steady %.5f / %.5f\n", sweepSteps.edge,
           jumpSteps.edge, sweepSteps.steady, jumpSteps.steady);
    EXPECT_NO_CLICK(sweepSteps);
    EXPECT_NO_CLICK(jumpSteps);
}
