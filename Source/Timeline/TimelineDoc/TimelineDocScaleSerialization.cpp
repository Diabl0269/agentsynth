// Concern: the piano roll scale fields of a clip in the toVar/fromVar JSON dialect.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

namespace synth {
namespace detail {

namespace {
// Whole numbers only: a float or string in a scale field is malformed, and malformed means "no scale".
bool readWholeNumber(const juce::var& v, int& out) {
    if (!v.isInt() && !v.isInt64())
        return false;
    const auto value = static_cast<juce::int64>(v);
    if (value < 0 || value > 0xFFFF)
        return false;
    out = static_cast<int>(value);
    return true;
}
} // namespace

std::optional<synth::MusicalScale> normaliseClipScale(const std::optional<synth::MusicalScale>& scale) {
    if (!scale.has_value() || (scale->mask & 0xFFF) == 0)
        return std::nullopt; // an empty mask contains no pitch at all, so it is no scale
    return makeScaleFromMask(juce::jlimit(0, 11, scale->rootPitchClass), scale->mask, scale->name);
}

void writeClipScaleFields(juce::DynamicObject& obj, const Clip& clip) {
    if (const auto scale = normaliseClipScale(clip.scale)) {
        juce::DynamicObject::Ptr s = new juce::DynamicObject();
        s->setProperty("root", scale->rootPitchClass);
        s->setProperty("mask", static_cast<int>(scale->mask));
        s->setProperty("name", scale->name);
        obj.setProperty("scale", juce::var(s.get()));
    }
    if (clip.scaleVisible)
        obj.setProperty("scaleVisible", true);
}

// A scale is a convenience, never a reason to refuse a project: anything unreadable loads as none.
bool readClipScaleFields(const juce::DynamicObject& obj, Clip& clip) {
    clip.scale = std::nullopt;
    clip.scaleVisible = false;

    const juce::var scaleVar = obj.getProperty("scale");
    if (const auto* s = scaleVar.getDynamicObject()) {
        int root = 0;
        int mask = 0;
        const juce::var nameVar = s->getProperty("name");
        if (readWholeNumber(s->getProperty("root"), root) && readWholeNumber(s->getProperty("mask"), mask))
            clip.scale = normaliseClipScale(
                makeScaleFromMask(juce::jlimit(0, 11, root), static_cast<std::uint16_t>(mask & 0xFFF),
                                  nameVar.isString() ? nameVar.toString() : juce::String()));
    }

    const juce::var visibleVar = obj.getProperty("scaleVisible");
    clip.scaleVisible = visibleVar.isBool() && static_cast<bool>(visibleVar);
    return true;
}

} // namespace detail
} // namespace synth
