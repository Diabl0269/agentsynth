#pragma once

// Shared helpers for the Pluck and Mallet tests: render a module with note events at exact sample
// positions, and measure level and pitch of what comes out.

#include "Modules/ModuleBase.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace instrumenttest {

constexpr int kBlock = 512;

struct Event {
    int sample;
    int note;
    float velocity; // 0 turns the event into a note-off
};

inline Event on(int sample, int note, float velocity = 100.0f / 127.0f) { return {sample, note, velocity}; }
inline Event off(int sample, int note) { return {sample, note, 0.0f}; }

/** Renders `seconds` of left-channel audio with the events delivered at their sample positions. */
template <typename Module>
std::vector<float> render(Module& module, const std::vector<Event>& events, double seconds, double rate) {
    const int total = static_cast<int>(seconds * rate);
    std::vector<float> out(static_cast<std::size_t>(total), 0.0f);
    for (int start = 0; start < total; start += kBlock) {
        const int n = std::min(kBlock, total - start);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::MidiBuffer midi;
        for (const auto& e : events)
            if (e.sample >= start && e.sample < start + n)
                midi.addEvent(e.velocity > 0.0f ? juce::MidiMessage::noteOn(1, e.note, e.velocity)
                                                : juce::MidiMessage::noteOff(1, e.note),
                              e.sample - start);
        module.processBlock(block, midi);
        std::copy(block.getReadPointer(0), block.getReadPointer(0) + n, out.begin() + start);
    }
    return out;
}

inline float peakOf(const std::vector<float>& s, double fromSec, double toSec, double rate) {
    const auto a = static_cast<std::size_t>(std::max(0.0, fromSec) * rate);
    const auto b = std::min(s.size(), static_cast<std::size_t>(toSec * rate));
    float peak = 0.0f;
    for (std::size_t i = a; i < b; ++i)
        peak = std::max(peak, std::fabs(s[i]));
    return peak;
}

inline float rmsOf(const std::vector<float>& s, double fromSec, double toSec, double rate) {
    const auto a = static_cast<std::size_t>(fromSec * rate);
    const auto b = std::min(s.size(), static_cast<std::size_t>(toSec * rate));
    double sum = 0.0;
    for (std::size_t i = a; i < b; ++i)
        sum += static_cast<double>(s[i]) * s[i];
    return b > a ? static_cast<float>(std::sqrt(sum / static_cast<double>(b - a))) : 0.0f;
}

/** Hann-windowed DFT magnitude of the span at `hz`. */
inline double magnitudeAt(const std::vector<float>& s, double fromSec, double toSec, double hz, double rate) {
    const auto a = static_cast<std::size_t>(fromSec * rate);
    const auto b = std::min(s.size(), static_cast<std::size_t>(toSec * rate));
    const auto n = static_cast<double>(b - a);
    const std::complex<double> step = std::polar(1.0, -2.0 * M_PI * hz / rate);
    std::complex<double> phasor = 1.0, sum = 0.0;
    for (std::size_t i = a; i < b; ++i) {
        const double window = 0.5 * (1.0 - std::cos(2.0 * M_PI * static_cast<double>(i - a) / n));
        sum += s[i] * window * phasor;
        phasor *= step;
    }
    return std::abs(sum);
}

/** Pitch of the strongest component within +-80 cents of `expectedHz`, as cents away from it. */
inline double centsOffFrom(const std::vector<float>& s, double fromSec, double toSec, double expectedHz, double rate) {
    constexpr double kSpan = 80.0, kStep = 0.5;
    const int steps = static_cast<int>(2.0 * kSpan / kStep) + 1;
    std::vector<double> mags(static_cast<std::size_t>(steps));
    int best = 0;
    for (int i = 0; i < steps; ++i) {
        const double cents = -kSpan + kStep * i;
        mags[static_cast<std::size_t>(i)] =
            magnitudeAt(s, fromSec, toSec, expectedHz * std::exp2(cents / 1200.0), rate);
        if (mags[static_cast<std::size_t>(i)] > mags[static_cast<std::size_t>(best)])
            best = i;
    }
    double offset = 0.0;
    if (best > 0 && best < steps - 1) { // parabolic refinement
        const double l = mags[static_cast<std::size_t>(best - 1)], c = mags[static_cast<std::size_t>(best)],
                     r = mags[static_cast<std::size_t>(best + 1)];
        const double denom = l - 2.0 * c + r;
        offset = denom != 0.0 ? 0.5 * (l - r) / denom : 0.0;
    }
    return -kSpan + kStep * (best + offset);
}

/** Sets a parameter to a real (not 0..1) value. */
inline void setParam(ModuleBase& module, const char* id, float value) {
    auto* p = findParameterByID(&module, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

inline double noteHz(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }

} // namespace instrumenttest
