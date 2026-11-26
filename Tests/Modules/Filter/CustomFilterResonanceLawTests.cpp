// CustomFilterResonanceLawTests.cpp -- the Custom filter's Resonance peak against the measured PZ SVF (the reference
// synth's drawable filter, loaded with a plain low-pass shape, a saw's harmonic response rendered offline at cutoff
// 1 kHz and 3 kHz): peak height and -3 dB width at each Resonance, the skirts around the peak, the peak's shape as the
// knob turns, the kernel the engine designs against the curve the card draws, and no click while Resonance sweeps.
// The numbers are printed so a run reads like the measurement table.
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

/** One row of the measurement: the PZ SVF with a plain low-pass shape, its peak over the same filter at Resonance 0
 *  (dB) and its -3 dB width. The reference's Resonance does nothing under 30% and from 75% up it self-oscillates
 *  (38 to 41 dB, 0.02 octave wide, the same at 75, 80, 90 and 100), which a 2048-tap kernel cannot hold: from 65%
 *  ours holds kMaxResonancePeakDb. The width is only read where the peak stands 3 dB above its hill (45% up). */
struct Measured {
    float resonance, peakDb, widthOctaves;
};
constexpr Measured kReference[] = {
    {0.30f, 0.1f, 0.0f},    {0.35f, 1.4f, 0.0f},    {0.40f, 5.45f, 0.0f},    {0.45f, 11.5f, 0.81f},
    {0.50f, 17.6f, 0.365f}, {0.55f, 23.35f, 0.18f}, {0.60f, 28.35f, 0.105f},
};
/** The skirts, symmetric in octaves, the same from 50% up: dB over Resonance 0 at octaves from the peak. */
struct MeasuredSkirt {
    float octaves, db, tolerance;
};
constexpr MeasuredSkirt kSkirts[] = {
    {0.25f, 15.4f, 3.0f}, {0.5f, 9.5f, 1.0f}, {1.0f, 4.3f, 0.8f}, {2.0f, 1.1f, 0.5f}, {3.0f, 0.3f, 0.3f}};

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

// Regression test: the law was fitted to a resonant low-pass and read
// 4.9 dB at 25% and 9.9 dB at 50%, where the reference synth's PZ SVF, the filter this one answers to, reads 0 and
// 17.6 dB. A stored sweep of that filter (the sound-match table) is the reference.
TEST(CustomFilterResonanceLaw, PeakAndWidthMatchTheMeasuredPzSvf) {
    printf("[ FRO2327 ] resonance  peak dB (ours / measured)  -3 dB width oct (ours / measured)\n");
    for (const auto& row : kReference) {
        const float peak = resonanceBellDb(row.resonance, 0.0f);
        const float width = bellWidthOctaves(row.resonance);
        printf("            %.2f       %6.2f / %6.2f              %6.3f / %6.3f\n", row.resonance, peak, row.peakDb,
               width, row.widthOctaves);
        EXPECT_NEAR(peak, row.peakDb, 0.6f) << row.resonance;
        if (row.widthOctaves > 0.0f)
            EXPECT_NEAR(width / row.widthOctaves, 1.0f, 0.35f)
                << row.resonance; // 0.06-0.1 octave needles: a fit, not an exact trace
    }
    EXPECT_NEAR(resonanceBellDb(0.65f, 0.0f), kMaxResonancePeakDb, 0.01f) << "the reference reads 32.4 dB there";
    EXPECT_NEAR(resonanceBellDb(1.0f, 0.0f), kMaxResonancePeakDb, 0.01f) << "the reference self-oscillates; ours holds";
    EXPECT_NEAR(resonanceBellDb(0.2f, 0.0f), 0.0f, 0.1f) << "the reference does nothing under 30%";
    EXPECT_LT(resonanceBellDb(0.3f, 0.0f), 0.3f);
}

TEST(CustomFilterResonanceLaw, TheSkirtsAreTheMeasuredOnesFromHalfResonanceUp) {
    printf("[ FRO2327 ] resonance  octaves  dB over resonance 0 (ours / measured)\n");
    for (float r : {0.5f, 0.55f, 0.6f, 0.65f, 1.0f})
        for (const auto& row : kSkirts) {
            const float ours = resonanceBellDb(r, row.octaves);
            printf("            %.2f       %.2f      %6.2f / %5.2f\n", r, row.octaves, ours, row.db);
            EXPECT_NEAR(ours, row.db, row.tolerance) << r << " at " << row.octaves << " octaves";
            EXPECT_FLOAT_EQ(resonanceBellDb(r, -row.octaves), ours) << "symmetric";
        }
}

