// CustomFilterCutoffTests.cpp -- Cutoff and Resonance in the Filter's Custom type: the pure shift and bell
// functions, the response grid the card draws, the designed kernel, and the Filter module's noise through
// a drawn shape (the numbers are printed so a run can be read like a measurement).
#include "../../ClickBoundHelpers.h"
#include "CustomFilterTestHelpers.h"
#include "Modules/Filter/CustomFilterBlend.h"
#include "Modules/Filter/CustomFilterCutoff.h"
#include "Modules/FilterModule.h"
#include <gtest/gtest.h>
#include <juce_dsp/juce_dsp.h>

using namespace synth;
using namespace synth::test;
using namespace synth::customfilter;

namespace {

constexpr int kBlock = 512;

CustomFilterShapes onePeakShape() {
    CustomFilterShapes s;
    s.shapes = {makeShape("peak", {{20.0f, -30.0f}, {1000.0f, 0.0f}, {20000.0f, -30.0f}}, {0.5f, 0.5f})};
    return s;
}

CustomFilterShapes flatShape() {
    CustomFilterShapes s;
    s.shapes = {makeShape("flat", {{20.0f, 0.0f}, {20000.0f, 0.0f}}, {0.5f, 0.5f})};
    return s;
}

/** Hz at which the bell has fallen to half its peak dB, in octaves from the cutoff. */
float halfPeakOctaves(float resonance) {
    float o = 0.0f;
    const float half = 0.5f * resonanceBellDb(resonance, 0.0f);
    while (resonanceBellDb(resonance, o) > half && o < 10.0f)
        o += 0.001f;
    return o;
}

juce::RangedAudioParameter* param(FilterModule& m, const juce::String& id) {
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id)
            return r;
    return nullptr;
}

void setValue(FilterModule& m, const juce::String& id, float plain) {
    auto* p = param(m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(plain));
}

FilterModule& makeCustom(FilterModule& m, const CustomFilterShapes& shapes, float cutoff, float resonance) {
    m.setCustomShapes(shapes);
    setValue(m, "filterType", (float)FilterModule::kCustomTypeIndex);
    setValue(m, "cutoff", cutoff);
    setValue(m, "resonance", resonance);
    return m;
}

/** White noise (fixed seed) through the mono filter; returns ch0 and the largest sample seen. */
std::vector<float> renderNoise(FilterModule& m, int blocks, float noiseAmp = 0.5f) {
    m.prepareToPlay(kTestSampleRate, kBlock);
    juce::AudioBuffer<float> buf(FilterModule::kNumChannels, kBlock);
    juce::MidiBuffer midi;
    juce::Random rng(99);
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        for (int i = 0; i < kBlock; ++i)
            buf.setSample(0, i, noiseAmp * (rng.nextFloat() * 2.0f - 1.0f));
        m.processBlock(buf, midi);
        out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
    }
    return out;
}

/** Average power spectrum (Hann, 4096 FFT, 50% overlap) of `x` after its first `skip` samples, in dB. */
struct Spectrum {
    std::vector<float> power;
    explicit Spectrum(const std::vector<float>& x, int skip = 4096) {
        constexpr int order = 12, n = 1 << order;
        juce::dsp::FFT fft(order);
        juce::dsp::WindowingFunction<float> window(n, juce::dsp::WindowingFunction<float>::hann);
        power.assign(n / 2, 0.0);
        std::vector<float> buf(2 * n);
        int frames = 0;
        for (int s = skip; s + n <= (int)x.size(); s += n / 2) {
            std::fill(buf.begin(), buf.end(), 0.0f);
            std::copy(x.begin() + s, x.begin() + s + n, buf.begin());
            window.multiplyWithWindowingTable(buf.data(), n);
            fft.performFrequencyOnlyForwardTransform(buf.data());
            for (int k = 0; k < n / 2; ++k)
                power[(size_t)k] += buf[(size_t)k] * buf[(size_t)k];
            ++frames;
        }
        for (auto& p : power)
            p /= (float)std::max(1, frames);
    }
    /** Mean power in the band `hz` +- a sixth of an octave, in dB. */
    float bandDb(float hz) const {
        const float binHz = (float)kTestSampleRate / 4096.0f;
        const int lo = std::max(1, (int)(hz * 0.89f / binHz)),
                  hi = std::min((int)power.size() - 1, (int)(hz * 1.12f / binHz));
        double sum = 0.0;
        for (int k = lo; k <= hi; ++k)
            sum += power[(size_t)k];
        return 10.0f * std::log10((float)(sum / (hi - lo + 1)) + 1.0e-20f);
    }
    float peakHz(float lo, float hi) const {
        float best = lo, bestDb = -1.0e9f;
        for (float f : logFrequencies(lo, hi, 300))
            if (const float d = bandDb(f); d > bestDb) {
                bestDb = d;
                best = f;
            }
        return best;
    }
};

} // namespace

