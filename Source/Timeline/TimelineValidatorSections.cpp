#include "TimelineValidatorSections.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

namespace synth {

namespace {

using Error = TimelineValidationError;

TimelineValidationResult fail(Error error, const juce::String& message) { return {false, error, message}; }

bool readNumber(const juce::var& v, double& out) {
    if (!(v.isDouble() || v.isInt() || v.isInt64()))
        return false;
    out = static_cast<double>(v);
    return true;
}

bool readIdValue(const juce::var& v, std::int64_t& out) {
    if (!(v.isInt() || v.isInt64()))
        return false;
    out = static_cast<std::int64_t>(static_cast<juce::int64>(v));
    return out > 0 && out <= TimelineDoc::kMaxIdValue;
}

bool onlyKeys(const juce::DynamicObject& obj, std::initializer_list<const char*> allowed) {
    for (int i = 0; i < obj.getProperties().size(); ++i) {
        const auto key = obj.getProperties().getName(i).toString();
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* k) { return key == k; }))
            return false;
    }
    return true;
}

bool inBounds(double beat) { return std::isfinite(beat) && beat >= 0.0 && beat <= kMaxPpqUntrusted; }

struct Span {
    double start;
    double end;
};

TimelineValidationResult validatePlacement(const juce::var& v, double length, std::set<std::int64_t>& seenIds,
                                           std::vector<Span>& spans) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return fail(Error::MalformedRoot, "A section placement is not an object.");
    if (!onlyKeys(*obj, {"id", "startBeat"}))
        return fail(Error::MalformedRoot, "A section placement carries only \"id\" and \"startBeat\".");
    std::int64_t id = 0;
    if (!readIdValue(obj->getProperty("id"), id))
        return fail(Error::MalformedRoot, "A section placement is missing a positive integer \"id\".");
    if (!seenIds.insert(id).second)
        return fail(Error::MalformedRoot, "Duplicate section or placement id " + juce::String(id) + ".");
    double start = 0.0;
    if (!readNumber(obj->getProperty("startBeat"), start))
        return fail(Error::MalformedRoot, "Placement " + juce::String(id) + " has no numeric \"startBeat\".");
    if (!inBounds(start) || !inBounds(start + length))
        return fail(Error::BeatOutOfBounds, "Placement " + juce::String(id) + " sits at beat " + juce::String(start) +
                                                ", which is not a finite beat inside the allowed range.");
    spans.push_back({start, start + length});
    return {};
}

TimelineValidationResult validateSection(const juce::var& v, std::set<std::int64_t>& seenIds, std::vector<Span>& spans,
                                         int& placementCount) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return fail(Error::MalformedRoot, "A section entry is not an object.");
    if (!onlyKeys(*obj, {"id", "name", "colourArgb", "lengthBeats", "placements"}))
        return fail(Error::MalformedRoot, "A section carries only \"id\", \"name\", \"colourArgb\", \"lengthBeats\" "
                                          "and \"placements\".");
    std::int64_t id = 0;
    if (!readIdValue(obj->getProperty("id"), id))
        return fail(Error::MalformedRoot, "A section is missing a positive integer \"id\".");
    if (!seenIds.insert(id).second)
        return fail(Error::MalformedRoot, "Duplicate section or placement id " + juce::String(id) + ".");
    const juce::String text = "Section " + juce::String(id);

    const auto nameVar = obj->getProperty("name");
    if (!nameVar.isString())
        return fail(Error::MalformedRoot, text + " needs a string \"name\".");
    const auto name = nameVar.toString();
    if (name.isEmpty() || name != name.trim())
        return fail(Error::MalformedRoot, text + " needs a non-empty \"name\" with no leading or trailing spaces.");
    if (name.length() > TimelineDoc::kMaxMarkerTextLength)
        return fail(Error::MarkerTextTooLong, text + " has a name over the limit of " +
                                                  juce::String(TimelineDoc::kMaxMarkerTextLength) + " characters.");

    const auto colour = obj->getProperty("colourArgb");
    if (!colour.isVoid()) {
        if (!(colour.isInt() || colour.isInt64()))
            return fail(Error::MalformedRoot, text + " has a non-integer \"colourArgb\".");
        const auto value = static_cast<std::int64_t>(static_cast<juce::int64>(colour));
        if (value < 0 || value > 0xffffffffLL)
            return fail(Error::MalformedRoot, text + " has a \"colourArgb\" outside 0..0xFFFFFFFF.");
    }

    double length = 0.0;
    if (!readNumber(obj->getProperty("lengthBeats"), length) || !(length > 0.0) || !inBounds(length))
        return fail(Error::BeatOutOfBounds, text + " needs a \"lengthBeats\" that is finite and above 0.");

    const auto list = obj->getProperty("placements");
    if (!list.isArray() || list.getArray()->isEmpty())
        return fail(Error::MalformedRoot, text + " needs a non-empty \"placements\" array.");
    for (const auto& item : *list.getArray()) {
        const auto result = validatePlacement(item, length, seenIds, spans);
        if (!result.ok)
            return result;
        ++placementCount;
        if (placementCount > TimelineDoc::kMaxPlacements)
            return fail(Error::TooManySections,
                        "Timeline has more than " + juce::String(TimelineDoc::kMaxPlacements) + " section placements.");
    }
    return {};
}

} // namespace

TimelineValidationResult validateTimelineSections(const juce::DynamicObject& root) {
    const auto counter = root.getProperty("nextSectionId");
    std::int64_t counterValue = 0;
    if (!counter.isVoid() && !readIdValue(counter, counterValue))
        return fail(Error::MalformedRoot, "\"nextSectionId\" must be a positive integer.");

    const auto listVar = root.getProperty("sections");
    if (listVar.isVoid())
        return {};
    if (!listVar.isArray())
        return fail(Error::MalformedRoot, "Timeline \"sections\" must be an array.");
    const auto* list = listVar.getArray();
    if (list->size() > TimelineDoc::kMaxSections)
        return fail(Error::TooManySections, "Timeline has " + juce::String(list->size()) +
                                                " sections, exceeding the limit of " +
                                                juce::String(TimelineDoc::kMaxSections) + ".");

    std::set<std::int64_t> seenIds;
    std::vector<Span> spans;
    int placementCount = 0;
    for (const auto& item : *list) {
        const auto result = validateSection(item, seenIds, spans, placementCount);
        if (!result.ok)
            return result;
    }

    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
    for (size_t i = 1; i < spans.size(); ++i)
        if (spans[i].start < spans[i - 1].end)
            return fail(Error::SectionOverlap, "Two section placements overlap around beat " +
                                                   juce::String(spans[i].start) +
                                                   ". Placements may touch but not overlap.");
    return {};
}

} // namespace synth
