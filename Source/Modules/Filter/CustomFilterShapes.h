// CustomFilterShapes.h -- pure data model for the Filter's Custom type: a list of drawn magnitude
// curves ("shapes"), each placed on a morph map, plus its versioned JSON round-trip
// (FilterModule::getExtraState/setExtraState; model output goes through fromUntrustedVar). Message-thread data; the
// audio thread only ever sees the immutable tables CustomFilterEngine renders from it.
#pragma once

#include "Modules/Lfo/LfoCustomWave.h"
#include "Modules/Shapes/MorphShapes.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/** One drawn shape: a magnitude curve in LfoCustomWave's point model (x = log frequency from
 *  kMinHz to kMaxHz, y in [0, 1] mapped linearly to kMinDb..kMaxDb, so y = 0.8 is 0 dB) and a
 *  position on the morph map. The record is shared with the LFO's shape list (morph::Shape). */
using CustomFilterShape = morph::Shape;

/** The whole shape list. Always sanitised to at least one shape (see `sanitise()`). */
struct CustomFilterShapes {
    static constexpr int kVersion = 1;
    static constexpr int kMapDimensions = morph::kMapDimensions; // coordinates the audio path reads
    static constexpr int kMaxShapes = morph::kMaxShapes;         // parse cap only; the format has no count
    static constexpr int kMaxNameLength = morph::kMaxNameLength;
    static constexpr int kMaxUntrustedShapes = morph::kMaxUntrustedShapes;         // model-authored patches
    static constexpr int kMaxUntrustedNameLength = morph::kMaxUntrustedNameLength; // [A-Za-z0-9 space -] only
    static constexpr int kMaxUntrustedMapValues = morph::kMaxUntrustedMapValues;
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static constexpr float kMinDb = -48.0f;
    static constexpr float kMaxDb = 12.0f;

    std::vector<CustomFilterShape> shapes;

    /** Named starting curves a shape can be stamped with (vowels, then animal calls). */
    enum class Preset { Ah, Ee, Oo, Wolf, Crow, Frog };
    static constexpr int kNumPresets = 6;
    static const char* presetName(Preset preset);
    static LfoCustomWave presetCurve(Preset preset);

    /** Three vowel-like shapes ("ah", "ee", "oo") at distinct map positions. */
    static CustomFilterShapes defaultShapes();
    /** A flat 0 dB curve. */
    static LfoCustomWave flatCurve();

    bool operator==(const CustomFilterShapes&) const;
    bool isDefault() const;

    /** `{"version":1,"shapes":[{"name":..,"position":[..],"curve":{LfoCustomWave::toVar}}]}`. */
    juce::var toVar() const;
    /** Never fails: garbage resolves to `defaultShapes()`, a bad curve to `flatCurve()`. */
    static CustomFilterShapes fromVar(const juce::var&);
    /** The model-output reader: keeps only each shape's curve points, up to kMaxUntrustedMapValues map
     *  coordinates (clamped to 0..1) and a name reduced to kMaxUntrustedNameLength characters of
     *  [A-Za-z0-9 space -]; at most kMaxUntrustedShapes shapes. Every other key is ignored. Returns false
     *  and leaves `out` untouched when there is no usable shape list. */
    static bool fromUntrustedVar(const juce::var&, CustomFilterShapes& out);
    /** Clamps every value into range, caps counts and lengths, keeps at least one shape. */
    void sanitise();

    static float yToDb(float y) { return kMinDb + y * (kMaxDb - kMinDb); }
    static float dbToY(float db) { return (db - kMinDb) / (kMaxDb - kMinDb); }
    /** Hz -> curve x, clamped to [0, 1]. */
    static float hzToX(float hz);
    static float xToHz(float x);
};

} // namespace synth
