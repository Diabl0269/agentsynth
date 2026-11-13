// FactoryShapes.cpp -- the shapes built into the app, with the starter set (plain shapes, then experimental ones). The
// five Filter vowels are CustomFilterShapes' own preset curves (not copies of their tables); the other shapes are built
// here from a few numbers. Every Filter shape keeps its peaks at or below 0 dB, well under the +6 dB gain ceiling.
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
// ---- The starter set: a few plain shapes to start from, then a few strange ones to play with. ----

/** Open to 500 Hz, then falls away to the top of the range: a gentle low-pass. */
LfoCustomWave lowPassRamp() { return responseThrough({{20.0f, 0.0f}, {500.0f, 0.0f}, {20000.0f, -48.0f}}); }

/** One band around 1 kHz with the rest pulled down. */
LfoCustomWave band() {
    return responseThrough(
        {{20.0f, -48.0f}, {400.0f, -30.0f}, {800.0f, 0.0f}, {1250.0f, 0.0f}, {2500.0f, -30.0f}, {20000.0f, -48.0f}});
}

/** Flat except for one deep notch at 1 kHz. */
LfoCustomWave notch() {
    return responseThrough({{20.0f, 0.0f}, {700.0f, 0.0f}, {1000.0f, -36.0f}, {1400.0f, 0.0f}, {20000.0f, 0.0f}});
}

/** Two close peaks, like a vowel with two formants. */
LfoCustomWave twoFormants() {
    return responseThrough({{20.0f, -36.0f},
                            {250.0f, -18.0f},
                            {400.0f, 0.0f},
                            {600.0f, -14.0f},
                            {1400.0f, -10.0f},
                            {2000.0f, 0.0f},
                            {2600.0f, -14.0f},
                            {6000.0f, -36.0f},
                            {20000.0f, -48.0f}});
}

/** A notch every octave from 150 Hz up, 24 dB deep. */
LfoCustomWave wideComb() {
    std::vector<std::pair<float, float>> hzDb{{20.0f, 0.0f}, {100.0f, 0.0f}};
    float hz = 150.0f;
    while (hz < 12000.0f) {
        hzDb.push_back({hz, -24.0f});
        hzDb.push_back({hz * 1.45f, 0.0f});
        hz *= 2.0f;
    }
    hzDb.push_back({20000.0f, 0.0f});
    return responseThrough(hzDb);
}

/** Falls in three flat steps, each 12 dB lower, with a sheer edge between them. */
LfoCustomWave stairs() {
    return responseThrough({{20.0f, 0.0f},
                            {250.0f, 0.0f},
                            {250.0f, -12.0f},
                            {800.0f, -12.0f},
                            {800.0f, -24.0f},
                            {2500.0f, -24.0f},
                            {2500.0f, -36.0f},
                            {20000.0f, -36.0f}});
}

/** Two narrow peaks with a gap between them. */
LfoCustomWave doublePeak() {
    return responseThrough({{20.0f, -40.0f},
                            {300.0f, -40.0f},
                            {500.0f, 0.0f},
                            {800.0f, -30.0f},
                            {1500.0f, -30.0f},
                            {2500.0f, 0.0f},
                            {4000.0f, -40.0f},
                            {20000.0f, -48.0f}});
}

/** Up and down in wide zigzags across the range. */
LfoCustomWave seesaw() {
    return responseThrough({{20.0f, -24.0f},
                            {200.0f, 0.0f},
                            {500.0f, -24.0f},
                            {1200.0f, 0.0f},
                            {3000.0f, -24.0f},
                            {7000.0f, 0.0f},
                            {20000.0f, -24.0f}});
}

