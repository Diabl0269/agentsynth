// CustomFilterShapes.cpp -- the Custom filter's shape list: defaults, JSON round-trip, sanitising.
#include "CustomFilterShapes.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace {

using morph::isFiniteNumber;

/** A curve built from (Hz, dB) breakpoints with straight (bend 0) segments in dB over log
    frequency. The first and last points are pinned to x = 0 / 1 by sanitise(). */
LfoCustomWave curveFromHzDb(std::initializer_list<std::pair<float, float>> points) {
    LfoCustomWave w;
    for (const auto& [hz, db] : points)
        w.points.push_back({CustomFilterShapes::hzToX(hz), CustomFilterShapes::dbToY(db), 0.0f});
    w.sanitise();
    return w;
}

} // namespace

float CustomFilterShapes::hzToX(float hz) {
    if (!(hz > kMinHz))
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz));
}

float CustomFilterShapes::xToHz(float x) { return kMinHz * std::pow(kMaxHz / kMinHz, juce::jlimit(0.0f, 1.0f, x)); }

LfoCustomWave CustomFilterShapes::flatCurve() {
    LfoCustomWave w;
    w.points = {{0.0f, dbToY(0.0f), 0.0f}, {1.0f, dbToY(0.0f), 0.0f}};
    w.sanitise();
    return w;
}

/** The named starting shapes: three vowels as formant-style curves (peaks near 0 dB at each vowel's first
    formants over a floor that falls away at both ends) and three animal calls. Peaks stay at or below
    +2 dB so stamping one never adds gain. */
const char* CustomFilterShapes::presetName(Preset preset) {
    switch (preset) {
    case Preset::Ah:
        return "ah";
    case Preset::Ee:
        return "ee";
    case Preset::Oo:
        return "oo";
    case Preset::Wolf:
        return "wolf";
    case Preset::Crow:
        return "crow";
    case Preset::Frog:
        return "frog";
    }
    return "ah";
}

LfoCustomWave CustomFilterShapes::presetCurve(Preset preset) {
    switch (preset) {
    case Preset::Ah:
        return curveFromHzDb({{20.0f, -30.0f},
                              {400.0f, -14.0f},
                              {800.0f, 2.0f},
                              {950.0f, -6.0f},
                              {1150.0f, 0.0f},
                              {1800.0f, -16.0f},
                              {2900.0f, -6.0f},
                              {5000.0f, -24.0f},
                              {20000.0f, -40.0f}});
    case Preset::Ee:
        return curveFromHzDb({{20.0f, -30.0f},
                              {270.0f, 2.0f},
                              {700.0f, -20.0f},
                              {1400.0f, -16.0f},
                              {2300.0f, 0.0f},
                              {3400.0f, -10.0f},
                              {5000.0f, -22.0f},
                              {20000.0f, -40.0f}});
    case Preset::Oo:
        return curveFromHzDb({{20.0f, -30.0f},
                              {300.0f, 2.0f},
                              {550.0f, -10.0f},
                              {870.0f, -2.0f},
                              {1500.0f, -22.0f},
                              {2250.0f, -12.0f},
                              {4000.0f, -30.0f},
                              {20000.0f, -44.0f}});
    case Preset::Wolf: // a howl: strong low-mid formants, little above 3 kHz
        return curveFromHzDb({{20.0f, -36.0f},
                              {150.0f, -20.0f},
                              {450.0f, 2.0f},
                              {600.0f, -8.0f},
                              {900.0f, 0.0f},
                              {1400.0f, -18.0f},
                              {3000.0f, -30.0f},
                              {20000.0f, -48.0f}});
    case Preset::Crow: // a caw: harsh and nasal, broad energy from 700 Hz to 4 kHz
        return curveFromHzDb({{20.0f, -40.0f},
                              {300.0f, -22.0f},
                              {700.0f, -6.0f},
                              {1100.0f, 0.0f},
                              {1700.0f, -4.0f},
                              {2600.0f, 0.0f},
                              {3800.0f, -8.0f},
                              {6000.0f, -22.0f},
                              {20000.0f, -40.0f}});
    case Preset::Frog: // a ribbit: a low throat resonance and a narrow nasal peak
        return curveFromHzDb({{20.0f, -30.0f},
                              {120.0f, -12.0f},
                              {220.0f, 2.0f},
                              {350.0f, -14.0f},
                              {1000.0f, -10.0f},
                              {1250.0f, 0.0f},
                              {1500.0f, -12.0f},
                              {3500.0f, -30.0f},
                              {20000.0f, -44.0f}});
    }
    return flatCurve();
}

/** ah, ee and oo at three corners of the map; the curves are the presets, so stamping a vowel and
    starting a Filter agree. */
CustomFilterShapes CustomFilterShapes::defaultShapes() {
    CustomFilterShapes s;
    s.shapes.push_back({presetName(Preset::Ah), presetCurve(Preset::Ah), {0.2f, 0.25f}});
    s.shapes.push_back({presetName(Preset::Ee), presetCurve(Preset::Ee), {0.8f, 0.25f}});
    s.shapes.push_back({presetName(Preset::Oo), presetCurve(Preset::Oo), {0.5f, 0.8f}});
    return s;
}

bool CustomFilterShapes::operator==(const CustomFilterShapes& other) const { return shapes == other.shapes; }

bool CustomFilterShapes::isDefault() const { return *this == defaultShapes(); }

juce::var CustomFilterShapes::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", kVersion);
    juce::Array<juce::var> list;
    for (const auto& shape : shapes)
        list.add(morph::shapeToVar(shape));
    obj->setProperty("shapes", juce::var(list));
    return juce::var(obj);
}

/** Version and shape-list problems resolve to the defaults outright; a shape whose own fields are
    unreadable keeps its slot with a flat curve / centred position, so one bad curve never discards
    the others. A non-object list entry is dropped. */
CustomFilterShapes CustomFilterShapes::fromVar(const juce::var& v) {
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return defaultShapes();
    const juce::var version = obj->getProperty("version");
    if (!isFiniteNumber(version) || (int)(double)version > kVersion)
        return defaultShapes();
    const juce::var list = obj->getProperty("shapes");
    if (!list.isArray())
        return defaultShapes();

    CustomFilterShapes out;
    out.shapes = morph::shapesFromVar(list, flatCurve(), false);
    out.sanitise();
    return out;
}

bool CustomFilterShapes::fromUntrustedVar(const juce::var& v, CustomFilterShapes& out) {
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;
    const juce::var list = obj->getProperty("shapes");
    if (!list.isArray())
        return false;

    CustomFilterShapes parsed;
    parsed.shapes = morph::shapesFromVar(list, flatCurve(), true);
    if (parsed.shapes.empty())
        return false;
    parsed.sanitise();
    out = std::move(parsed);
    return true;
}

/** Rules: no shapes -> defaults; more than kMaxShapes -> truncated; each position padded with 0.5
    up to kMapDimensions and every coordinate (including extra dimensions) clamped to [0, 1], a
    non-finite one centred; names trimmed and capped at kMaxNameLength; each curve sanitised by
    LfoCustomWave's own rules. */
void CustomFilterShapes::sanitise() {
    if (shapes.empty()) {
        *this = defaultShapes();
        return;
    }
    morph::sanitiseShapes(shapes);
}

} // namespace synth
