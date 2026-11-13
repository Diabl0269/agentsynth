// ModCurve.h -- the response curve an AttenuverterModule applies to its signal before the amount:
// a LfoCustomWave breakpoint shape plus an output range, with a 256-step lookup table the audio
// thread reads. Pure data, no locks, no allocation outside the callers' own Table.
#pragma once

#include "Modules/Lfo/LfoCustomWave.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

namespace synth {

/** A modulation response curve: input magnitude in [0, 1] maps to rangeMin + (rangeMax - rangeMin) * shape(input). */
struct ModCurve {
    static constexpr int kTableSize = 256;
    /** Index kTableSize holds the curve's value at input 1 (the last point's y). */
    using Table = std::array<float, kTableSize + 1>;

    enum class Preset { Linear, Exp, Log, SCurve };

    static LfoCustomWave straightShape();

    /** Default-constructs to the linear curve. */
    LfoCustomWave shape = straightShape();
    float rangeMin = 0.0f;
    float rangeMax = 1.0f;

    static ModCurve linear();
    static ModCurve preset(Preset);

    bool operator==(const ModCurve&) const;
    /** True only for the straight (0,0)-(1,1) shape over the full range. */
    bool isLinear() const;

    juce::var toVar() const;
    /** Never fails: anything unusable resolves to `linear()`. */
    static ModCurve fromVar(const juce::var&);

    /** No allocation. */
    void renderTable(Table& out) const;

    /** Audio thread safe. Mirrored around zero (zero counts as positive); |input| above 1 clamps; NaN gives 0. */
    static float lookup(const Table& table, float input) noexcept {
        if (std::isnan(input))
            return 0.0f;
        const float magnitude = std::min(std::abs(input), 1.0f);
        const float position = magnitude * (float)kTableSize;
        const int index = std::min((int)position, kTableSize - 1);
        const float frac = position - (float)index;
        const float lo = table[(size_t)index];
        const float value = lo + (table[(size_t)index + 1] - lo) * frac;
        return input < 0.0f ? -value : value;
    }
};

} // namespace synth
