// FactoryShapes.cpp -- the shapes built into the app. The five Filter vowels are CustomFilterShapes' own preset
// curves (not copies of their tables); the other shapes are built here from a few numbers. Every Filter shape keeps
// its peaks at or below 0 dB, well under the +6 dB gain ceiling.
#include "Modules/Filter/CustomFilterShapes.h"
#include "Modules/Shapes/ShapeLibrary.h"
#include <cmath>
#include <utility>

namespace synth::shapes {

namespace {
using Preset = CustomFilterShapes::Preset;

LfoCustomWave waveOf(std::vector<LfoCustomWave::Point> points) {
    LfoCustomWave wave;
    wave.points = std::move(points);
    wave.sanitise();
    return wave;
}

/** A Filter curve through (Hz, dB) pairs, joined by straight lines on the log-frequency axis. */
LfoCustomWave responseThrough(const std::vector<std::pair<float, float>>& hzDb) {
    std::vector<LfoCustomWave::Point> points;
    for (const auto& [hz, db] : hzDb)
        points.push_back({CustomFilterShapes::hzToX(hz), CustomFilterShapes::dbToY(db), 0.0f});
    return waveOf(std::move(points));
}

/** Passes 300 Hz to 3.4 kHz (-3 dB at both ends) and falls away on either side, like a phone line. */
LfoCustomWave telephoneBand() {
    return responseThrough({{20.0f, -48.0f},
                            {100.0f, -36.0f},
                            {200.0f, -14.0f},
                            {300.0f, -3.0f},
                            {450.0f, 0.0f},
                            {2000.0f, 0.0f},
                            {3400.0f, -3.0f},
                            {5000.0f, -14.0f},
                            {8000.0f, -30.0f},
                            {20000.0f, -48.0f}});
}

/** A notch every 1 kHz from 1 kHz to 19 kHz, 24 dB deep, with the response back at 0 dB halfway between. */
LfoCustomWave comb() {
    std::vector<std::pair<float, float>> hzDb{{20.0f, 0.0f}};
    for (int k = 1; k < 20; ++k) {
        hzDb.push_back({1000.0f * ((float)k - 0.5f), 0.0f});
        hzDb.push_back({1000.0f * (float)k, -24.0f});
    }
    hzDb.push_back({19500.0f, 0.0f});
    hzDb.push_back({20000.0f, 0.0f});
    return responseThrough(hzDb);
}

/** A straight slope from 0 dB at the bass to -12 dB at the treble. */
LfoCustomWave tilt() { return responseThrough({{20.0f, 0.0f}, {20000.0f, -12.0f}}); }

LibraryShape filterShape(const juce::String& name, LfoCustomWave curve) {
    return {ShapeKind::Filter, name, std::move(curve), true};
}

LibraryShape lfoShape(const juce::String& name, LfoCustomWave curve) {
    return {ShapeKind::Lfo, name, std::move(curve), true};
}

/** `steps` samples of `f` over x = from..to (both ends included). */
void sampleInto(std::vector<LfoCustomWave::Point>& out, float from, float to, int steps, float (*f)(float t)) {
    for (int i = 0; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        out.push_back({from + (to - from) * t, f(t), 0.0f});
    }
}

/** Falls from 1 to 0 along an exponential. */
LfoCustomWave exponentialDecay() {
    std::vector<LfoCustomWave::Point> points;
    sampleInto(points, 0.0f, 1.0f, 16, [](float t) {
        constexpr float k = 5.0f;
        return (std::exp(-k * t) - std::exp(-k)) / (1.0f - std::exp(-k));
    });
    return waveOf(std::move(points));
}

/** Drops to 0 at the start of the cycle and swells back to 1 by 70% of it, then holds. */
LfoCustomWave sidechainDuck() {
    std::vector<LfoCustomWave::Point> points;
    sampleInto(points, 0.0f, 0.7f, 12, [](float t) {
        constexpr float k = 4.0f;
        return (1.0f - std::exp(-k * t)) / (1.0f - std::exp(-k));
    });
    points.push_back({1.0f, 1.0f, 0.0f});
    return waveOf(std::move(points));
}

/** Three arcs, each lower and shorter than the last, then rest. */
LfoCustomWave bounce() {
    struct Arc {
        float from, to, height;
    };
    std::vector<LfoCustomWave::Point> points;
    for (const Arc& arc : {Arc{0.0f, 0.5f, 1.0f}, Arc{0.5f, 0.8f, 0.5f}, Arc{0.8f, 0.95f, 0.2f}}) {
        constexpr int kSteps = 6;
        for (int i = 0; i < kSteps; ++i) {
            const float t = (float)i / (float)kSteps;
            points.push_back({arc.from + (arc.to - arc.from) * t, arc.height * 4.0f * t * (1.0f - t), 0.0f});
        }
    }
    points.push_back({0.95f, 0.0f, 0.0f});
    points.push_back({1.0f, 0.0f, 0.0f});
    return waveOf(std::move(points));
}

/** A slow wander built from three whole-number harmonics, so the end meets the start. */
LfoCustomWave smoothRandom() {
    std::vector<LfoCustomWave::Point> points;
    constexpr int kSteps = 24;
    const float twoPi = juce::MathConstants<float>::twoPi;
    for (int i = 0; i <= kSteps; ++i) {
        const float x = (float)i / (float)kSteps;
        const float wave = 0.28f * std::sin(twoPi * 2.0f * x + 0.7f) + 0.18f * std::sin(twoPi * 5.0f * x + 2.1f) +
                           0.10f * std::sin(twoPi * 7.0f * x + 4.0f);
        points.push_back({x, juce::jlimit(0.0f, 1.0f, 0.5f + wave), 0.0f});
    }
    points.back().y = points.front().y;
    return waveOf(std::move(points));
}

/** Sixteen pulses across the cycle, each on for the first half of its sixteenth. Uses all 64 points. */
LfoCustomWave sixteenthGate() {
    constexpr int kCells = 16;
    std::vector<LfoCustomWave::Point> points;
    for (int i = 0; i < kCells; ++i) {
        const float start = (float)i / (float)kCells;
        const float end = (float)(i + 1) / (float)kCells;
        points.push_back({start, 1.0f, 0.0f});
        points.push_back({(start + end) * 0.5f, 1.0f, 0.0f});
        points.push_back({(start + end) * 0.5f, 0.0f, 0.0f});
        points.push_back({end, 0.0f, 0.0f});
    }
    return waveOf(std::move(points));
}
} // namespace

std::vector<LibraryShape> ShapeLibrary::factoryShapes(ShapeKind kind) {
    if (kind == ShapeKind::Filter) {
        std::vector<LibraryShape> out;
        for (const auto preset : {Preset::Ah, Preset::Ee, Preset::Oo, Preset::Eh, Preset::Ih})
            out.push_back(filterShape(CustomFilterShapes::presetName(preset), CustomFilterShapes::presetCurve(preset)));
        out.push_back(filterShape("telephone band", telephoneBand()));
        out.push_back(filterShape("comb", comb()));
        out.push_back(filterShape("tilt", tilt()));
        return out;
    }
    return {lfoShape("ramp up", LfoCustomWave::preset(LfoCustomWave::Preset::RampUp)),
            lfoShape("ramp down", LfoCustomWave::preset(LfoCustomWave::Preset::RampDown)),
            lfoShape("exponential decay", exponentialDecay()),
            lfoShape("sidechain duck", sidechainDuck()),
            lfoShape("bounce", bounce()),
            lfoShape("4-step staircase", LfoCustomWave::preset(LfoCustomWave::Preset::Steps4)),
            lfoShape("smooth random", smoothRandom()),
            lfoShape("1/16 gate", sixteenthGate())};
}

} // namespace synth::shapes