TEST(CustomFilterResonanceLaw, ThePeakGrowsAndNarrowsSmoothlyAtTheCutoff) {
    float lastPeak = 0.0f, lastWidth = 1.0e9f;
    for (int i = 1; i <= 100; ++i) {
        const float r = (float)i / 100.0f;
        const float peak = resonanceBellDb(r, 0.0f);
        if (r <= 0.3f || r > 0.65f)
            EXPECT_GE(peak, lastPeak) << r << ": still while the reference does nothing and once it self-oscillates";
        else
            EXPECT_GT(peak, lastPeak) << r;
        EXPECT_LT(peak - lastPeak, 1.5f) << r << ": no jump between two knob steps";
        EXPECT_GE(peak, resonanceBellDb(r, 0.01f))
            << r << ": the top is at the cutoff (flat on the hill at low resonance)";
        EXPECT_NEAR(resonanceBellDb(r, 0.3f), resonanceBellDb(r, -0.3f), 1e-4f) << r << ": symmetric in octaves";
        const float width = bellWidthOctaves(r);
        if (peak > 3.0f) {
            EXPECT_LE(width, lastWidth + 1e-4f) << r;
            lastWidth = width;
        }
        lastPeak = peak;
    }
    EXPECT_GE(resonanceBellDb(0.4f, 0.0f), 3.0f) << "audible from 40%, as in the reference";
    EXPECT_FLOAT_EQ(resonanceBellDb(0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(resonanceBellDb(7.0f, 0.0f), resonanceBellDb(1.0f, 0.0f)) << "held to 1";
    EXPECT_FLOAT_EQ(resonanceBellDb(std::nanf(""), 0.0f), 0.0f);
    EXPECT_NEAR(resonanceBellDb(1.0f, 3.0f), 0.0f, 0.3f) << "far from the cutoff the bell leaves the curve alone";
}

TEST(CustomFilterResonanceLaw, TheKernelHasThePeakTheCardDraws) {
    constexpr float kCutoff = 1000.0f;
    CustomFilterEngine engine;
    engine.setShapes(flatShape());
    printf("[ FRO1184 ] the engine's kernel at a 1 kHz cutoff on a flat shape (what is heard):\n"
           "            resonance  kernel peak dB  drawn peak dB  kernel -3 dB width oct\n");
    for (float r : {0.1f, 0.4f, 0.5f, 0.6f, 0.65f, 1.0f}) {
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
        // From 65% up the ring outlasts the 2048-tap kernel (its decay time is about 13 ms at 1 kHz against a 46 ms
        // kernel ending in a taper), so what is heard stops about 1.5 dB under the drawn 32 dB, more at lower cutoffs.
        EXPECT_NEAR(heardPeak, drawnPeak, r < 0.65f ? 1.0f : 2.0f) << r;
        if (width > 0.0f && r < 0.65f)
            EXPECT_NEAR(width / bellWidthOctaves(r), 1.0f, 0.15f) << r << ": the kernel holds the drawn width";
        EXPECT_NEAR(magnitudeDb(ir, kCutoff / 8.0f), 0.0f, 0.5f) << r;
    }
}

// what is heard has the skirts too (the kernel, not just the formula), and the card's response view draws the
// same curve the kernel is designed to (bug kind jack-raw-mismatch: two consumers of one law).
TEST(CustomFilterResonanceLaw, TheKernelLiftsOneOctaveOutAndMatchesTheResponseViewOnADrawnShape) {
    constexpr float kCutoff = 1000.0f;
    CustomFilterEngine engine;
    engine.setShapes(flatShape());
    CustomFilterEngine::Controls c;
    c.cutoffHz = kCutoff;
    c.resonance = 0.5f;
    const auto flatIr = renderImpulse(engine, c, 16384);
    EXPECT_GE(magnitudeDb(flatIr, kCutoff * 2.0f), 3.0f) << "an octave above the cutoff";
    EXPECT_GE(magnitudeDb(flatIr, kCutoff / 2.0f), 3.0f) << "an octave below the cutoff";

    CustomFilterShapes drawn;
    drawn.shapes = {
        makeShape("lp", {{20.0f, 0.0f}, {500.0f, 0.0f}, {2000.0f, -12.0f}, {20000.0f, -40.0f}}, {0.5f, 0.5f})};
    engine.setShapes(drawn);
    const auto ir = renderImpulse(engine, c, 16384);
    const auto view = responseGridDb(drawn, 0.5f, 0.5f, 0.0f, 1.0f, kCutoff, 0.5f);
    for (int k = 0; k < (int)view.size(); k += 32) {
        const float hz = CustomFilterShapes::xToHz((float)k / (float)(view.size() - 1));
        if (hz < 100.0f || hz > 10000.0f)
            continue;
        EXPECT_NEAR(magnitudeDb(ir, hz), view[(size_t)k], 0.5f) << hz << " Hz";
    }
}

namespace {
/** A plain low-pass whose knee is at 440 Hz: flat to the knee, 12 dB an octave above it, as the measured PZ SVF shape
 *  is flat to its pole. The Cutoff knob stays at the reference, so the drawn knee and the Peak-mode bell both sit at
 *  440 Hz. */
CustomFilterShapes lowPassShape() {
    CustomFilterShapes s;
    s.shapes = {makeShape("lp", {{20.0f, 0.0f}, {440.0f, 0.0f}, {1760.0f, -24.0f}, {20000.0f, -56.0f}}, {0.5f, 0.5f})};
    return s;
}

struct HeardPeak {
    float peakDb = 0.0f, peakHz = 0.0f, widthOctaves = 0.0f;
};

/** What is heard: the kernel's response at `resonance` over the same filter at Resonance 0, its highest point
 *  and the -3 dB width around it. */
HeardPeak heardPeak(customfilter::ResonanceMode mode, float resonance, const std::vector<float>& freqs,
                    const std::vector<float>& atZero) {
    CustomFilterEngine engine;
    engine.setShapes(lowPassShape());
    CustomFilterEngine::Controls c;
    c.cutoffHz = kReferenceCutoffHz;
    c.resonance = resonance;
    c.resonanceMode = mode;
    const auto ir = renderImpulse(engine, c, 16384);
    std::vector<float> diff;
    for (size_t i = 0; i < freqs.size(); ++i)
        diff.push_back(magnitudeDb(ir, freqs[i]) - atZero[i]);
    const size_t top = (size_t)(std::max_element(diff.begin(), diff.end()) - diff.begin());
    size_t lo = top, hi = top;
    while (lo > 0 && diff[lo - 1] >= diff[top] - 3.0f)
        --lo;
    while (hi + 1 < diff.size() && diff[hi + 1] >= diff[top] - 3.0f)
        ++hi;
    return {diff[top], freqs[top], diff[top] > 3.0f ? std::log2(freqs[hi] / freqs[lo]) : 0.0f};
}
} // namespace

TEST(CustomFilterResonanceLaw, PrintWhatIsHeardOnALowPassInBothModes) {
    const auto freqs = logFrequencies(40.0f, 10000.0f, 1801); // 1/240 octave apart
    CustomFilterEngine engine;
    engine.setShapes(lowPassShape());
    CustomFilterEngine::Controls c0;
    c0.cutoffHz = kReferenceCutoffHz;
    const auto ir0 = renderImpulse(engine, c0, 16384);
    std::vector<float> atZero;
    for (float f : freqs)
        atZero.push_back(magnitudeDb(ir0, f));
    printf("[ FRO2327 ] low-pass, knee 440 Hz: resonance  Peak mode (dB @ Hz, width oct)  Whole Shape (dB @ Hz, width "
           "oct)\n");
    for (float r : {0.25f, 0.4f, 0.5f, 0.6f, 0.65f, 0.75f, 1.0f}) {
        const auto a = heardPeak(customfilter::ResonanceMode::Peak, r, freqs, atZero);
        const auto b = heardPeak(customfilter::ResonanceMode::WholeShape, r, freqs, atZero);
        printf("            %.2f       %6.2f @ %5.0f  %5.3f          %6.2f @ %5.0f  %5.3f\n", r, a.peakDb, a.peakHz,
               a.widthOctaves, b.peakDb, b.peakHz, b.widthOctaves);
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
