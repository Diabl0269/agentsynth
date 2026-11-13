// Oscillator alias guard: every edged waveform, over the pitch range, keeps its inharmonic energy below 16 kHz under a
// measured bound, and keeps the top octave of its harmonics at the ideal level.
//
// Regression test for FRO1347: a two-sample polyBLEP left inharmonic energy at -49 dB (C4) to -35 dB (C8) on a Saw and
// drooped the 10-20 kHz band by 2.5 dB.

#include "Modules/OscillatorModule.h"

#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace {

constexpr double kAliasRate = 48000.0;
constexpr int kAliasBlock = 512;
constexpr int kAliasFftSize = 1 << 16;
constexpr int kAliasSineIndex = 0;

// Worst measured -103.9 dB (the old generators: -35 dB); the pass mark is -80 dB.
constexpr double kAliasBoundDb = -95.0;
// Worst measured -118.8 dB under vibrato (the old generators: -53.7 dB on a Triangle).
constexpr double kBelowNoteBoundDb = -105.0;
constexpr double kInharmonicCeilingHz = 16000.0;
// Worst measured -0.19 dB; the reference synth is flat here, and the old generators droop by 2.7 dB.
constexpr double kTopBandToleranceDb = 0.5;

void setAliasParam(juce::AudioProcessor& module, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

double noteHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

using FineCv = float (*)(int sample);

// A single plain voice at `note`, past its start, one FFT long; `fineCv` drives the Fine jack in semitones.
std::vector<float> renderNote(int waveform, int note, float pulseWidth, FineCv fineCv = nullptr) {
    auto osc = std::make_unique<OscillatorModule>();
    setAliasParam(*osc, "waveform", (float)waveform);
    setAliasParam(*osc, "pulseWidth", pulseWidth);
    setAliasParam(*osc, "randomPhase", 0.0f);
    setAliasParam(*osc, "unison", 1.0f);
    osc->prepareToPlay(kAliasRate, kAliasBlock);

    const int skip = (int)(0.05 * kAliasRate);
    std::vector<float> out;
    out.reserve((size_t)kAliasFftSize);
    juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kAliasBlock);
    for (int pos = 0; (int)out.size() < kAliasFftSize; pos += kAliasBlock) {
        buf.clear();
        if (fineCv != nullptr)
            for (int i = 0; i < kAliasBlock; ++i)
                buf.setSample(4, i, fineCv(pos + i));
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), 0);
        osc->processBlock(buf, midi);
        for (int i = 0; i < kAliasBlock && (int)out.size() < kAliasFftSize; ++i)
            if (pos + i >= skip)
                out.push_back(buf.getSample(0, i));
    }
    return out;
}

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

// Power per FFT bin under a Kaiser window: its side lobes sit near -150 dB, far under anything measured here, and its
// main lobe still fits the harmonic mask.
std::vector<double> powerSpectrum(const std::vector<float>& signal) {
    constexpr double kBeta = 20.0;
    std::vector<std::complex<double>> x((size_t)kAliasFftSize);
    for (int i = 0; i < kAliasFftSize; ++i) {
        const double r = 2.0 * i / (kAliasFftSize - 1) - 1.0;
        x[(size_t)i] = signal[(size_t)i] * besselI0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r)));
    }
    for (size_t i = 1, j = 0; i < x.size(); ++i) {
        size_t bit = x.size() >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= x.size(); len <<= 1) {
        const double angle = -juce::MathConstants<double>::twoPi / (double)len;
        for (size_t start = 0; start < x.size(); start += len)
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = x[start + k];
                const auto v = x[start + k + len / 2] * std::polar(1.0, angle * (double)k);
                x[start + k] = u + v;
                x[start + k + len / 2] = u - v;
            }
    }
    std::vector<double> power((size_t)kAliasFftSize / 2 + 1);
    for (size_t i = 0; i < power.size(); ++i)
        power[i] = std::norm(x[i]);
    return power;
}

double binHz(size_t bin) { return (double)bin * kAliasRate / kAliasFftSize; }

// The first harmonic's real frequency, so a rounded phase increment does not read as inharmonic.
double measuredFundamental(const std::vector<double>& power, double nominalHz) {
    double weighted = 0.0, total = 0.0;
    for (size_t b = 1; b < power.size(); ++b)
        if (std::abs(binHz(b) - nominalHz) < nominalHz * 0.05) {
            weighted += binHz(b) * power[b];
            total += power[b];
        }
    return weighted / total;
}

bool isHarmonicBin(size_t bin, double f0) {
    const double f = binHz(bin);
    const double n = std::max(1.0, std::round(f / f0));
    return std::abs(f - n * f0) < std::max(6.0, 0.002 * n * f0);
}

