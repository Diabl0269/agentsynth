// MorphShapes.cpp -- blend weights, JSON and sanitising shared by the Filter's and the LFO's shape lists.
#include "MorphShapes.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace synth::morph {

namespace {
constexpr float kMinDistance = 0.02f;
constexpr float kDistancePower = 2.5f;
} // namespace

bool isFiniteNumber(const juce::var& v) {
    return (v.isDouble() || v.isInt() || v.isInt64()) && std::isfinite((double)v);
}

bool Shape::operator==(const Shape& other) const {
    return name == other.name && curve == other.curve && position == other.position;
}

void blendWeights(const float* xs, const float* ys, int count, float x, float y, float* out) noexcept {
    float sum = 0.0f;
    for (int s = 0; s < count; ++s) {
        const float dx = x - xs[s];
        const float dy = y - ys[s];
        const float d = juce::jmax(kMinDistance, std::sqrt(dx * dx + dy * dy));
        out[s] = 1.0f / std::pow(d, kDistancePower);
        sum += out[s];
    }
    for (int s = 0; s < count; ++s)
        out[s] /= sum;
}

bool hasReadableCurve(const juce::var& v) {
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;
    const juce::var pts = obj->getProperty("points");
    return pts.isArray() && pts.getArray()->size() >= LfoCustomWave::kMinPoints;
}

juce::var shapeToVar(const Shape& shape) {
    auto* so = new juce::DynamicObject();
    so->setProperty("name", shape.name);
    juce::Array<juce::var> pos;
    for (float c : shape.position)
        pos.add((double)c);
    so->setProperty("position", juce::var(pos));
    so->setProperty("curve", shape.curve.toVar());
    return juce::var(so);
}

std::vector<Shape> shapesFromVar(const juce::var& list, const LfoCustomWave& unreadableCurve, bool untrusted) {
    std::vector<Shape> out;
    if (!list.isArray())
        return out;
    const int maxShapes = untrusted ? kMaxUntrustedShapes : kMaxShapes;
    const int maxPosition = untrusted ? kMaxUntrustedMapValues : std::numeric_limits<int>::max();
    for (const auto& sv : *list.getArray()) {
        const auto* so = sv.getDynamicObject();
        if (so == nullptr)
            continue;
        Shape shape;
        shape.name = so->getProperty("name").toString();
        if (untrusted)
            shape.name = shape.name
                             .retainCharacters("abcdefghijklmnopqrstuvwxyz"
                                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                               "0123456789 -")
                             .trim()
                             .substring(0, kMaxUntrustedNameLength);
        const juce::var curve = so->getProperty("curve");
        shape.curve = hasReadableCurve(curve) ? LfoCustomWave::fromVar(curve) : unreadableCurve;
        const juce::var pos = so->getProperty("position");
        if (pos.isArray())
            for (const auto& c : *pos.getArray()) {
                if ((int)shape.position.size() >= maxPosition)
                    break;
                shape.position.push_back(isFiniteNumber(c) ? (float)(double)c : 0.5f);
            }
        out.push_back(std::move(shape));
        if ((int)out.size() >= maxShapes)
            break;
    }
    return out;
}

void sanitiseShapes(std::vector<Shape>& shapes) {
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

} // namespace synth::morph