// ---- pure functions --------------------------------------------------------------------------------------------

// Regression test for FRO964: Cutoff moves the drawn response.
TEST(CustomFilterCutoff, TotalShiftIsTwelveLog2OfCutoffOverTheReference) {
    EXPECT_FLOAT_EQ(cutoffShiftSemitones(440.0f), 0.0f);
    EXPECT_NEAR(cutoffShiftSemitones(1760.0f), 24.0f, 1e-3);
    EXPECT_NEAR(cutoffShiftSemitones(220.0f), -12.0f, 1e-3);
    EXPECT_NEAR(cutoffShiftSemitones(20.0f), 12.0f * std::log2(20.0f / 440.0f), 1e-3);
    EXPECT_NEAR(cutoffShiftSemitones(1.0e9f), 12.0f * std::log2(20000.0f / 440.0f), 1e-3) << "held to 20 kHz";
    EXPECT_FLOAT_EQ(cutoffShiftSemitones(std::nanf("")), 0.0f);
    EXPECT_NEAR(cutoffX(20.0f), 0.0f, 1e-6);
    EXPECT_NEAR(cutoffX(20000.0f), 1.0f, 1e-6);
}

TEST(CustomFilterCutoff, TheBellPeaksAtEighteenTimesResonanceSquaredAndNarrowsAsResonanceRises) {
    EXPECT_NEAR(resonanceBellDb(1.0f, 0.0f), 18.0f, 1e-4);
    EXPECT_NEAR(resonanceBellDb(0.5f, 0.0f), 4.5f, 1e-4);
    EXPECT_NEAR(resonanceBellDb(0.1f, 0.0f), 0.18f, 1e-4);
    EXPECT_FLOAT_EQ(resonanceBellDb(0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(resonanceBellDb(7.0f, 0.0f), 18.0f) << "held to 1";
    EXPECT_GT(halfPeakOctaves(0.3f), halfPeakOctaves(0.6f));
    EXPECT_GT(halfPeakOctaves(0.6f), halfPeakOctaves(1.0f));
    EXPECT_NEAR(halfPeakOctaves(1.0f), 1.1774f / 6.0f, 0.01f) << "a sixth of an octave sigma at resonance 1";
}

TEST(CustomFilterCutoff, TheResponseGridAtTheReferenceCutoffAndNoResonanceIsTheOldGridExactly) {
    const auto shapes = CustomFilterShapes::defaultShapes();
    for (float shift : {0.0f, 7.0f, -12.0f})
        EXPECT_EQ(responseGridDb(shapes, 0.3f, 0.4f, shift, 1.3f, 440.0f, 0.0f),
                  responseGridDb(shapes, 0.3f, 0.4f, shift, 1.3f))
            << shift;
}

TEST(CustomFilterCutoff, CutoffMovesTheGridByItsSemitonesAndResonanceOneAddsAnEighteenDbPeakAtTheCutoff) {
    const auto shapes = onePeakShape();
    EXPECT_EQ(responseGridDb(shapes, 0.5f, 0.5f, 5.0f, 1.0f, 1760.0f, 0.0f),
              responseGridDb(shapes, 0.5f, 0.5f, 29.0f, 1.0f));

    for (float cutoff : {440.0f, 1760.0f, 6000.0f}) {
        const auto flat = responseGridDb(flatShape(), 0.5f, 0.5f, 0.0f, 0.0f, cutoff, 1.0f);
        const int expectedIndex = (int)std::lround(cutoffX(cutoff) * (float)(kGridSize - 1));
        const int peakIndex = (int)(std::max_element(flat.begin(), flat.end()) - flat.begin());
        EXPECT_NEAR(peakIndex, expectedIndex, 1) << cutoff << " Hz: the bell follows the cutoff";
        EXPECT_NEAR(flat[(size_t)peakIndex], 18.0f, 0.1f);
    }
    // Narrower at resonance 1 than at 0.7: count grid points above 3 dB.
    auto above = [&](float r) {
        const auto g = responseGridDb(flatShape(), 0.5f, 0.5f, 0.0f, 0.0f, 1000.0f, r);
        return (int)std::count_if(g.begin(), g.end(), [](float v) { return v > 3.0f; });
    };
    EXPECT_LT(above(1.0f), above(0.8f));
}

// ---- the designed kernel ---------------------------------------------------------------------------------------

TEST(CustomFilterCutoff, TheKernelPeakFollowsTheCutoffByTwoOctavesForTwentyFourSemitones) {
    CustomFilterEngine engine;
    engine.setShapes(onePeakShape());
    CustomFilterEngine::Controls c;
    c.cutoffHz = 440.0f;
    const auto base = renderImpulse(engine, c);
    c.cutoffHz = 1760.0f;
    const auto up = renderImpulse(engine, c);
    auto peak = [](const std::vector<float>& ir) {
        float best = 100.0f, bestDb = -1.0e9f;
        for (float f : logFrequencies(100.0f, 16000.0f, 500))
            if (const float d = magnitudeDb(ir, f); d > bestDb) {
                bestDb = d;
                best = f;
            }
        return best;
    };
    EXPECT_NEAR(peak(base), 1000.0f, 40.0f);
    EXPECT_NEAR(peak(up) / peak(base), 4.0f, 0.15f);
}

TEST(CustomFilterCutoff, ResonanceOneRaisesTheKernelByEighteenDbAtTheCutoffAndLeavesTheRestAlone) {
    CustomFilterEngine engine;
    engine.setShapes(flatShape());
    CustomFilterEngine::Controls c;
    c.cutoffHz = 2000.0f;
    c.resonance = 1.0f;
    const auto ir = renderImpulse(engine, c);
    EXPECT_NEAR(magnitudeDb(ir, 2000.0f), 18.0f, 1.0f);
    EXPECT_NEAR(magnitudeDb(ir, 500.0f), 0.0f, 0.5f);
    EXPECT_NEAR(magnitudeDb(ir, 8000.0f), 0.0f, 0.5f);
    c.resonance = 0.0f;
    EXPECT_NEAR(magnitudeDb(renderImpulse(engine, c), 2000.0f), 0.0f, 0.2f);
}

TEST(CustomFilterCutoff, ExtremeCutoffAndResonanceStayFinite) {
    for (float cutoff : {20.0f, 20000.0f}) {
        FilterModule m;
        makeCustom(m, CustomFilterShapes::defaultShapes(), cutoff, 1.0f);
        setValue(m, "depth", 2.0f);
        const auto out = renderNoise(m, 24);
        for (float s : out)
            ASSERT_TRUE(std::isfinite(s)) << cutoff;
    }
    CustomFilterEngine engine;
    CustomFilterEngine::Controls c;
    c.cutoffHz = std::nanf("");
    c.resonance = std::nanf("");
    for (float s : renderImpulse(engine, c))
        ASSERT_TRUE(std::isfinite(s));
}

// ---- the module, with noise ------------------------------------------------------------------------------------

TEST(CustomFilterCutoff, NoiseThroughTheModuleShowsTheDrawnPeakMoveTwoOctavesWithCutoffAndResonanceRaiseIt) {
    auto levels = [&](float cutoff, float resonance, float* peakHz) {
        FilterModule m;
        makeCustom(m, onePeakShape(), cutoff, resonance);
        const Spectrum spec(renderNoise(m, 200));
        if (peakHz != nullptr)
            *peakHz = spec.peakHz(100.0f, 12000.0f);
        return std::array<float, 3>{spec.bandDb(1000.0f), spec.bandDb(1760.0f), spec.bandDb(4000.0f)};
    };
    float peak440 = 0.0f, peak1760 = 0.0f, peakRes = 0.0f;
    const auto a = levels(440.0f, 0.0f, &peak440);
    const auto b = levels(1760.0f, 0.0f, &peak1760);
    const auto c = levels(1760.0f, 1.0f, &peakRes);
    printf("[ FRO964 ] band dB at 1000 / 1760 / 4000 Hz, noise through a 1 kHz drawn peak:\n"
           "           cutoff  440, res 0: %.1f / %.1f / %.1f  (peak %.0f Hz)\n"
           "           cutoff 1760, res 0: %.1f / %.1f / %.1f  (peak %.0f Hz)\n"
           "           cutoff 1760, res 1: %.1f / %.1f / %.1f  (peak %.0f Hz)\n",
           a[0], a[1], a[2], peak440, b[0], b[1], b[2], peak1760, c[0], c[1], c[2], peakRes);
    EXPECT_NEAR(peak440, 1000.0f, 120.0f);
    EXPECT_NEAR(peak1760 / peak440, 4.0f, 0.5f) << "cutoff 440 -> 1760 is 24 semitones, two octaves";
    EXPECT_GT(a[0] - a[2], 8.0f) << "at cutoff 440 the peak is at 1 kHz";
    EXPECT_GT(b[2] - a[2], 12.0f) << "at cutoff 1760 the 4 kHz band rises";
    EXPECT_GT(a[0] - b[0], 8.0f) << "and the 1 kHz band falls";
    EXPECT_GT(c[1] - b[1], 12.0f) << "resonance 1 raises the level near the cutoff";
}

TEST(CustomFilterCutoff, TheDesignAtTheReferenceCutoffAndNoResonanceIsTheOldDesignExactly) {
    CustomFilterKernelDesigner designer;
    designer.prepare(kTestSampleRate);
    const auto table = CustomFilterEngine::renderTable(CustomFilterShapes::defaultShapes());
    std::vector<float> grid(table->db.begin(), table->db.begin() + CustomFilterKernelDesigner::kGridSize);
    std::vector<float> oldKernel(CustomFilterKernelDesigner::kKernelLength), newKernel(oldKernel.size());
    designer.design(grid.data(), 3.0f, 1.2f, oldKernel.data());
    designer.design(grid.data(), 3.0f, 1.2f, 0.0f, 440.0f, newKernel.data());
    EXPECT_EQ(oldKernel, newKernel);
}

// Regression test for FRO964: a fast cutoff LFO or a cutoff jump makes no click.
TEST(CustomFilterCutoff, AFastCutoffLfoAndACutoffJumpStayWithinTheClickBound) {
    auto gentle = [] {
        CustomFilterShapes s;
        s.shapes = {makeShape("tilt", {{20.0f, -3.0f}, {2000.0f, 3.0f}, {20000.0f, -3.0f}}, {0.5f, 0.5f})};
        return s;
    };
    constexpr int kCvChannel = 1; // mono Cutoff CV
    auto run = [&](bool lfo, bool jump) {
        FilterModule m;
        makeCustom(m, gentle(), 440.0f, 0.5f);
        m.setPlayConfigDetails(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels(), clickbound::kRate,
                               clickbound::kBlock);
        m.prepareToPlay(clickbound::kRate, clickbound::kBlock);
        clickbound::Input in;
        in.audioChannels = 1;
        double lfoPhase = 0.0;
        juce::AudioBuffer<float> buf(m.getTotalNumInputChannels(), clickbound::kBlock);
        std::vector<float> out;
        double phase = 0.0;
        const double step = 2.0 * juce::MathConstants<double>::pi * in.hz / clickbound::kRate;
        for (int b = 0; b < clickbound::kSettleBlocks + clickbound::kEdgeBlocks; ++b) {
            if (jump && b == clickbound::kSettleBlocks)
                setValue(m, "cutoff", 1760.0f);
            buf.clear();
            for (int i = 0; i < clickbound::kBlock; ++i)
                buf.setSample(0, i, in.amplitude * (float)std::sin(phase + step * i));
            phase += step * clickbound::kBlock;
            if (lfo && b >= clickbound::kSettleBlocks) {
                lfoPhase += 2.0 * juce::MathConstants<double>::pi * 40.0 * clickbound::kBlock / clickbound::kRate;
                juce::FloatVectorOperations::fill(buf.getWritePointer(kCvChannel), 0.3f * (float)std::sin(lfoPhase),
                                                  clickbound::kBlock);
            }
            juce::MidiBuffer midi;
            m.processBlock(buf, midi);
            out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + clickbound::kBlock);
        }
        return clickbound::measure({out});
    };
    const auto lfoSteps = run(true, false);
    const auto jumpSteps = run(false, true);
    printf("[ FRO964 ] click check: LFO edge step %.4f, jump edge step %.4f, steady step %.4f / %.4f\n", lfoSteps.edge,
           jumpSteps.edge, lfoSteps.steady, jumpSteps.steady);
    EXPECT_NO_CLICK(lfoSteps);
    EXPECT_NO_CLICK(jumpSteps);
}