/** Inharmonic energy below the ceiling, relative to the harmonics, in dB. */
double inharmonicDb(const std::vector<float>& signal, double nominalHz) {
    const auto power = powerSpectrum(signal);
    const double f0 = measuredFundamental(power, nominalHz);
    double harmonic = 0.0, inharmonic = 0.0;
    for (size_t b = 1; b < power.size(); ++b) {
        if (isHarmonicBin(b, f0))
            harmonic += power[b];
        else if (binHz(b) > 20.0 && binHz(b) < kInharmonicCeilingHz)
            inharmonic += power[b];
    }
    return 10.0 * std::log10((inharmonic + 1e-30) / (harmonic + 1e-30));
}

/** The harmonics inside 10-20 kHz against an ideal 1/n^order series anchored on the fundamental, in dB. */
double topBandDb(const std::vector<float>& signal, double nominalHz, int order, int step) {
    const auto power = powerSpectrum(signal);
    const double f0 = measuredFundamental(power, nominalHz);
    auto energyAt = [&](double f) {
        double e = 0.0;
        for (size_t b = 1; b < power.size(); ++b)
            if (std::abs(binHz(b) - f) < 6.0)
                e += power[b];
        return e;
    };
    const double first = energyAt(f0);
    double measured = 0.0, ideal = 0.0;
    for (int n = 1; n * f0 < 20000.0; n += step)
        if (n * f0 >= 10240.0) {
            measured += energyAt(n * f0);
            ideal += first / std::pow((double)n, 2.0 * order);
        }
    return 10.0 * std::log10(measured / ideal);
}

} // namespace

// Walks the module's own Waveform list, so a new edged waveform joins the guard without being named here.
TEST(OscillatorAlias, InharmonicEnergyStaysUnderTheBoundOverPitchAndWaveform) {
    auto probe = std::make_unique<OscillatorModule>();
    auto* waveform = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(probe.get(), "waveform"));
    ASSERT_NE(waveform, nullptr);
    for (int w = 0; w < waveform->choices.size(); ++w) {
        if (w == kAliasSineIndex)
            continue;
        const bool hasWidth = waveform->choices[w] == "Square";
        for (const float width : {50.0f, 10.0f, 73.0f}) {
            if (width != 50.0f && !hasWidth)
                continue;
            for (int note = 60; note <= 108; note += 3) {
                const double db = inharmonicDb(renderNote(w, note, width), noteHz(note));
                EXPECT_LT(db, kAliasBoundDb) << waveform->choices[w] << " note " << note << " width " << width;
            }
        }
    }
}

// A moving pitch has no harmonics to mask, but anything well below the lowest fundamental can only be aliasing.
TEST(OscillatorAlias, VibratoLeavesNothingBelowTheNote) {
    constexpr int kNote = 96;
    FineCv vibrato = [](int sample) { return 0.5f * (float)std::sin(0.0026 * sample); }; // 20 Hz, half a semitone
    for (const int w : {1, 2, 3}) {
        const auto power = powerSpectrum(renderNote(w, kNote, 50.0f, vibrato));
        const double lowest = noteHz(kNote) * std::pow(2.0, -2.5 / 12.0);
        double below = 0.0, total = 0.0;
        for (size_t b = 1; b < power.size(); ++b) {
            total += power[b];
            if (binHz(b) > 20.0 && binHz(b) < lowest)
                below += power[b];
        }
        EXPECT_LT(10.0 * std::log10((below + 1e-30) / total), kBelowNoteBoundDb) << "waveform " << w;
    }
}

TEST(OscillatorAlias, TopBandHoldsTheIdealLevel) {
    struct Shape {
        int waveform, order, step;
    };
    for (const Shape shape : {Shape{2, 1, 1}, Shape{1, 1, 2}, Shape{3, 2, 2}}) {
        for (const int note : {48, 60, 69, 84, 96}) {
            const double db = topBandDb(renderNote(shape.waveform, note, 50.0f), noteHz(note), shape.order, shape.step);
            EXPECT_NEAR(db, 0.0, kTopBandToleranceDb) << "waveform " << shape.waveform << " note " << note;
        }
    }
}

// Pulse Width sweeping through its range moves an edge and never jumps: the largest step between samples stays what a
// held width gives.
TEST(OscillatorAlias, PulseWidthSweepAddsNoStep) {
    auto largestStep = [](bool sweep) {
        auto osc = std::make_unique<OscillatorModule>();
        setAliasParam(*osc, "waveform", 1.0f);
        setAliasParam(*osc, "randomPhase", 0.0f);
        osc->prepareToPlay(kAliasRate, kAliasBlock);
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kAliasBlock);
        constexpr int kBlocks = 200, kSettleBlocks = 10;
        float previous = 0.0f, largest = 0.0f;
        for (int b = 0; b < kBlocks; ++b) {
            buf.clear();
            setAliasParam(*osc, "pulseWidth", sweep ? 5.0f + 90.0f * (float)b / (float)(kBlocks - 1) : 50.0f);
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
            osc->processBlock(buf, midi);
            for (int i = 0; i < kAliasBlock; ++i) {
                if (b >= kSettleBlocks)
                    largest = std::max(largest, std::abs(buf.getSample(0, i) - previous));
                previous = buf.getSample(0, i);
            }
        }
        return largest;
    };
    EXPECT_LT(largestStep(true), largestStep(false) * 1.1f);
}
