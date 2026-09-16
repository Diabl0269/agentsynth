// Drive-vs-loudness measurements for RingModulatorModule (FRO120). `diodeRing` is linear above the
// diode breakpoint `vl`, so without normalisation a same-signal carrier/modulator patch came out at
// roughly `drive * input` — Drive was an uncompensated gain of up to 8x (+18 dB). The module now
// divides the wet signal by drive; these tests pin that Drive changes character, not loudness
// (pre-fix they measured +12 dB at drive 4.08 and +18 dB at drive 8).
#include "Modules/FX/RingModulatorModule.h"
#include "Modules/ModuleBase.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <juce_audio_basics/juce_audio_basics.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;
constexpr int kSkipSamples = 2048; // oversampler latency settle

juce::RangedAudioParameter* paramByID(juce::AudioProcessor& module, const juce::String& id) {
    return findParameterByID(&module, id);
}

void setFloat(juce::AudioProcessor& module, const juce::String& id, float value) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(paramByID(module, id));
    ASSERT_NE(p, nullptr) << "no float parameter named " << id;
    *p = value;
}

void setChoice(juce::AudioProcessor& module, const juce::String& id, int index) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(paramByID(module, id));
    ASSERT_NE(p, nullptr) << "no choice parameter named " << id;
    ASSERT_GT(p->choices.size(), 1);
    p->setValueNotifyingHost((float)index / (float)(p->choices.size() - 1));
}

// Renders `totalSamples` with the SAME waveform on Carrier (ch0) and Modulator (ch1) — the exact
// shape of the reported patch (filter output fanned into both ring-mod inputs).
juce::AudioBuffer<float> renderSameSignal(RingModulatorModule& module, bool square, float freqHz, float amplitude,
                                          int totalSamples) {
    juce::AudioBuffer<float> out(4, totalSamples);
    out.clear();
    juce::AudioBuffer<float> block(4, kBlockSize);
    juce::MidiBuffer midi;
    int written = 0;
    int global = 0;
    while (written < totalSamples) {
        const int n = std::min(kBlockSize, totalSamples - written);
        block.clear();
        for (int i = 0; i < n; ++i, ++global) {
            const float t = static_cast<float>(global) / static_cast<float>(kSampleRate);
            const float phase = juce::MathConstants<float>::twoPi * freqHz * t;
            float s = square ? (std::sin(phase) >= 0.0f ? amplitude : -amplitude) : amplitude * std::sin(phase);
            block.setSample(0, i, s);
            block.setSample(1, i, s);
        }
        module.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, written, block, ch, 0, n);
        written += n;
    }
    return out;
}

juce::AudioBuffer<float> renderDistinct(RingModulatorModule& module, float carrierHz, float modulatorHz,
                                        float amplitude, int totalSamples) {
    juce::AudioBuffer<float> out(4, totalSamples);
    out.clear();
    juce::AudioBuffer<float> block(4, kBlockSize);
    juce::MidiBuffer midi;
    int written = 0;
    int global = 0;
    while (written < totalSamples) {
        const int n = std::min(kBlockSize, totalSamples - written);
        block.clear();
        for (int i = 0; i < n; ++i, ++global) {
            const float t = static_cast<float>(global) / static_cast<float>(kSampleRate);
            block.setSample(0, i, amplitude * std::sin(juce::MathConstants<float>::twoPi * carrierHz * t));
            block.setSample(1, i, amplitude * std::sin(juce::MathConstants<float>::twoPi * modulatorHz * t));
        }
        module.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, written, block, ch, 0, n);
        written += n;
    }
    return out;
}

float magnitudeAt(const juce::AudioBuffer<float>& buffer, int channel, float hz, double sampleRate, int start = 0) {
    const float* data = buffer.getReadPointer(channel);
    const int n = buffer.getNumSamples() - start;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i) {
        const double w = 2.0 * juce::MathConstants<double>::pi * (double)hz * (double)i / sampleRate;
        re += data[start + i] * std::cos(w);
        im -= data[start + i] * std::sin(w);
    }
    return (float)(std::sqrt(re * re + im * im) * 2.0 / (double)n);
}

float peakFrom(const juce::AudioBuffer<float>& buffer, int channel, int start) {
    float peak = 0.0f;
    const float* data = buffer.getReadPointer(channel);
    for (int i = start; i < buffer.getNumSamples(); ++i)
        peak = std::max(peak, std::abs(data[i]));
    return peak;
}

float rmsFrom(const juce::AudioBuffer<float>& buffer, int channel, int start) {
    const float* data = buffer.getReadPointer(channel);
    const int n = buffer.getNumSamples() - start;
    double sum = 0.0;
    for (int i = start; i < buffer.getNumSamples(); ++i)
        sum += (double)data[i] * (double)data[i];
    return (float)std::sqrt(sum / (double)n);
}

float dbGain(float rms, float refRms) {
    if (refRms < 1.0e-9f)
        return 0.0f;
    return 20.0f * std::log10(rms / refRms);
}

} // namespace

class RingModulatorGainTest : public ::testing::Test {
protected:
    std::unique_ptr<RingModulatorModule> makeModule(float character = 0.5f, int oversamplingIndex = 1) {
        auto m = std::make_unique<RingModulatorModule>();
        setChoice(*m, "oversampling", oversamplingIndex);
        setFloat(*m, "mix", 1.0f);
        setFloat(*m, "character", character);
        m->prepareToPlay(kSampleRate, kBlockSize);
        return m;
    }
};

