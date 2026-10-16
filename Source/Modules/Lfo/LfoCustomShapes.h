// LfoCustomShapes.h -- pure data model for the LFO's Custom wave as a list of shapes placed on a morph map: each an
// LfoCustomWave curve (y in [0, 1], x = phase), a map position and a name, the same record the Filter's Custom type
// uses (morph::Shape). Carries the JSON round-trip (LFOModule::getExtraState/setExtraState; model output goes
// through fromUntrustedVar). Message-thread data; the audio thread only sees tables LfoShapeBlender renders from it.
#pragma once

#include "Modules/Shapes/MorphShapes.h"
#include <vector>

namespace synth {

/** The whole shape list. Always sanitised to at least one shape (see `sanitise()`). One unnamed shape at the map
 *  centre is the "plain" wave every LFO had before shapes existed: it plays and saves exactly as that wave did. */
struct LfoCustomShapes {
    static constexpr int kVersion = 1;
    static constexpr int kMaxShapes = morph::kMaxShapes;
    static constexpr int kMaxUntrustedShapes = morph::kMaxUntrustedShapes;

    std::vector<morph::Shape> shapes;

    /** One Triangle shape (LfoCustomWave::defaultWave()). */
    static LfoCustomShapes defaultShapes();
    /** `wave` as the only shape: unnamed, at the map centre. */
    static LfoCustomShapes single(const LfoCustomWave& wave);

    bool operator==(const LfoCustomShapes&) const;
    bool isDefault() const;
    /** One unnamed shape at the map centre: the list needs no shape list in its JSON. */
    bool isPlainSingle() const;

    /** A plain single shape saves as the bare LfoCustomWave JSON (`{"version":1,"points":[..]}`, what every LFO
     *  saved before shapes existed), anything else as `{"version":1,"shapes":[{name,position,curve}..]}`. */
    juce::var toVar() const;
    /** Never fails. A bare LfoCustomWave object (no "shapes" key) becomes one shape; garbage resolves to
     *  `defaultShapes()`; a bad curve inside a list to the Triangle. */
    static LfoCustomShapes fromVar(const juce::var&);
    /** True when `v` is an object with a "shapes" key, i.e. the list format rather than the bare curve. */
    static bool isShapeListVar(const juce::var& v);
    /** The model-output reader for the list format (see morph::shapesFromVar for the caps). Returns false and
     *  leaves `out` untouched when there is no usable shape. */
    static bool fromUntrustedVar(const juce::var&, LfoCustomShapes& out);
    /** Clamps every value into range, caps counts and lengths, keeps at least one shape. */
    void sanitise();
};

} // namespace synth
