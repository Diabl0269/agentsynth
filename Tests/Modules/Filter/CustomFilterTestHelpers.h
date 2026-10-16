// CustomFilterTestHelpers.h -- shared helpers for the Filter Custom-type tests: shape builders,
// impulse rendering through CustomFilterEngine, and magnitude measurement by direct DFT.
#pragma once

#include "Modules/Filter/CustomFilterEngine.h"
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

namespace synth::test {

constexpr double kTestSampleRate = 44100.0;

/** A shape whose curve passes through (Hz, dB) points with straight segments over log frequency. */
inline CustomFilterShape makeShape(const juce::String& name, std::initializer_list<std::pair<float, float>> hzDb,
                                   std::vector<float> position) {
    CustomFilterShape s;
    s.name = name;
    for (const auto& [hz, db] : hzDb)
        s.curve.points.push_back({CustomFilterShapes::hzToX(hz), CustomFilterShapes::dbToY(db), 0.0f});
    s.curve.sanitise();
    s.position = std::move(position);
    return s;
}

/** The drawn curve's dB at `hz` (what the filter should produce at depth 1, shift 0). */
inline float drawnDb(const CustomFilterShape& s, float hz) {
    return CustomFilterShapes::yToDb(s.curve.evaluate(CustomFilterShapes::hzToX(hz)));
}

/** Feeds a unit impulse through channel 0 of a freshly prepared engine and returns `length`
    output samples, processed in blocks of `block`. */
inline std::vector<float> renderImpulse(CustomFilterEngine& engine, const CustomFilterEngine::Controls& c,
                                        int length = 8192, int block = 512) {
    engine.prepare(kTestSampleRate);
    std::vector<float> out((size_t)length, 0.0f);
    out[0] = 1.0f;
    for (int start = 0; start < length; start += block) {
        float* ch[1] = {out.data() + start};
        engine.process(ch, 1, std::min(block, length - start), c);
    }
    return out;
}

/** |DFT| of `ir` at `hz`, in dB. */
inline float magnitudeDb(const std::vector<float>& ir, float hz, double sr = kTestSampleRate) {
    double re = 0.0, im = 0.0;
    const double w = 2.0 * juce::MathConstants<double>::pi * (double)hz / sr;
    for (size_t n = 0; n < ir.size(); ++n) {
        re += (double)ir[n] * std::cos(w * (double)n);
        im -= (double)ir[n] * std::sin(w * (double)n);
    }
    return (float)(20.0 * std::log10(std::max(1.0e-12, std::sqrt(re * re + im * im))));
}

/** `count` log-spaced frequencies from `lo` to `hi` Hz inclusive. */
inline std::vector<float> logFrequencies(float lo, float hi, int count) {
    std::vector<float> f;
    for (int i = 0; i < count; ++i)
        f.push_back(lo * std::pow(hi / lo, (float)i / (float)(count - 1)));
    return f;
}

} // namespace synth::test