// Identical signal on both inputs (the reported patch's shape): Drive should change character, not
// loudness. Measures peak/RMS at drive in {1, 2, 4.08, 8} for a +/-1 square at ~55 Hz and a 0.8
// sine at 220 Hz, and prints a table. Asserts RMS at drive d stays within +3 dB of drive=1's RMS.
TEST_F(RingModulatorGainTest, SameSignalOnBothInputsDoesNotAmplifyWithDrive) {
    constexpr int kTotal = 16384;
    const float drives[] = {1.0f, 2.0f, 4.08f, 8.0f};

    for (bool square : {true, false}) {
        SCOPED_TRACE(square ? "square@55Hz amp=1.0" : "sine@220Hz amp=0.8");
        const float freq = square ? 55.0f : 220.0f;
        const float amp = square ? 1.0f : 0.8f;

        float refRms = -1.0f;
        std::cout << "\n[SameSignalOnBothInputsDoesNotAmplifyWithDrive] " << (square ? "square 55Hz" : "sine 220Hz")
                  << "\n  drive | peak    | rms     | gain(dB vs drive=1)\n";
        for (float drive : drives) {
            auto m = makeModule();
            setFloat(*m, "drive", drive);
            auto out = renderSameSignal(*m, square, freq, amp, kTotal);
            const float peak = peakFrom(out, 0, kSkipSamples);
            const float rms = rmsFrom(out, 0, kSkipSamples);
            if (refRms < 0.0f)
                refRms = rms;
            const float gainDb = dbGain(rms, refRms);
            std::cout << "  " << drive << " | " << peak << " | " << rms << " | " << gainDb << " dB\n";
            EXPECT_LT(gainDb, 3.0f) << "drive " << drive << " must not add more than 3 dB of loudness vs drive=1";
        }
    }
}

// Hot input (+/-4 on both channels, drive 4.08): output peak must stay bounded near the input's
// own level, not blow up with an uncompensated drive gain.
TEST_F(RingModulatorGainTest, HotInputStaysBounded) {
    constexpr int kTotal = 16384;
    constexpr float kInputPeak = 4.0f;

    auto m = makeModule();
    setFloat(*m, "drive", 4.08f);
    auto out = renderSameSignal(*m, /*square=*/true, 55.0f, kInputPeak, kTotal);
    const float peak = peakFrom(out, 0, kSkipSamples);

    std::cout << "\n[HotInputStaysBounded] input peak=" << kInputPeak << " drive=4.08 -> output peak=" << peak
              << " (ratio " << (peak / kInputPeak) << "x)\n";
    EXPECT_LE(peak, 1.5f * kInputPeak) << "output must not exceed 1.5x the input's own peak level";
}

// Classic ring-mod case: distinct carrier/modulator. Sideband energy at (carrierHz +/- modulatorHz)
// must not scale up with drive.
TEST_F(RingModulatorGainTest, DistinctCarrierAndModulatorLevelDoesNotScaleWithDrive) {
    constexpr float kCarrierHz = 440.0f;
    constexpr float kModulatorHz = 110.0f;
    constexpr float kSidebandHz = kCarrierHz + kModulatorHz; // 550 Hz
    constexpr int kTotal = 16384;
    const float drives[] = {1.0f, 4.0f, 8.0f};

    float refMag = -1.0f;
    std::cout << "\n[DistinctCarrierAndModulatorLevelDoesNotScaleWithDrive] carrier=440Hz modulator=110Hz "
                 "sideband=550Hz\n  drive | sideband mag | gain(dB vs drive=1)\n";
    for (float drive : drives) {
        auto m = makeModule();
        setFloat(*m, "drive", drive);
        auto out = renderDistinct(*m, kCarrierHz, kModulatorHz, 0.8f, kTotal);
        const float mag = magnitudeAt(out, 0, kSidebandHz, kSampleRate, kSkipSamples);
        if (refMag < 0.0f)
            refMag = mag;
        const float gainDb = dbGain(mag, refMag);
        std::cout << "  " << drive << " | " << mag << " | " << gainDb << " dB\n";
        EXPECT_LT(gainDb, 3.0f) << "drive " << drive << " must not add more than 3 dB of sideband energy vs drive=1";
    }
}

// Informational: drive below 1 (down to the 0.5 floor) after the fix's /drive normalisation — does
// the dead zone behave oddly as drive shrinks? Not asserted strictly, just recorded.
TEST_F(RingModulatorGainTest, LowDriveIsRecordedForReference) {
    constexpr int kTotal = 16384;
    auto m = makeModule();
    setFloat(*m, "drive", 0.5f);
    auto out = renderSameSignal(*m, /*square=*/true, 55.0f, 1.0f, kTotal);
    const float peak = peakFrom(out, 0, kSkipSamples);
    const float rms = rmsFrom(out, 0, kSkipSamples);
    std::cout << "\n[LowDriveIsRecordedForReference] drive=0.5 (square, amp=1.0) -> peak=" << peak << " rms=" << rms
              << "\n";
    SUCCEED();
}
