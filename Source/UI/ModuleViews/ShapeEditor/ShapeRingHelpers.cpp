// ShapeRingHelpers.cpp -- the peaks each Ring helper places and the curve that draws them.
#include "ShapeRingHelpers.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include <algorithm>
#include <cmath>

namespace synth::ui::ringhelpers {

namespace {
using synth::CustomFilterShapes;

constexpr int kChordFirstC = 48; // C3
constexpr int kChordOctaves = 2;
constexpr double kBellTopHz = 10000.0;
// Two neighbouring peaks may use up to this share of the gap between them each, so their feet never cross.
constexpr double kMaxGapShare = 0.45;

double noteHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

bool inRange(double hz) { return hz >= CustomFilterShapes::kMinHz && hz <= CustomFilterShapes::kMaxHz; }

// One octave along the curve's x (log frequency from kMinHz to kMaxHz).
double octaveInX() { return std::log(2.0) / std::log((double)CustomFilterShapes::kMaxHz / CustomFilterShapes::kMinHz); }
} // namespace

std::vector<Peak> stackHarmonics(double rootHz, int count, float topDb, float stepDb) {
    std::vector<Peak> peaks;
    for (int n = 1; n <= count && rootHz > 0.0; ++n)
        if (inRange(rootHz * n))
            peaks.push_back({rootHz * n, topDb - stepDb * (float)(n - 1)});
    return peaks;
}

LfoCustomWave stackCurve(const StackOptions& options) {
    return curveWithPeaks(stackHarmonics(options.rootHz, options.count, kTopDb, options.stepDb),
                          options.halfWidthOctaves);
}

std::vector<Peak> chordPeaks(const std::optional<MusicalScale>& key) {
    const int root = key.has_value() ? key->rootPitchClass : 9;
    const auto has = [&](int interval) {
        return key.has_value() && key->mask != 0 && (key->mask & (1u << (interval % 12))) != 0;
    };
    const int third = has(3) && !has(4) ? 3 : 4;
    std::vector<Peak> peaks;
    for (int octave = 0; octave < kChordOctaves; ++octave)
        for (const int interval : {0, third, 7})
            peaks.push_back({noteHz(kChordFirstC + 12 * octave + root + interval), kTopDb});
    return peaks;
}

std::vector<Peak> octaveBells(double rootHz) {
    std::vector<Peak> peaks;
    for (double hz = rootHz; rootHz > 0.0 && hz <= kBellTopHz; hz *= 2.0)
        if (inRange(hz))
            peaks.push_back({hz, kTopDb});
    return peaks;
}

LfoCustomWave curveWithPeaks(std::vector<Peak> peaks, double halfWidthOctaves) {
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.hz < b.hz; });
    const auto maxPeaks = (size_t)((LfoCustomWave::kMaxPoints - 2) / 3);
    if (peaks.size() > maxPeaks)
        peaks.resize(maxPeaks);
    const float floorY = CustomFilterShapes::dbToY(kFloorDb);
    std::vector<double> xs;
    for (const auto& peak : peaks)
        xs.push_back((double)CustomFilterShapes::hzToX((float)peak.hz));

    LfoCustomWave wave;
    wave.points.push_back({0.0f, floorY, 0.0f});
    for (size_t i = 0; i < peaks.size(); ++i) {
        double half = halfWidthOctaves * octaveInX();
        if (i > 0)
            half = std::min(half, (xs[i] - xs[i - 1]) * kMaxGapShare);
        if (i + 1 < peaks.size())
            half = std::min(half, (xs[i + 1] - xs[i]) * kMaxGapShare);
        const float top = CustomFilterShapes::dbToY(juce::jlimit(kFloorDb, CustomFilterShapes::kMaxDb, peaks[i].db));
        const double left = xs[i] - half;
        const double right = xs[i] + half;
        if (left > wave.points.back().x)
            wave.points.push_back({(float)left, floorY, 0.0f});
        wave.points.push_back({(float)xs[i], top, 0.0f});
        if (right < 1.0)
            wave.points.push_back({(float)right, floorY, 0.0f});
    }
    if (wave.points.back().x < 1.0f)
        wave.points.push_back({1.0f, floorY, 0.0f});
    wave.sanitise();
    return wave;
}

juce::String helperName(Helper helper) {
    switch (helper) {
    case Helper::tunePeak:
        return "Tune selected peak to a note";
    case Helper::stackHarmonics:
        return "Stack harmonics";
    case Helper::chordPeaks:
        return "Chord peaks from the key";
    case Helper::octaveBells:
        return "Octave bells";
    }
    return {};
}

juce::String helperTooltip(Helper helper) {
    switch (helper) {
    case Helper::tunePeak:
        return "Moves the selected point (or the highest one) across to the nearest note, a note of the key when there "
               "is one";
    case Helper::stackHarmonics:
        return "Eight narrow peaks on the harmonics of the root, each a little quieter than the one before";
    case Helper::chordPeaks:
        return "Peaks on the root, third and fifth of the key, in two octaves";
    case Helper::octaveBells:
        return "Thin, tall peaks on the root and every octave above it";
    }
    return {};
}

std::optional<LfoCustomWave> tunePeak(const LfoCustomWave& curve, int index, const std::optional<MusicalScale>& key) {
    const int count = (int)curve.points.size();
    if (count < 3)
        return std::nullopt;
    if (index <= 0 || index >= count - 1) {
        index = 1;
        for (int i = 2; i < count - 1; ++i)
            if (curve.points[(size_t)i].y > curve.points[(size_t)index].y)
                index = i;
    }
    const double hz = CustomFilterShapes::xToHz(curve.points[(size_t)index].x);
    const int nearest = (int)std::lround(69.0 + 12.0 * std::log2(hz / 440.0));
    const int best = key.has_value() ? key->snapPitch(nearest) : nearest;
    auto tuned = curve;
    const float lo = curve.points[(size_t)index - 1].x + 1.0e-4f;
    const float hi = curve.points[(size_t)index + 1].x - 1.0e-4f;
    tuned.points[(size_t)index].x =
        juce::jlimit(lo, juce::jmax(lo, hi), CustomFilterShapes::hzToX((float)noteHz(best)));
    return tuned;
}

} // namespace synth::ui::ringhelpers
