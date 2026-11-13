#pragma once

#include <array>
#include <cmath>

// Band-limited Saw, Square and Triangle as pure functions of phase and phase increment.
namespace synth::bandlimited {

inline constexpr int kHalfWidthSamples = 8;
inline constexpr int kStepsPerSample = 256;
inline constexpr int kTableSize = kHalfWidthSamples * kStepsPerSample + 2;

/** Over t = 0..kHalfWidthSamples: the band-limited step minus the ideal step, and likewise the ramp. */
struct ResidualTables {
    std::array<float, kTableSize> step{};
    std::array<float, kTableSize> ramp{};
};
/** Filled before main and never written again, so the audio thread only reads it. */
extern const ResidualTables residualTables;

/** `t` is the distance from the edge in samples, |t| at most kHalfWidthSamples. */
inline float stepResidual(float t) {
    const float a = std::abs(t) * (float)kStepsPerSample;
    const int i = (int)a;
    const auto& table = residualTables.step;
    const float v = table[(size_t)i] + (table[(size_t)i + 1] - table[(size_t)i]) * (a - (float)i);
    return t < 0.0f ? -v : v;
}

inline float rampResidual(float t) {
    const float a = std::abs(t) * (float)kStepsPerSample;
    const int i = (int)a;
    const auto& table = residualTables.ramp;
    return table[(size_t)i] + (table[(size_t)i + 1] - table[(size_t)i]) * (a - (float)i);
}

/** Sums `residual` over every edge within reach; `sincePhase` is the phase since one of the edges, 0..1. */
template <float (*residual)(float)>
inline float sumEdges(float sincePhase, float dt) {
    const float reach = (float)kHalfWidthSamples * dt;
    if (reach < 0.5f) {
        const float x = sincePhase < 0.5f ? sincePhase : sincePhase - 1.0f;
        return std::abs(x) < reach ? residual(x / dt) : 0.0f;
    }
    float sum = 0.0f;
    const float last = std::floor(sincePhase + reach);
    for (float k = std::ceil(sincePhase - reach); k <= last; k += 1.0f) {
        const float x = sincePhase - k;
        if (std::abs(x) < reach)
            sum += residual(x / dt);
    }
    return sum;
}

inline float saw(float phase, float dt) { return 2.0f * phase - 1.0f - 2.0f * sumEdges<stepResidual>(phase, dt); }

/** `pulseWidth` is the duty cycle, 0..1. */
inline float square(float phase, float dt, float pulseWidth) {
    const float sinceFall = phase < pulseWidth ? phase - pulseWidth + 1.0f : phase - pulseWidth;
    return (phase < pulseWidth ? 1.0f : -1.0f) +
           2.0f * (sumEdges<stepResidual>(phase, dt) - sumEdges<stepResidual>(sinceFall, dt));
}

inline float triangle(float phase, float dt) {
    const float sinceTop = phase < 0.5f ? phase + 0.5f : phase - 0.5f;
    return 4.0f * std::abs(phase - 0.5f) - 1.0f +
           8.0f * dt * (sumEdges<rampResidual>(sinceTop, dt) - sumEdges<rampResidual>(phase, dt));
}

} // namespace synth::bandlimited
