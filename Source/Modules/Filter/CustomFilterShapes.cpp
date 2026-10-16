// CustomFilterShapes.cpp -- the Custom filter's shape list: defaults, JSON round-trip, sanitising.
#include "CustomFilterShapes.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace {

bool isFiniteNumber(const juce::var& v) {
    return (v.isDouble() || v.isInt() || v.isInt64()) && std::isfinite((double)v);
}

/** A curve built from (Hz, dB) breakpoints with straight (bend 0) segments in dB over log
    frequency. The first and last points are pinned to x = 0 / 1 by sanitise(). */
LfoCustomWave curveFromHzDb(std::initializer_list<std::pair<float, float>> points) {
    LfoCustomWave w;
    for (const auto& [hz, db] : points)
        w.points.push_back({CustomFilterShapes::hzToX(hz), CustomFilterShapes::dbToY(db), 0.0f});
    w.sanitise();
    return w;
}

/** True when `v` looks like an LfoCustomWave object with at least two points to read. Anything
    less resolves to a flat curve here, not to LfoCustomWave::fromVar's triangle fallback, which
    as a filter shape would be a loud band-pass. */
bool hasReadableCurve(const juce::var& v) {
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;
    const juce::var pts = obj->getProperty("points");
    return pts.isArray() && pts.getArray()->size() >= LfoCustomWave::kMinPoints;
}

} // namespace

bool CustomFilterShape::operator==(const CustomFilterShape& other) const {
    return name == other.name && curve == other.curve && position == other.position;
}

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

/** Formant-style curves: peaks near 0 dB at each vowel's first three formants over a floor that
    falls away at both ends. Peaks stay at or below +2 dB so the default never adds gain. */
CustomFilterShapes CustomFilterShapes::defaultShapes() {
    CustomFilterShapes s;
    s.shapes.push_back({"ah",
                        curveFromHzDb({{20.0f, -30.0f},
                                       {400.0f, -14.0f},
                                       {800.0f, 2.0f},
                                       {950.0f, -6.0f},
                                       {1150.0f, 0.0f},
                                       {1800.0f, -16.0f},
                                       {2900.0f, -6.0f},
                                       {5000.0f, -24.0f},
                                       {20000.0f, -40.0f}}),
                        {0.2f, 0.25f}});
    s.shapes.push_back({"ee",
                        curveFromHzDb({{20.0f, -30.0f},
                                       {270.0f, 2.0f},
                                       {700.0f, -20.0f},
                                       {2300.0f, 0.0f},
                                       {2600.0f, -8.0f},
                                       {3000.0f, -2.0f},
                                       {5000.0f, -22.0f},
                                       {20000.0f, -40.0f}}),
                        {0.8f, 0.25f}});
    s.shapes.push_back({"oo",
                        curveFromHzDb({{20.0f, -30.0f},
                                       {300.0f, 2.0f},
                                       {550.0f, -10.0f},
                                       {870.0f, -2.0f},
                                       {1500.0f, -22.0f},
                                       {2250.0f, -12.0f},
                                       {4000.0f, -30.0f},
                                       {20000.0f, -44.0f}}),
                        {0.5f, 0.8f}});
    return s;
}

bool CustomFilterShapes::operator==(const CustomFilterShapes& other) const { return shapes == other.shapes; }

bool CustomFilterShapes::isDefault() const { return *this == defaultShapes(); }

juce::var CustomFilterShapes::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", kVersion);
    juce::Array<juce::var> list;
    for (const auto& shape : shapes) {
        auto* so = new juce::DynamicObject();
        so->setProperty("name", shape.name);
        juce::Array<juce::var> pos;
        for (float c : shape.position)
            pos.add((double)c);
        so->setProperty("position", juce::var(pos));
        so->setProperty("curve", shape.curve.toVar());
        list.add(juce::var(so));
    }
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
    for (const auto& sv : *list.getArray()) {
        const auto* so = sv.getDynamicObject();
        if (so == nullptr)
            continue;
        CustomFilterShape shape;
        shape.name = so->getProperty("name").toString();
        const juce::var curve = so->getProperty("curve");
        shape.curve = hasReadableCurve(curve) ? LfoCustomWave::fromVar(curve) : flatCurve();
        const juce::var pos = so->getProperty("position");
        if (pos.isArray())
            for (const auto& c : *pos.getArray())
                shape.position.push_back(isFiniteNumber(c) ? (float)(double)c : 0.5f);
        out.shapes.push_back(std::move(shape));
        if ((int)out.shapes.size() >= kMaxShapes)
            break;
    }
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
    for (const auto& sv : *list.getArray()) {
        const auto* so = sv.getDynamicObject();
        if (so == nullptr)
            continue;
        CustomFilterShape shape;
        shape.name = so->getProperty("name")
                         .toString()
                         .retainCharacters("abcdefghijklmnopqrstuvwxyz"
                                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                           "0123456789 -")
                         .trim()
                         .substring(0, kMaxUntrustedNameLength);
        const juce::var curve = so->getProperty("curve");
        shape.curve = hasReadableCurve(curve) ? LfoCustomWave::fromVar(curve) : flatCurve();
        const juce::var pos = so->getProperty("position");
        if (pos.isArray())
            for (const auto& c : *pos.getArray()) {
                if ((int)shape.position.size() >= kMaxUntrustedMapValues)
                    break;
                shape.position.push_back(isFiniteNumber(c) ? (float)(double)c : 0.5f);
            }
        parsed.shapes.push_back(std::move(shape));
        if ((int)parsed.shapes.size() >= kMaxUntrustedShapes)
            break;
    }
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
    if ((int)shapes.size() > kMaxShapes)
        shapes.resize((size_t)kMaxShapes);
    for (auto& shape : shapes) {
        shape.name = shape.name.trim().substring(0, kMaxNameLength);
        shape.curve.sanitise();
        while ((int)shape.position.size() < kMapDimensions)
            shape.position.push_back(0.5f);
        for (auto& c : shape.position)
            c = std::isfinite(c) ? juce::jlimit(0.0f, 1.0f, c) : 0.5f;
    }
}

} // namespace synth
