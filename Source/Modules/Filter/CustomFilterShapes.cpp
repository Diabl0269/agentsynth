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

struct HzDbBend {
    float hz, db, bend;
};

/** A curve built from (Hz, dB, bend) breakpoints: dB over log frequency, each bend shaping its outgoing
    segment. The first and last points are pinned to x = 0 / 1 by sanitise(). */
template <size_t N>
LfoCustomWave curveFromHzDbBend(const HzDbBend (&points)[N]) {
    LfoCustomWave w;
    for (const auto& p : points)
        w.points.push_back({CustomFilterShapes::hzToX(p.hz), CustomFilterShapes::dbToY(p.db), p.bend});
    w.sanitise();
    return w;
}

/** The vowels, drawn from the Csound manual's tenor formant table (formant frequency, level and -3 dB
    bandwidth for a, e, i and u; "ih" is not in that table and uses Peterson and Barney's male F1-F3 of
    390 / 1990 / 2550 Hz). Each formant is a tent of three points (the -3 dB points either side of its
    peak) with a bent skirt down to a deep valley shared with its neighbour, so the peak sits at the
    table frequency. The widths are filter-style (F1 about a third of an octave), wider than the table's
    sung-voice widths: a filter's peak narrower than a note's harmonic spacing falls between harmonics
    and passes almost nothing. The other formants follow the table's relative levels, and each vowel is
    offset so a saw comes out at about the same loudness through any of them, with the loudest peak
    under +3 dB. The floor falls away below F1 and above the last formant. */
static constexpr HzDbBend kAh[] = {
    {20.0f, -39.7f, -1.00f},   {572.0f, -0.7f, 0.20f},   {650.0f, 2.3f, -0.25f},   {728.0f, -0.7f, -0.10f},
    {863.0f, -20.3f, 0.05f},   {988.0f, -6.7f, 0.25f},   {1080.0f, -3.7f, -0.25f}, {1172.0f, -6.7f, -0.15f},
    {1429.0f, -45.7f, -1.00f}, {2525.0f, -7.7f, 0.25f},  {2650.0f, -4.7f, 1.00f},  {2900.0f, -5.7f, -0.25f},
    {3025.0f, -8.7f, 0.95f},   {3525.0f, -23.7f, 1.00f}, {5000.0f, -35.7f, 1.00f}, {20000.0f, -47.0f, 0.00f},
};
static constexpr HzDbBend kEe[] = {
    {20.0f, -43.0f, -0.65f},   {235.0f, -4.0f, 0.20f},   {290.0f, -1.0f, -0.30f},   {345.0f, -4.0f, -0.15f},
    {512.0f, -47.0f, -1.00f},  {1795.0f, -19.0f, 0.25f}, {1870.0f, -16.0f, -0.25f}, {1945.0f, -19.0f, -0.10f},
    {2120.0f, -47.0f, -0.40f}, {2675.0f, -22.0f, 0.25f}, {2800.0f, -19.0f, -0.25f}, {2925.0f, -22.0f, -0.05f},
    {3036.0f, -29.7f, 0.05f},  {3125.0f, -24.0f, 0.25f}, {3250.0f, -21.0f, -0.25f}, {3375.0f, -24.0f, 0.80f},
    {3875.0f, -39.0f, 0.00f},  {5000.0f, -39.0f, 0.20f}, {20000.0f, -47.0f, 0.00f},
};
static constexpr HzDbBend kOo[] = {
    {20.0f, -41.0f, -1.00f},  {290.0f, -2.0f, 0.20f},   {350.0f, 1.0f, -0.30f},    {410.0f, -2.0f, -0.15f},
    {528.0f, -25.3f, 0.05f},  {550.0f, -22.0f, 0.25f},  {600.0f, -19.0f, -0.25f},  {650.0f, -22.0f, -0.10f},
    {756.0f, -47.0f, -1.00f}, {2575.0f, -19.0f, 0.25f}, {2700.0f, -16.0f, -0.15f}, {2900.0f, -13.0f, -0.25f},
    {3025.0f, -16.0f, 0.90f}, {3525.0f, -31.0f, 1.00f}, {5000.0f, -37.0f, 0.85f},  {20000.0f, -47.0f, 0.00f},
};
static constexpr HzDbBend kEh[] = {
    {20.0f, -42.0f, -1.00f},   {335.0f, -3.0f, 0.20f},   {400.0f, 0.0f, -0.30f},    {465.0f, -3.0f, -0.15f},
    {663.0f, -47.0f, -1.00f},  {1610.0f, -17.0f, 0.25f}, {1700.0f, -14.0f, -0.25f}, {1790.0f, -17.0f, -0.10f},
    {2004.0f, -47.0f, -0.15f}, {2475.0f, -15.0f, 0.25f}, {2600.0f, -12.0f, -0.25f}, {2725.0f, -15.0f, -0.10f},
    {2909.0f, -30.3f, 0.05f},  {3075.0f, -17.0f, 0.25f}, {3200.0f, -14.0f, -0.25f}, {3325.0f, -17.0f, 0.90f},
    {3825.0f, -32.0f, 1.00f},  {5000.0f, -38.0f, 0.40f}, {20000.0f, -47.0f, 0.00f},
};
static constexpr HzDbBend kIh[] = {
    {20.0f, -40.9f, -1.00f},   {335.0f, -1.9f, 0.20f},   {390.0f, 1.1f, -0.30f},    {445.0f, -1.9f, -0.15f},
    {613.0f, -46.9f, -1.00f},  {1910.0f, -13.9f, 0.25f}, {1990.0f, -10.9f, -0.25f}, {2070.0f, -13.9f, -0.10f},
    {2221.0f, -35.7f, 0.10f},  {2425.0f, -17.9f, 0.25f}, {2550.0f, -14.9f, -0.25f}, {2675.0f, -17.9f, -0.10f},
    {2952.0f, -46.0f, 0.10f},  {3175.0f, -25.9f, 0.25f}, {3300.0f, -22.9f, -0.25f}, {3425.0f, -25.9f, 0.75f},
    {3925.0f, -40.9f, -1.00f}, {5000.0f, -36.9f, 0.90f}, {20000.0f, -47.0f, 0.00f},
};

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

/** The named starting shapes: five vowels as formant curves (a narrow peak at each of the first formants over a
    floor that falls away at both ends) and three animal calls. Peaks stay under +3 dB, well inside the
    gain audit's +6 dB, so stamping one never makes a patch jump in level. */
const char* CustomFilterShapes::presetName(Preset preset) {
    switch (preset) {
    case Preset::Ah:
        return "ah";
    case Preset::Ee:
        return "ee";
    case Preset::Oo:
        return "oo";
    case Preset::Eh:
        return "eh";
    case Preset::Ih:
        return "ih";
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
        return curveFromHzDbBend(kAh);
    case Preset::Ee:
        return curveFromHzDbBend(kEe);
    case Preset::Oo:
        return curveFromHzDbBend(kOo);
    case Preset::Eh:
        return curveFromHzDbBend(kEh);
    case Preset::Ih:
        return curveFromHzDbBend(kIh);
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
