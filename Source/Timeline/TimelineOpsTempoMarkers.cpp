// Concern: reading and describing the timelineOps `setTempo` and `addMarker` ops.
#include "TimelineOpsTempoMarkers.h"

#include "TimelineValidator.h"

#include <cmath>
#include <initializer_list>

namespace synth {

namespace {

bool readNumber(const juce::var& v, double& out) {
    if (!(v.isDouble() || v.isInt() || v.isInt64()))
        return false;
    out = static_cast<double>(v);
    return std::isfinite(out);
}

// Same closed-object rule as every other op (see TimelineOps.cpp's unknownKey): an unrecognised key rejects.
juce::String firstUnknownKey(juce::DynamicObject& o, std::initializer_list<const char*> allowed) {
    for (int i = 0; i < o.getProperties().size(); ++i) {
        const juce::String key = o.getProperties().getName(i).toString();
        bool known = false;
        for (const char* candidate : allowed)
            known = known || key == candidate;
        if (!known)
            return key;
    }
    return {};
}

juce::String numberText(double v) {
    if (v == std::floor(v))
        return juce::String(static_cast<juce::int64>(v));
    return juce::String(v, 2).trimCharactersAtEnd("0");
}

constexpr int kMaxPreviewedMarkers = 4;

} // namespace

juce::String readSetTempoOp(juce::DynamicObject& op, double& bpm) {
    if (const auto bad = firstUnknownKey(op, {"op", "bpm"}); bad.isNotEmpty())
        return "has an unknown field \"" + bad + "\". A setTempo op accepts only \"bpm\".";
    if (!readNumber(op.getProperty("bpm"), bpm))
        return "needs a numeric \"bpm\".";
    if (bpm < kOpMinTempoBpm || bpm > kOpMaxTempoBpm)
        return "asks for " + numberText(bpm) + " bpm. The tempo must be between " + numberText(kOpMinTempoBpm) +
               " and " + numberText(kOpMaxTempoBpm) + ".";
    return {};
}

juce::String readAddMarkerOp(juce::DynamicObject& op, MarkerOpFields& out) {
    if (const auto bad = firstUnknownKey(op, {"op", "beat", "name"}); bad.isNotEmpty())
        return "has an unknown field \"" + bad + "\". An addMarker op accepts only \"beat\" and \"name\".";
    if (!readNumber(op.getProperty("beat"), out.beat))
        return "needs a numeric \"beat\".";
    if (out.beat < 0.0 || out.beat > kMaxPpqUntrusted)
        return "has a \"beat\" of " + numberText(out.beat) + ". It must be between 0 and " +
               juce::String(kMaxPpqUntrusted) + ".";
    const juce::var nameVar = op.getProperty("name");
    if (!nameVar.isString())
        return "needs a string \"name\".";
    out.name = nameVar.toString();
    if (out.name.isEmpty())
        return "has an empty \"name\". A marker needs a name.";
    if (out.name.length() > kOpMaxMarkerNameChars)
        return "has a \"name\" of " + juce::String(out.name.length()) + " characters, exceeding the limit of " +
               juce::String(kOpMaxMarkerNameChars) + ".";
    return {};
}

juce::String describeSetTempo(double bpm) { return "sets the tempo to " + numberText(bpm) + " BPM"; }

juce::String describeMarkers(const std::vector<MarkerOpFields>& markers) {
    if (markers.empty())
        return {};
    auto one = [](const MarkerOpFields& m) { return "\"" + m.name + "\" at beat " + numberText(m.beat); };
    if (markers.size() == 1)
        return "adds marker " + one(markers.front());
    juce::StringArray shown;
    for (size_t i = 0; i < markers.size() && i < static_cast<size_t>(kMaxPreviewedMarkers); ++i)
        shown.add(one(markers[i]));
    juce::String text =
        "adds " + juce::String(static_cast<int>(markers.size())) + " markers (" + shown.joinIntoString(", ");
    if (markers.size() > static_cast<size_t>(kMaxPreviewedMarkers))
        text += ", and " + juce::String(static_cast<int>(markers.size()) - kMaxPreviewedMarkers) + " more";
    return text + ")";
}

} // namespace synth