/** Two bumps of different height in one cycle. */
LfoCustomWave doubleBump() {
    std::vector<LfoCustomWave::Point> points;
    for (int i = 0; i <= 24; ++i) {
        const float x = (float)i / 24.0f;
        const float first = std::exp(-std::pow((x - 0.25f) / 0.09f, 2.0f));
        const float second = 0.55f * std::exp(-std::pow((x - 0.72f) / 0.09f, 2.0f));
        points.push_back({x, juce::jlimit(0.0f, 1.0f, first + second), 0.0f});
    }
    return waveOf(std::move(points));
}

/** A heartbeat: a strong beat, a softer one right after it, then rest. */
LfoCustomWave heartbeat() {
    return waveOf({{0.0f, 0.0f, 0.0f},
                   {0.06f, 1.0f, 0.0f},
                   {0.16f, 0.0f, 0.0f},
                   {0.26f, 0.6f, 0.0f},
                   {0.36f, 0.0f, 0.0f},
                   {1.0f, 0.0f, 0.0f}});
}

/** Four short ramps in a row, each starting where the last one began. */
LfoCustomWave stutterRamp() {
    std::vector<LfoCustomWave::Point> points;
    for (int i = 0; i < 4; ++i) {
        const float start = (float)i * 0.25f;
        points.push_back({start, 0.0f, 0.0f});
        points.push_back({start + 0.2f, 1.0f, 0.0f});
    }
    points.push_back({1.0f, 0.0f, 0.0f});
    return waveOf(std::move(points));
}

/** A staircase that climbs in uneven steps and then falls back in one drop. */
LfoCustomWave wobbleSteps() {
    return waveOf({{0.0f, 0.1f, 0.0f},
                   {0.18f, 0.1f, 0.0f},
                   {0.18f, 0.45f, 0.0f},
                   {0.3f, 0.45f, 0.0f},
                   {0.3f, 0.3f, 0.0f},
                   {0.5f, 0.3f, 0.0f},
                   {0.5f, 0.8f, 0.0f},
                   {0.72f, 0.8f, 0.0f},
                   {0.72f, 0.6f, 0.0f},
                   {0.88f, 0.6f, 0.0f},
                   {0.88f, 1.0f, 0.0f},
                   {0.97f, 1.0f, 0.0f},
                   {1.0f, 0.0f, 0.0f}});
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
        // The starter set: plain shapes first, then the experimental ones.
        out.push_back(filterShape("low-pass ramp", lowPassRamp()));
        out.push_back(filterShape("band", band()));
        out.push_back(filterShape("notch", notch()));
        out.push_back(filterShape("two formants", twoFormants()));
        out.push_back(filterShape("wide comb", wideComb()));
        out.push_back(filterShape("stairs", stairs()));
        out.push_back(filterShape("double peak", doublePeak()));
        out.push_back(filterShape("seesaw", seesaw()));
        return out;
    }
    return {lfoShape("ramp up", LfoCustomWave::preset(LfoCustomWave::Preset::RampUp)),
            lfoShape("ramp down", LfoCustomWave::preset(LfoCustomWave::Preset::RampDown)),
            lfoShape("exponential decay", exponentialDecay()),
            lfoShape("sidechain duck", sidechainDuck()),
            lfoShape("bounce", bounce()),
            lfoShape("4-step staircase", LfoCustomWave::preset(LfoCustomWave::Preset::Steps4)),
            lfoShape("smooth random", smoothRandom()),
            lfoShape("1/16 gate", sixteenthGate()),
            lfoShape("triangle", LfoCustomWave::preset(LfoCustomWave::Preset::Triangle)),
            lfoShape("soft sine", LfoCustomWave::preset(LfoCustomWave::Preset::SoftSine)),
            lfoShape("square", LfoCustomWave::preset(LfoCustomWave::Preset::Square)),
            lfoShape("pulse", LfoCustomWave::preset(LfoCustomWave::Preset::Pulse)),
            lfoShape("double bump", doubleBump()),
            lfoShape("heartbeat", heartbeat()),
            lfoShape("stutter ramp", stutterRamp()),
            lfoShape("wobble steps", wobbleSteps())};
}

} // namespace synth::shapes
