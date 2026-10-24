// Offline measurement of an Oscillator unison stack, for the tests that hold the Blend and Width maps to
// reference numbers: loudness against one voice, side/mid level ratio, and the share of the mid power
// that sits on the centre voice's own partials.
#pragma once

#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace unison_measure {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kFftOrder = 18;
constexpr int kFftSize = 1 << kFftOrder;
constexpr float kDetuneCents = 34.0f; ///< the reference stack's outer voices sit about 34 cents from the centre

struct Stats {
    double rms = 0.0;         ///< stereo RMS, mean of both legs
    double sideMid = 0.0;     ///< RMS of L-R over RMS of L+R
    double centreShare = 0.0; ///< share of the mid power on the centre voice's partials (cents-wide window)
};

inline void setActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

/** Renders `kFftSize` samples of a held 220 Hz Saw chord into `left` / `right` after a settling stretch. */
inline void renderHeld(OscillatorModule& m, std::vector<float>& left, std::vector<float>& right) {
    m.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
    const int warm = 40;
    const int blocks = warm + kFftSize / kBlock;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
        m.processBlock(buf, midi);
        if (b < warm)
            continue;
        left.insert(left.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
        right.insert(right.end(), buf.getReadPointer(OscillatorModule::kRightBase),
                     buf.getReadPointer(OscillatorModule::kRightBase) + kBlock);
    }
}

/** Mono-leg stack: the Saw at 220 Hz, `unison` voices, Rand at 100 %, the reference detune. */
inline Stats measureStack(int unison, float blend, float width) {
    OscillatorModule m;
    setActual(m, "level", 1.0f);
    setActual(m, "waveform", 2.0f);
    setActual(m, "unison", (float)unison);
    setActual(m, "detune", kDetuneCents);
    setActual(m, "blend", blend);
    setActual(m, "width", width);
    setActual(m, "randomPhase", 100.0f);
    std::vector<float> l, r;
    renderHeld(m, l, r);

    Stats s;
    double sumL = 0, sumR = 0, sumSide = 0, sumMid = 0;
    std::vector<float> mid(kFftSize);
    for (int i = 0; i < kFftSize; ++i) {
        sumL += (double)l[(size_t)i] * l[(size_t)i];
        sumR += (double)r[(size_t)i] * r[(size_t)i];
        const double d = (double)l[(size_t)i] - r[(size_t)i], p = (double)l[(size_t)i] + r[(size_t)i];
        sumSide += d * d;
        sumMid += p * p;
        mid[(size_t)i] = 0.5f * (l[(size_t)i] + r[(size_t)i]);
    }
    s.rms = std::sqrt((sumL + sumR) / (2.0 * kFftSize));
    s.sideMid = std::sqrt(sumSide / std::max(sumMid, 1e-30));

    std::vector<float> fft(2 * (size_t)kFftSize, 0.0f);
    for (int i = 0; i < kFftSize; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / (kFftSize - 1));
        fft[(size_t)i] = (float)(mid[(size_t)i] * w);
    }
    juce::dsp::FFT(kFftOrder).performRealOnlyForwardTransform(fft.data());
    double centre = 0.0, total = 0.0;
    for (int bin = 1; bin < kFftSize / 2; ++bin) {
        const double hz = bin * kRate / kFftSize;
        const double power = (double)fft[(size_t)(2 * bin)] * fft[(size_t)(2 * bin)] +
                             (double)fft[(size_t)(2 * bin + 1)] * fft[(size_t)(2 * bin + 1)];
        for (int k = 2; k <= 8; ++k) {
            const double cents = std::abs(1200.0 * std::log2(hz / (220.0 * k)));
            if (cents < 4.0)
                centre += power;
            if (cents < 80.0)
                total += power;
        }
    }
    s.centreShare = total > 0.0 ? centre / total : 0.0;
    return s;
}

} // namespace unison_measure
