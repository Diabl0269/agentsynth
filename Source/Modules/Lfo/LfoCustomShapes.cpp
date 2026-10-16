// LfoCustomShapes.cpp -- the LFO's Custom shape list: defaults, JSON round-trip, sanitising.
#include "LfoCustomShapes.h"

namespace synth {

LfoCustomShapes LfoCustomShapes::defaultShapes() { return single(LfoCustomWave::defaultWave()); }

LfoCustomShapes LfoCustomShapes::single(const LfoCustomWave& wave) {
    LfoCustomShapes s;
    s.shapes.push_back({juce::String(), wave, {0.5f, 0.5f}});
    s.sanitise();
    return s;
}

bool LfoCustomShapes::operator==(const LfoCustomShapes& other) const { return shapes == other.shapes; }

bool LfoCustomShapes::isDefault() const { return *this == defaultShapes(); }

bool LfoCustomShapes::isPlainSingle() const {
    if (shapes.size() != 1)
        return false;
    const auto& s = shapes.front();
    return s.name.isEmpty() && s.position.size() == (size_t)morph::kMapDimensions && s.position[0] == 0.5f &&
           s.position[1] == 0.5f;
}

juce::var LfoCustomShapes::toVar() const {
    if (isPlainSingle())
        return shapes.front().curve.toVar();
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", kVersion);
    juce::Array<juce::var> list;
    for (const auto& shape : shapes)
        list.add(morph::shapeToVar(shape));
    obj->setProperty("shapes", juce::var(list));
    return juce::var(obj);
}

bool LfoCustomShapes::isShapeListVar(const juce::var& v) {
    const auto* obj = v.getDynamicObject();
    return obj != nullptr && obj->hasProperty("shapes");
}

/** Version and shape-list problems resolve to the default outright; a shape whose curve is unreadable keeps its
    slot with the Triangle, so one bad curve never discards the others. */
LfoCustomShapes LfoCustomShapes::fromVar(const juce::var& v) {
    if (!isShapeListVar(v))
        return single(LfoCustomWave::fromVar(v));
    const auto* obj = v.getDynamicObject();
    const juce::var version = obj->getProperty("version");
    if (!morph::isFiniteNumber(version) || (int)(double)version > kVersion)
        return defaultShapes();
    LfoCustomShapes out;
    out.shapes = morph::shapesFromVar(obj->getProperty("shapes"), LfoCustomWave::defaultWave(), false);
    out.sanitise();
    return out;
}

bool LfoCustomShapes::fromUntrustedVar(const juce::var& v, LfoCustomShapes& out) {
    if (!isShapeListVar(v))
        return false;
    LfoCustomShapes parsed;
    parsed.shapes =
        morph::shapesFromVar(v.getDynamicObject()->getProperty("shapes"), LfoCustomWave::defaultWave(), true);
    if (parsed.shapes.empty())
        return false;
    parsed.sanitise();
    out = std::move(parsed);
    return true;
}

void LfoCustomShapes::sanitise() {
    if (shapes.empty()) {
        *this = defaultShapes();
        return;
    }
    morph::sanitiseShapes(shapes);
}

} // namespace synth
