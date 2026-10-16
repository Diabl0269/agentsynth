// MorphShapes.h -- what every "list of drawn shapes on a morph map" shares: the shape record, the blend weights, the
// versioned JSON of one shape and of a list of them, and the sanitising rules. The Filter's Custom type
// (CustomFilterShapes) and the LFO's Custom wave (LfoCustomShapes) are thin wrappers that add their own curve meaning,
// defaults and format version. Message-thread data; the audio thread only ever sees tables rendered from it.
#pragma once

#include "Modules/Lfo/LfoCustomWave.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::morph {

/** One drawn shape: a name, a curve in LfoCustomWave's point model and a position on the morph map (each
 *  coordinate in [0, 1]; two used today, more allowed). */
struct Shape {
    juce::String name;
    LfoCustomWave curve;
    std::vector<float> position;

    bool operator==(const Shape&) const;
};

constexpr int kMapDimensions = 2; // coordinates the audio path reads
constexpr int kMaxShapes = 64;    // parse cap only; the format itself has no count
constexpr int kMaxNameLength = 64;
constexpr int kMaxUntrustedShapes = 16;     // model-authored patches
constexpr int kMaxUntrustedNameLength = 24; // [A-Za-z0-9 space -] only
constexpr int kMaxUntrustedMapValues = 3;

/** w_i = 1 / max(d_i, 0.02)^2.5, normalised, d_i the distance from (x, y) to shape i at (xs[i], ys[i]). The distance
 *  floor keeps a cursor sitting on a shape finite while still letting that shape dominate (~1e4 : 1 against a shape a
 *  whole map-width away). Writes `count` weights summing to 1. Allocation-free. */
void blendWeights(const float* xs, const float* ys, int count, float x, float y, float* out) noexcept;

/** The index of the shape whose position is closest to (x, y): the one that dominates the blend there. `Shapes` is any
 *  list of records with a `position` (morph::Shape, the editor's ShapeItem). 0 for an empty list; a missing coordinate
 *  counts as the map centre. */
template <class Shapes>
int nearestShapeIndex(const Shapes& shapes, float x, float y) noexcept {
    int best = 0;
    float bestDistance = 1.0e9f;
    for (int i = 0; i < (int)shapes.size(); ++i) {
        const auto& p = shapes[(size_t)i].position;
        const float dx = (p.size() > 0 ? p[0] : 0.5f) - x;
        const float dy = (p.size() > 1 ? p[1] : 0.5f) - y;
        if (const float d = dx * dx + dy * dy; d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

/** A finite int or double. */
bool isFiniteNumber(const juce::var& v);

/** True when `v` looks like an LfoCustomWave object with at least two points to read. */
bool hasReadableCurve(const juce::var& v);

/** `{"name":..,"position":[..],"curve":{LfoCustomWave::toVar}}`. */
juce::var shapeToVar(const Shape& shape);

/** The shapes of a JSON list. A shape whose curve is unreadable gets `unreadableCurve`; a non-object entry is dropped;
 *  a missing position stays empty (sanitiseShapes pads it). Trusted input keeps up to kMaxShapes shapes; untrusted
 *  (model) input keeps up to kMaxUntrustedShapes, only the curve points, up to kMaxUntrustedMapValues map coordinates
 *  and a name reduced to kMaxUntrustedNameLength characters of [A-Za-z0-9 space -]. Not sanitised, may be empty. */
std::vector<Shape> shapesFromVar(const juce::var& list, const LfoCustomWave& unreadableCurve, bool untrusted);

/** Caps the count at kMaxShapes, trims and caps names at kMaxNameLength, sanitises each curve, pads every position
 *  with 0.5 up to kMapDimensions and clamps every coordinate to [0, 1] (non-finite centred). Leaves an empty list
 *  empty: the owner decides its default. */
void sanitiseShapes(std::vector<Shape>& shapes);

} // namespace synth::morph
