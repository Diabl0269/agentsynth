#include "LfoCustomWave.h"
#include <algorithm>
#include <cmath>

namespace synth {

LfoCustomWave LfoCustomWave::defaultWave() { return preset(Preset::Triangle); }

LfoCustomWave LfoCustomWave::preset(Preset p) {
    LfoCustomWave w;
    switch (p) {
    case Preset::Triangle:
        w.points = {{0.0f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
        break;
    case Preset::RampUp:
        w.points = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
        break;
    case Preset::RampDown:
        w.points = {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
        break;
    case Preset::Square:
        w.points = {{0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
        break;
    case Preset::Pulse:
        w.points = {{0.0f, 1.0f, 0.0f}, {0.25f, 1.0f, 0.0f}, {0.25f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
        break;
    case Preset::Steps4:
        w.points = {{0.0f, 0.0f, 0.0f},        {0.25f, 0.0f, 0.0f},       {0.25f, 1.0f / 3.0f, 0.0f},
                    {0.5f, 1.0f / 3.0f, 0.0f}, {0.5f, 2.0f / 3.0f, 0.0f}, {0.75f, 2.0f / 3.0f, 0.0f},
                    {0.75f, 1.0f, 0.0f},       {1.0f, 1.0f, 0.0f}};
        break;
    case Preset::SoftSine:
        w.points = {
            {0.0f, 0.5f, -0.6f}, {0.25f, 1.0f, 0.6f}, {0.5f, 0.5f, -0.6f}, {0.75f, 0.0f, 0.6f}, {1.0f, 0.5f, 0.0f}};
        break;
    }
    w.sanitise();
    return w;
}

void LfoCustomWave::apply(Tool tool) {
    switch (tool) {
    case Tool::Invert:
        for (auto& p : points)
            p.y = 1.0f - p.y;
        break;

    case Tool::Reverse: {
        const int n = (int)points.size();
        std::vector<Point> reversed((size_t)n);
        for (int i = 0; i < n; ++i) {
            const Point& src = points[(size_t)(n - 1 - i)];
            reversed[(size_t)i].x = 1.0f - src.x;
            reversed[(size_t)i].y = src.y;
        }
        // Bend belongs to the OUTGOING segment. After reversal, new segment k (between new points
        // k and k+1) is the mirror of old segment (n-2-k) (between old points (n-2-k) and
        // (n-1-k)), traversed in the opposite direction -- so its bend is that old segment's own
        // outgoing bend, negated (a bend that bulged the curve toward the higher x side now bulges
        // it toward the lower x side once the axis is flipped).
        for (int k = 0; k < n - 1; ++k)
            reversed[(size_t)k].bend = -points[(size_t)(n - 2 - k)].bend;
        if (n > 0)
            reversed[(size_t)(n - 1)].bend = 0.0f;
        points = std::move(reversed);
        break;
    }

    case Tool::Straighten:
        for (auto& p : points)
            p.bend = 0.0f;
        break;

    case Tool::Clear:
        points = {{0.0f, 0.5f, 0.0f}, {1.0f, 0.5f, 0.0f}};
        break;
    }
    sanitise();
}

bool LfoCustomWave::operator==(const LfoCustomWave& other) const {
    if (points.size() != other.points.size())
        return false;
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = other.points[i];
        if (a.x != b.x || a.y != b.y || a.bend != b.bend)
            return false;
    }
    return true;
}

bool LfoCustomWave::isDefault() const { return *this == defaultWave(); }

juce::var LfoCustomWave::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", kVersion);

    juce::Array<juce::var> pts;
    for (const auto& p : points) {
        auto* po = new juce::DynamicObject();
        po->setProperty("x", (double)p.x);
        po->setProperty("y", (double)p.y);
        po->setProperty("bend", (double)p.bend);
        pts.add(juce::var(po));
    }
    obj->setProperty("points", juce::var(pts));
    return juce::var(obj);
}

namespace {
bool isFiniteNumericVar(const juce::var& v) {
    return (v.isDouble() || v.isInt() || v.isInt64()) && std::isfinite((double)v);
}
} // namespace

LfoCustomWave LfoCustomWave::fromVar(const juce::var& v) {
    // Rules 1-3 (not-an-object / bad-or-missing version / points-not-an-array) resolve to the
    // default wave outright, before a single point is parsed -- see sanitise()'s own doc comment
    // for rules 4-8, which this hands off to once a raw (possibly still-invalid) point list exists.
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return defaultWave();

    const juce::var versionVar = obj->getProperty("version");
    if (!isFiniteNumericVar(versionVar))
        return defaultWave();
    if ((int)(double)versionVar > kVersion)
        return defaultWave();

    const juce::var pointsVar = obj->getProperty("points");
    if (!pointsVar.isArray())
        return defaultWave();

    LfoCustomWave wave;
    for (const auto& pv : *pointsVar.getArray()) {
        const auto* po = pv.getDynamicObject();
        if (po == nullptr)
            continue; // rule 4: not an object -- dropped

        const juce::var xv = po->getProperty("x");
        const juce::var yv = po->getProperty("y");
        if (!isFiniteNumericVar(xv) || !isFiniteNumericVar(yv))
            continue; // rule 4: non-finite/non-numeric x or y -- dropped

        Point p;
        p.x = (float)(double)xv;
        p.y = (float)(double)yv;
        const juce::var bv = po->getProperty("bend");
        p.bend = isFiniteNumericVar(bv) ? (float)(double)bv : 0.0f; // rule 5: missing/non-finite -> 0
        wave.points.push_back(p);
    }

    wave.sanitise();
    return wave;
}

void LfoCustomWave::sanitise() {
    // Defence in depth for a caller that builds a wave directly (never through fromVar) with a
    // stray non-finite coordinate -- fromVar itself already drops these before they arrive here.
    points.erase(std::remove_if(points.begin(), points.end(),
                                [](const Point& p) { return !std::isfinite(p.x) || !std::isfinite(p.y); }),
                 points.end());

    for (auto& p : points) {
        p.x = juce::jlimit(0.0f, 1.0f, p.x);
        p.y = juce::jlimit(0.0f, 1.0f, p.y);
        p.bend = std::isfinite(p.bend) ? juce::jlimit(-1.0f, 1.0f, p.bend) : 0.0f;
    }

    std::stable_sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.x < b.x; });
    if ((int)points.size() > kMaxPoints)
        points.resize((size_t)kMaxPoints);

    if ((int)points.size() < kMinPoints) {
        *this = defaultWave();
        return;
    }

    points.front().x = 0.0f;
    points.back().x = 1.0f;
    points.back().bend = 0.0f;
}

float LfoCustomWave::evaluate(float phase) const {
    const int n = (int)points.size();
    if (n < kMinPoints)
        return 0.5f; // unreachable post-sanitise; a safe flat fallback if it ever isn't

    for (int i = 0; i < n - 1; ++i) {
        const float x0 = points[(size_t)i].x;
        const float x1 = points[(size_t)i + 1].x;
        if (x1 <= x0)
            continue; // zero-length segment: never matched, so the NEXT segment claims its x
                      // (right-continuity at a step)
        if (phase >= x0 && phase < x1) {
            const float t = (phase - x0) / (x1 - x0);
            const float shaped = EnvelopeGenerator::shape(t, points[(size_t)i].bend);
            return points[(size_t)i].y + (points[(size_t)i + 1].y - points[(size_t)i].y) * shaped;
        }
    }
    return points.back().y; // phase >= x.back() (or every segment before it was zero-length)
}

void LfoCustomWave::renderTable(Table& out) const {
    for (int k = 0; k < kTableSize; ++k)
        out[(size_t)k] = evaluate((float)k / (float)kTableSize);
    out[(size_t)kTableSize] = out[0];
}

} // namespace synth
