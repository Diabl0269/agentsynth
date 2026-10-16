// CustomFilterBlend.cpp -- the blended Custom response as numbers, and its sampling back into a curve.
#include "CustomFilterBlend.h"
#include <cmath>

namespace synth::customfilter {

/** The engine's own table renderer and blend weights, so a copy made here sounds as the live blend
    does at the same cursor (before shift and depth). */
std::vector<float> blendedGridDb(const CustomFilterShapes& shapes, float x, float y) {
    CustomFilterShapes clean = shapes;
    clean.sanitise();
    const auto table = CustomFilterEngine::renderTable(clean);
    std::vector<float> weights((size_t)table->count, 0.0f);
    CustomFilterEngine::blendWeights(*table, x, y, weights.data());
    std::vector<float> out((size_t)kGridSize, 0.0f);
    for (int s = 0; s < table->count; ++s)
        for (int i = 0; i < kGridSize; ++i)
            out[(size_t)i] += weights[(size_t)s] * table->db[(size_t)(s * kGridSize + i)];
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

LfoCustomWave curveFromGridDb(const std::vector<float>& gridDb, int numPoints) {
    LfoCustomWave wave;
    numPoints = juce::jmax(LfoCustomWave::kMinPoints, numPoints);
    const int last = (int)gridDb.size() - 1;
    for (int p = 0; p < numPoints; ++p) {
        const float x = (float)p / (float)(numPoints - 1);
        float db = 0.0f;
        if (last >= 1) {
            const float pos = x * (float)last;
            const int i0 = juce::jmin((int)pos, last - 1);
            db = gridDb[(size_t)i0] + (gridDb[(size_t)i0 + 1] - gridDb[(size_t)i0]) * (pos - (float)i0);
        }
        wave.points.push_back({x, juce::jlimit(0.0f, 1.0f, CustomFilterShapes::dbToY(db)), 0.0f});
    }
    wave.sanitise();
    return wave;
}

} // namespace synth::customfilter
