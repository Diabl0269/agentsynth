#include "ModCurve.h"

namespace synth {

namespace {
bool isFiniteNumber(const juce::var& v) {
    return (v.isDouble() || v.isInt() || v.isInt64()) && std::isfinite((double)v);
}
} // namespace

LfoCustomWave ModCurve::straightShape() {
    LfoCustomWave w;
    w.points = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
    return w;
}

ModCurve ModCurve::linear() { return ModCurve{}; }

ModCurve ModCurve::preset(Preset p) {
    ModCurve c = linear();
    // EnvelopeGenerator::shape: a negative bend starts slow (below the diagonal), a positive one starts fast.
    switch (p) {
    case Preset::Linear:
        break;
    case Preset::Exp:
        c.shape.points = {{0.0f, 0.0f, -0.5f}, {1.0f, 1.0f, 0.0f}};
        break;
    case Preset::Log:
        c.shape.points = {{0.0f, 0.0f, 0.5f}, {1.0f, 1.0f, 0.0f}};
        break;
    case Preset::SCurve:
        c.shape.points = {{0.0f, 0.0f, -0.6f}, {0.5f, 0.5f, 0.6f}, {1.0f, 1.0f, 0.0f}};
        break;
    }
    c.shape.sanitise();
    return c;
}

bool ModCurve::operator==(const ModCurve& o) const {
    return shape == o.shape && rangeMin == o.rangeMin && rangeMax == o.rangeMax;
}

bool ModCurve::isLinear() const { return *this == linear(); }

juce::var ModCurve::toVar() const {
    juce::var v = shape.toVar();
    if (auto* obj = v.getDynamicObject()) {
        obj->setProperty("rangeMin", (double)rangeMin);
        obj->setProperty("rangeMax", (double)rangeMax);
    }
    return v;
}

ModCurve ModCurve::fromVar(const juce::var& v) {
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return linear();

    // LfoCustomWave::fromVar falls back to a triangle, so everything that would trigger it is rejected here first.
    if (obj->hasProperty("version")) {
        const juce::var version = obj->getProperty("version");
        if (!isFiniteNumber(version) || (int)(double)version > LfoCustomWave::kVersion)
            return linear();
    }

    const juce::var pointsVar = obj->getProperty("points");
    if (!pointsVar.isArray())
        return linear();
    int validPoints = 0;
    for (const auto& pv : *pointsVar.getArray()) {
        const auto* po = pv.getDynamicObject();
        if (po != nullptr && isFiniteNumber(po->getProperty("x")) && isFiniteNumber(po->getProperty("y")))
            ++validPoints;
    }
    if (validPoints < LfoCustomWave::kMinPoints)
        return linear();

    ModCurve c;
    const auto readRange = [&](const char* key, float& dest) {
        if (!obj->hasProperty(key))
            return true;
        const juce::var rv = obj->getProperty(key);
        if (!isFiniteNumber(rv))
            return false;
        dest = juce::jlimit(0.0f, 1.0f, (float)(double)rv);
        return true;
    };
    if (!readRange("rangeMin", c.rangeMin) || !readRange("rangeMax", c.rangeMax))
        return linear();
    if (c.rangeMin > c.rangeMax)
        std::swap(c.rangeMin, c.rangeMax);

    c.shape = LfoCustomWave::fromVar(v);
    return c;
}

void ModCurve::renderTable(Table& out) const {
    const float span = rangeMax - rangeMin;
    for (int k = 0; k < kTableSize; ++k)
        out[(size_t)k] = rangeMin + span * shape.evaluate((float)k / (float)kTableSize);
    out[(size_t)kTableSize] = rangeMin + span * shape.points.back().y;
}

} // namespace synth
