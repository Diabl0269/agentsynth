// CustomFilterBlend.cpp -- the blended Custom response as numbers, and its sampling back into a curve.
#include "CustomFilterBlend.h"
#include "CustomFilterCutoff.h"
#include "CustomFilterPeakMorph.h"
#include "Modules/Shapes/CurveSimplify.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <set>

namespace synth::customfilter {

/** The engine's own table renderer, blend weights and peak-moving blend, so a copy made here sounds as the live blend
    does at the same cursor (before shift and depth). */
std::vector<float> blendedGridDb(const CustomFilterShapes& shapes, float x, float y) {
    CustomFilterShapes clean = shapes;
    clean.sanitise();
    const auto table = CustomFilterEngine::renderTable(clean);
    std::vector<float> weights((size_t)table->count, 0.0f);
    CustomFilterEngine::blendWeights(*table, x, y, weights.data());
    std::vector<float> out((size_t)kGridSize, 0.0f);
    blendShapes(*table, weights.data(), out.data());
    return out;
}

/** Mirrors CustomFilterKernelDesigner::fillLogMagnitude: a positive shift moves the response up, so a
    point reads the curve at its own position minus the shift, holding the end values outside the grid. */
std::vector<float> responseGridDb(const CustomFilterShapes& shapes, float x, float y, float shiftSemitones,
                                  float depth) {
    const auto blended = blendedGridDb(shapes, x, y);
    const float dx =
        shiftSemitones / 12.0f * std::log(2.0f) / std::log(CustomFilterShapes::kMaxHz / CustomFilterShapes::kMinHz);
    std::vector<float> out((size_t)kGridSize, 0.0f);
    for (int k = 0; k < kGridSize; ++k) {
        const float at = juce::jlimit(0.0f, 1.0f, (float)k / (float)(kGridSize - 1) - dx);
        const float pos = at * (float)(kGridSize - 1);
        const int i0 = juce::jmin((int)pos, kGridSize - 2);
        const float frac = pos - (float)i0;
        out[(size_t)k] = (blended[(size_t)i0] + (blended[(size_t)i0 + 1] - blended[(size_t)i0]) * frac) * depth;
    }
    return out;
}

std::vector<float> responseGridDb(const CustomFilterShapes& shapes, float x, float y, float shiftSemitones, float depth,
                                  float cutoffHz, float resonance) {
    auto out = responseGridDb(shapes, x, y, shiftSemitones + cutoffShiftSemitones(cutoffHz), depth);
    if (!(resonance > 0.0f))
        return out;
    const float bellX = cutoffX(cutoffHz);
    const float octaves = axisOctaves();
    for (int k = 0; k < kGridSize; ++k)
        out[(size_t)k] += resonanceBellDb(resonance, ((float)k / (float)(kGridSize - 1) - bellX) * octaves);
    return out;
}

namespace {

/** How far a peak must fall for the point on its side: far enough that the straight segments keep its width. */
constexpr float kFlankDropDb = 6.0f;

/** How far the curve drawn through the points may stray from the grid (the curve is linear in dB between points). */
constexpr float kToleranceDb = 0.5f;

/** The grid index of a peak position: of the two bins around a half-bin position, the higher one. */
int peakIndex(const std::vector<float>& db, float pos) {
    const int lo = juce::jlimit(0, (int)db.size() - 1, (int)std::floor(pos));
    const int hi = juce::jlimit(0, (int)db.size() - 1, (int)std::ceil(pos));
    return db[(size_t)hi] > db[(size_t)lo] ? hi : lo;
}

/** The grid indices that carry the shape of `db`, most important first: the two ends, the prominent peaks, the
    lowest dip between neighbouring peaks, then a point on each side of a peak where it has fallen kFlankDropDb. */
std::vector<int> shapeIndices(const std::vector<float>& db) {
    const int last = (int)db.size() - 1;
    std::vector<int> order{0, last};

    std::array<float, CustomFilterEngine::ShapeTable::kMaxPeaks> found{};
    const int count = findPeaks(db.data(), (int)db.size(), found.data());
    std::vector<int> peaks, dips;
    for (int k = 0; k < count; ++k)
        peaks.push_back(peakIndex(db, found[(size_t)k]));
    for (size_t k = 0; k + 1 < peaks.size(); ++k)
        dips.push_back((int)(std::min_element(db.begin() + peaks[k], db.begin() + peaks[k + 1] + 1) - db.begin()));
    order.insert(order.end(), peaks.begin(), peaks.end());
    order.insert(order.end(), dips.begin(), dips.end());

    for (size_t k = 0; k < peaks.size(); ++k) {
        const int from = k == 0 ? 0 : dips[k - 1];
        const int to = k + 1 == peaks.size() ? last : dips[k];
        const float level = db[(size_t)peaks[k]] - kFlankDropDb;
        int left = peaks[k], right = peaks[k];
        while (left > from && db[(size_t)left] > level)
            --left;
        while (right < to && db[(size_t)right] > level)
            ++right;
        order.push_back(left);
        order.push_back(right);
    }
    return order;
}

} // namespace

LfoCustomWave curveFromGridDb(const std::vector<float>& gridDb, int numPoints) {
    LfoCustomWave wave;
    numPoints = juce::jmax(LfoCustomWave::kMinPoints, numPoints);
    const int last = (int)gridDb.size() - 1;
    if (last < 1) {
        for (int p = 0; p < numPoints; ++p)
            wave.points.push_back({(float)p / (float)(numPoints - 1), CustomFilterShapes::dbToY(0.0f), 0.0f});
        wave.sanitise();
        return wave;
    }

    // Pick grid bins: the shape's own points first (a duplicate or an out-of-range bin costs nothing), then a point
    // wherever the straight segments still miss the grid by more than kToleranceDb.
    std::set<int> chosen;
    for (const int index : shapeIndices(gridDb)) {
        if ((int)chosen.size() >= numPoints)
            break;
        chosen.insert(juce::jlimit(0, last, index));
    }
    shapes::addPointsWhereLinesMiss(chosen, gridDb, numPoints, kToleranceDb);

    for (const int index : chosen)
        wave.points.push_back({(float)index / (float)last,
                               juce::jlimit(0.0f, 1.0f, CustomFilterShapes::dbToY(gridDb[(size_t)index])), 0.0f});
    wave.sanitise();
    return wave;
}

} // namespace synth::customfilter
