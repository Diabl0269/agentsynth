// CustomFilterPerNote.cpp -- the per-note settings' JSON round trip and sanitising.
#include "CustomFilterPerNote.h"
#include "CustomFilterShapes.h"
#include <cmath>

namespace synth {

namespace {

bool isFiniteNumber(const juce::var& v) {
    return (v.isDouble() || v.isInt() || v.isInt64()) && std::isfinite((double)v);
}

/** The mode whose id is `text`, if any (exact spelling only). */
bool modeFromId(const juce::String& text, CustomFilterPerNote::Mode& out) {
    for (int i = 0; i < CustomFilterPerNote::kNumModes; ++i) {
        const auto mode = static_cast<CustomFilterPerNote::Mode>(i);
        if (text == CustomFilterPerNote::modeId(mode)) {
            out = mode;
            return true;
        }
    }
    return false;
}

} // namespace

const char* CustomFilterPerNote::modeId(Mode mode) {
    switch (mode) {
    case Mode::ByHand:
        return "byHand";
    case Mode::ByPitch:
        return "byPitch";
    case Mode::Random:
        return "random";
    case Mode::Off:
    default:
        return "off";
    }
}

const char* CustomFilterPerNote::modeLabel(Mode mode) {
    switch (mode) {
    case Mode::ByHand:
        return "By hand";
    case Mode::ByPitch:
        return "By pitch";
    case Mode::Random:
        return "Random";
    case Mode::Off:
    default:
        return "Off";
    }
}

std::array<int8_t, CustomFilterPerNote::kNumKeys> CustomFilterPerNote::makeUnassignedKeys() {
    std::array<int8_t, kNumKeys> keys{};
    keys.fill(kUnassigned);
    return keys;
}

bool CustomFilterPerNote::operator==(const CustomFilterPerNote& other) const {
    return mode == other.mode && smoothness == other.smoothness && keys == other.keys;
}

bool CustomFilterPerNote::isDefault() const { return *this == CustomFilterPerNote{}; }

juce::var CustomFilterPerNote::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", kVersion);
    obj->setProperty("mode", juce::String(modeId(mode)));
    obj->setProperty("smoothness", (double)smoothness);
    juce::Array<juce::var> list;
    for (int note = 0; note < kNumKeys; ++note)
        if (keys[(size_t)note] != kUnassigned)
            list.add(juce::var(juce::Array<juce::var>{note, (int)keys[(size_t)note]}));
    obj->setProperty("keys", list);
    return juce::var(obj);
}

/** A version newer than this build understands is read as the defaults (the shapes beside it still load),
    the same rule the shape list follows. */
CustomFilterPerNote CustomFilterPerNote::fromVar(const juce::var& v) {
    CustomFilterPerNote out;
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return out;
    const juce::var version = obj->getProperty("version");
    if (!isFiniteNumber(version) || (int)version != kVersion)
        return out;
    modeFromId(obj->getProperty("mode").toString(), out.mode);
    if (const juce::var s = obj->getProperty("smoothness"); isFiniteNumber(s))
        out.smoothness = juce::jlimit(0.0f, 1.0f, (float)(double)s);
    if (const juce::var keys = obj->getProperty("keys"); keys.isArray()) {
        for (const auto& entry : *keys.getArray()) {
            if (!entry.isArray() || entry.getArray()->size() != 2)
                continue;
            const juce::var note = entry[0], shape = entry[1];
            if (!isFiniteNumber(note) || !isFiniteNumber(shape))
                continue;
            const int n = (int)note, s = (int)shape;
            if (n >= 0 && n < kNumKeys && s >= 0 && s < CustomFilterShapes::kMaxShapes)
                out.keys[(size_t)n] = (int8_t)s;
        }
    }
    return out;
}

bool CustomFilterPerNote::fromUntrustedVar(const juce::var& v, CustomFilterPerNote& inOut) {
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;
    bool applied = false;
    if (Mode mode; modeFromId(obj->getProperty("mode").toString(), mode)) {
        inOut.mode = mode;
        applied = true;
    }
    if (const juce::var s = obj->getProperty("smoothness"); isFiniteNumber(s)) {
        inOut.smoothness = juce::jlimit(0.0f, 1.0f, (float)(double)s);
        applied = true;
    }
    return applied;
}

} // namespace synth
