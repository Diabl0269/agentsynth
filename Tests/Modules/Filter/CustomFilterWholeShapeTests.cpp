// CustomFilterWholeShapeTests.cpp -- the Custom filter's Whole-shape Resonance mode: the law on the dB curve against
// a measured drawable pole-zero filter (cut regions rise, the shape's own peaks sharpen), the kernel against the curve
// the views draw, Peak mode unchanged, and the loudness of a rendered saw kept as Resonance turns up. The numbers are
// printed so a run reads like the measurement table.
#include "CustomFilterTestHelpers.h"
#include "Modules/Filter/CustomFilterBlend.h"
#include "Modules/Filter/CustomFilterCutoff.h"
#include "Modules/Filter/CustomFilterWholeShape.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <numeric>

using namespace synth;
using namespace synth::test;
using namespace synth::customfilter;

namespace {

/** A vowel-like low-pass: flat to 400 Hz, then formant peaks standing out of a deep cut region. */
CustomFilterShapes vowelShape() {
    CustomFilterShapes s;
    s.shapes = {makeShape("vowel",
                          {{20.0f, 0.0f},
                           {400.0f, 0.0f},
                           {650.0f, -36.0f},
                           {900.0f, -20.0f},
                           {1150.0f, -40.0f},
                           {1800.0f, -24.0f},
                           {2300.0f, -46.0f},
                           {3000.0f, -30.0f},
                           {4200.0f, -48.0f},
                           {20000.0f, -48.0f}},
                          {0.5f, 0.5f})};
    return s;
}

CustomFilterShapes lowPassShape() {
    CustomFilterShapes s;
    s.shapes = {makeShape("lp", {{20.0f, 0.0f}, {500.0f, 0.0f}, {2000.0f, -24.0f}, {20000.0f, -48.0f}}, {0.5f, 0.5f})};
    return s;
}

std::vector<float> wholeShape(std::vector<float> db, float resonance) {
    std::vector<float> scratch((size_t)wholeShapeScratchSize((int)db.size()));
    applyWholeShapeResonance(db.data(), (int)db.size(), resonance, scratch.data());
    return db;
}

float gridHz(int k) { return CustomFilterShapes::xToHz((float)k / (float)(kGridSize - 1)); }

/** Median of `after - before` over grid points whose frequency lies in [lo, hi]. */
float medianLift(const std::vector<float>& before, const std::vector<float>& after, float lo, float hi) {
    std::vector<float> d;
    for (int k = 0; k < kGridSize; ++k)
        if (const float hz = gridHz(k); hz >= lo && hz <= hi)
            d.push_back(after[(size_t)k] - before[(size_t)k]);
    std::nth_element(d.begin(), d.begin() + (long)d.size() / 2, d.end());
    return d[d.size() / 2];
}

float dbAt(const std::vector<float>& db, float hz) {
    return db[(size_t)std::lround(CustomFilterShapes::hzToX(hz) * (float)(kGridSize - 1))];
}

} // namespace

TEST(CustomFilterWholeShape, ResonanceZeroLeavesTheCurveAlone) {
    const auto base = responseGridDb(vowelShape(), 0.5f, 0.5f, 0.0f, 1.0f);
    EXPECT_EQ(wholeShape(base, 0.0f), base);
    EXPECT_EQ(wholeShape(base, std::nanf("")), base);
    EXPECT_EQ(responseGridDb(vowelShape(), 0.5f, 0.5f, 0.0f, 1.0f, 440.0f, 0.0f, ResonanceMode::WholeShape), base);
    EXPECT_FLOAT_EQ(wholeShapeFill(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(wholeShapeFill(5.0f), wholeShapeFill(1.0f)) << "held to 1";
}

// The measured pole-zero filter (vowel shape, cutoff 300 Hz to 4 kHz): at 25% its cut region rises 5-10 dB, at 50% by
// 13-48 dB with peaks on the shape's own features, from 75% everything lifts. Ours, on a drawn vowel at the same cut
// depth (-20 to -48 dB, so a little less room to rise than the measured -50 to -80 dB).
TEST(CustomFilterWholeShape, CutRegionsRiseAndTheShapesOwnPeaksSharpenAsResonanceTurns) {
    const auto base = responseGridDb(vowelShape(), 0.5f, 0.5f, 0.0f, 1.0f);
    printf("[ FRO2240 ] resonance  cut-region lift vs pass band dB (median 600 Hz-10 kHz)  900 Hz peak over its dips "
           "dB  top dB\n");
    float lastLift = 0.0f;
    for (float r : {0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto out = wholeShape(base, r);
        // Against the pass band: the level stage moves the whole curve, the cut region rises toward the pass band.
        const float lift = medianLift(base, out, 600.0f, 10000.0f) - (dbAt(out, 100.0f) - dbAt(base, 100.0f));
        const float relief = dbAt(out, 900.0f) - 0.5f * (dbAt(out, 650.0f) + dbAt(out, 1150.0f));
        const float top = *std::max_element(out.begin(), out.end());
        printf("            %.2f       %6.1f                                     %6.1f                        %5.1f\n",
               r, lift, relief, top);
        EXPECT_GT(lift, lastLift) << r << ": more resonance fills more";
        lastLift = lift;
        EXPECT_LE(top, 30.0f) << "the design ceiling";
        if (r == 0.25f)
            EXPECT_GE(lift, 3.0f);
        if (r == 0.5f) {
            EXPECT_GE(lift, 10.0f) << "the measured filter rises 13-48 dB at 50%";
            const float baseRelief = dbAt(base, 900.0f) - 0.5f * (dbAt(base, 650.0f) + dbAt(base, 1150.0f));
            EXPECT_GT(relief, baseRelief) << "the 900 Hz formant stands out more";
        }
    }
    // The formant's peak stays where it was drawn: resonance sharpens the shape's features, it does not move them.
    const auto out = wholeShape(base, 0.6f);
    int best = 0;
    for (int k = 0; k < kGridSize; ++k)
        if (const float hz = gridHz(k); hz > 750.0f && hz < 1050.0f && out[(size_t)k] > out[(size_t)best])
            best = k;
    EXPECT_NEAR(std::log2(gridHz(best) / 900.0f), 0.0f, 0.1f);
}

// A plain drawn low-pass resonates at its own knee, as a resonant low-pass does, without a cutoff bell.
TEST(CustomFilterWholeShape, APlainLowPassGetsAHumpAtItsKnee) {
    const auto base = responseGridDb(lowPassShape(), 0.5f, 0.5f, 0.0f, 1.0f);
    const auto out = wholeShape(base, 0.75f);
    const auto top = std::max_element(out.begin(), out.end());
    const float topHz = gridHz((int)(top - out.begin()));
    printf("[ FRO2240 ] low-pass at 75%%: hump %.1f dB at %.0f Hz, pass band %.1f dB\n", *top, topHz,
           dbAt(out, 100.0f));
    EXPECT_GT(*top - dbAt(out, 100.0f), 2.0f) << "a hump over the pass band";
    EXPECT_NEAR(std::log2(topHz / 500.0f), 0.0f, 0.5f) << "at the drawn knee";
}

// Peak mode is the design it was before the mode existed, sample for sample (old patches sound the same).
TEST(CustomFilterWholeShape, PeakModeDesignsTheSameKernelAsBefore) {
    CustomFilterKernelDesigner d;
    d.prepare(kTestSampleRate);
    const auto grid = blendedGridDb(vowelShape(), 0.5f, 0.5f);
    std::vector<float> before((size_t)CustomFilterKernelDesigner::kKernelLength);
    std::vector<float> after(before.size());
    d.design(grid.data(), 3.0f, 1.2f, 0.6f, 1000.0f, before.data());
    d.design(grid.data(), 3.0f, 1.2f, 0.6f, 1000.0f, ResonanceMode::Peak, after.data());
    EXPECT_EQ(before, after);
    d.design(grid.data(), 3.0f, 1.2f, 0.6f, 1000.0f, ResonanceMode::WholeShape, after.data());
    EXPECT_NE(before, after);
}

// What is heard is what the card's Show response and the shape window draw (bug kind jack-raw-mismatch: two consumers
// of one law), in Whole shape too.
TEST(CustomFilterWholeShape, TheKernelMatchesTheResponseView) {
    for (float r : {0.25f, 0.5f, 0.75f}) {
        for (float cutoff : {440.0f, 1000.0f}) {
            CustomFilterEngine engine;
            engine.setShapes(vowelShape());
            CustomFilterEngine::Controls c;
            c.cutoffHz = cutoff;
            c.resonance = r;
            c.resonanceMode = ResonanceMode::WholeShape;
            const auto ir = renderImpulse(engine, c, 16384);
            const auto view =
                responseGridDb(vowelShape(), 0.5f, 0.5f, 0.0f, 1.0f, cutoff, r, ResonanceMode::WholeShape);
            float worst = 0.0f;
            for (int k = 0; k < kGridSize; k += 16) {
                const float hz = gridHz(k);
                if (hz < 100.0f || hz > 10000.0f || view[(size_t)k] < -60.0f)
                    continue;
                worst = std::max(worst, std::abs(magnitudeDb(ir, hz) - view[(size_t)k]));
            }
            printf("[ FRO2240 ] kernel vs view, resonance %.2f cutoff %.0f Hz: worst %.2f dB\n", r, cutoff, worst);
            EXPECT_LT(worst, 1.0f) << r << " " << cutoff;
        }
    }
}

TEST(CustomFilterWholeShape, ChangingOnlyTheModeRedesignsTheKernel) {
    CustomFilterEngine engine;
    engine.setShapes(vowelShape());
    engine.prepare(kTestSampleRate);
    CustomFilterEngine::Controls c;
    c.resonance = 0.5f;
    std::vector<float> buf(256, 0.0f);
    float* ch[1] = {buf.data()};
    engine.process(ch, 1, 256, c);
    const int designs = engine.getDesignCount();
    engine.process(ch, 1, 256, c);
    EXPECT_EQ(engine.getDesignCount(), designs) << "nothing moved";
    c.resonanceMode = ResonanceMode::WholeShape;
    engine.process(ch, 1, 256, c);
    EXPECT_GT(engine.getDesignCount(), designs);
}

namespace {

struct Level {
    float rmsDb, peak;
};

/** A 110 Hz saw (amplitude `amp`) through the engine for 2 s at `c`, measured after the first 0.5 s. Optionally
 *  written as a 24-bit WAV to `wav`. */
Level renderSaw(const CustomFilterShapes& shapes, const CustomFilterEngine::Controls& c, float amp,
                const juce::File& wav = {}) {
    constexpr int kBlock = 256;
    const int total = (int)(2.0 * kTestSampleRate) / kBlock * kBlock;
    CustomFilterEngine engine;
    engine.setShapes(shapes);
    engine.prepare(kTestSampleRate);
    std::vector<float> out((size_t)total);
    double phase = 0.0;
    for (int start = 0; start < total; start += kBlock) {
        for (int i = 0; i < kBlock; ++i) {
            out[(size_t)(start + i)] = amp * (float)(2.0 * phase - 1.0);
            phase = std::fmod(phase + 110.0 / kTestSampleRate, 1.0);
        }
        float* ch[1] = {out.data() + start};
        engine.process(ch, 1, kBlock, c);
    }
    const int from = (int)(0.5 * kTestSampleRate);
    double sum = 0.0;
    float peak = 0.0f;
    for (int i = from; i < total; ++i) {
        sum += (double)out[(size_t)i] * out[(size_t)i];
        peak = std::max(peak, std::abs(out[(size_t)i]));
    }
    if (wav != juce::File()) {
        wav.deleteFile();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(new juce::FileOutputStream(wav), kTestSampleRate, 1, 24, {}, 0));
        if (writer != nullptr) {
            const float* chans[1] = {out.data()};
            writer->writeFromFloatArrays(chans, 1, total);
        }
    }
    return {(float)(10.0 * std::log10(std::max(1.0e-20, sum / (double)(total - from)))), peak};
}

} // namespace

// Turning Resonance up in Whole shape keeps the loudness: a saw through the vowel and the low-pass stays within 3 dB
// of its level at resonance 0 at every setting, and a saw peaking at -6 dBFS never reaches full scale. With
// AGENTSYNTH_WHOLE_SHAPE_RENDER_DIR set, every render is also written there as a WAV for listening.
TEST(CustomFilterWholeShape, ASawKeepsItsLoudnessAsResonanceTurnsUp) {
    const auto dirPath = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_WHOLE_SHAPE_RENDER_DIR", {});
    const juce::File dir = dirPath.isNotEmpty() ? juce::File(dirPath) : juce::File();
    if (dir != juce::File())
        dir.createDirectory();
    constexpr float kAmp = 0.5f;
    struct Case {
        const char* name;
        CustomFilterShapes shapes;
        float cutoff;
    };
    const Case cases[] = {{"vowel", vowelShape(), 440.0f},
                          {"vowel-1k", vowelShape(), 1000.0f},
                          {"lowpass", lowPassShape(), 440.0f},
                          {"default", CustomFilterShapes::defaultShapes(), 440.0f}};
    printf("[ FRO2240 ] shape     mode        res   RMS dB (vs res 0)   peak\n");
    for (const auto& kase : cases) {
        for (auto mode : {ResonanceMode::Peak, ResonanceMode::WholeShape}) {
            const char* modeName = mode == ResonanceMode::Peak ? "peak" : "whole";
            float rms0 = 0.0f;
            for (float r : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                CustomFilterEngine::Controls c;
                c.cutoffHz = kase.cutoff;
                c.resonance = r;
                c.resonanceMode = mode;
                const auto file = dir != juce::File()
                                      ? dir.getChildFile(juce::String(kase.name) + "-" + modeName + "-res" +
                                                         juce::String((int)std::lround(r * 100.0f)) + ".wav")
                                      : juce::File();
                const auto level = renderSaw(kase.shapes, c, kAmp, file);
                if (r == 0.0f)
                    rms0 = level.rmsDb;
                printf("            %-9s %-6s     %.2f  %6.1f (%+5.1f)      %.3f\n", kase.name, modeName, r,
                       level.rmsDb, level.rmsDb - rms0, level.peak);
                if (mode == ResonanceMode::WholeShape) {
                    EXPECT_NEAR(level.rmsDb, rms0, 3.0f) << kase.name << " " << r;
                    EXPECT_LT(level.peak, 1.0f) << kase.name << " " << r << ": no clipping";
                }
            }
        }
    }
}
