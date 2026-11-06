#include "TimelineValidatorSections.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
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

TimelineValidationResult validatePlacement(const juce::var& v, double length, const std::set<int>& letters,
                                           std::set<std::int64_t>& seenIds, std::vector<Span>& spans) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return fail(Error::MalformedRoot, "A section placement is not an object.");
    if (!onlyKeys(*obj, {"id", "startBeat", "variation"}))
        return fail(Error::MalformedRoot, "A section placement carries only \"id\", \"startBeat\" and \"variation\".");
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
    const auto variation = obj->getProperty("variation");
    if (!variation.isVoid()) {
        const bool isInteger = variation.isInt() || variation.isInt64();
        const auto letter = isInteger ? static_cast<std::int64_t>(static_cast<juce::int64>(variation)) : -1;
        if (letter < 0 || letter >= TimelineDoc::kMaxVariations || letters.count(static_cast<int>(letter)) == 0)
            return fail(Error::MalformedRoot,
                        "Placement " + juce::String(id) + " plays a \"variation\" its section does not have.");
    }
    spans.push_back({start, start + length});
    return {};
}

// What the file's tracks say about ids: which tracks exist and which track owns each lane.
struct FileTracks {
    std::set<std::int64_t> tracks;
    std::map<std::int64_t, std::int64_t> laneOwner;
};

FileTracks indexFileTracks(const juce::DynamicObject& root) {
    FileTracks out;
    const auto list = root.getProperty("tracks");
    if (!list.isArray())
        return out;
    for (const auto& trackVar : *list.getArray()) {
        auto* track = trackVar.getDynamicObject();
        std::int64_t trackId = 0;
        if (track == nullptr || !readIdValue(track->getProperty("id"), trackId))
            continue;
        out.tracks.insert(trackId);
        const auto lanes = track->getProperty("lanes");
        if (lanes.isArray())
            for (const auto& laneVar : *lanes.getArray()) {
                std::int64_t laneId = 0;
                if (auto* lane = laneVar.getDynamicObject();
                    lane != nullptr && readIdValue(lane->getProperty("id"), laneId))
                    out.laneOwner[laneId] = trackId;
            }
    }
    return out;
}

bool readKeyId(const juce::String& key, std::int64_t& out) {
    out = static_cast<std::int64_t>(key.getLargeIntValue());
    return out > 0 && out <= TimelineDoc::kMaxIdValue && juce::String(static_cast<juce::int64>(out)) == key;
}

// Everything a stored content is checked against.
struct StoredContext {
    const FileTracks& file;
    const std::set<std::int64_t>* allowedTracks; // nullptr: any track of the file
    double length;
    const StoredClipValidator& validateClip;
    std::int64_t& totalNotes;
    int& storedClips;
};

TimelineValidationResult validateStoredClips(const juce::var& v, const StoredContext& ctx, const juce::String& text) {
    if (v.isVoid())
        return {};
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return fail(Error::MalformedRoot, text + " has a \"clips\" value that is not an object.");
    for (int i = 0; i < obj->getProperties().size(); ++i) {
        std::int64_t trackId = 0;
        const auto& list = obj->getProperties().getValueAt(i);
        if (!readKeyId(obj->getProperties().getName(i).toString(), trackId) || ctx.file.tracks.count(trackId) == 0 ||
            (ctx.allowedTracks != nullptr && ctx.allowedTracks->count(trackId) == 0))
            return fail(Error::MalformedRoot, text + " stores clips for a track it does not hold.");
        if (!list.isArray())
            return fail(Error::MalformedRoot, text + " stores a clip list that is not an array.");
        if (list.getArray()->size() > TimelineDoc::kMaxClipsPerTrack)
            return fail(Error::MalformedRoot, text + " stores more clips on one track than the limit of " +
                                                  juce::String(TimelineDoc::kMaxClipsPerTrack) + ".");
        ctx.storedClips += list.getArray()->size();
        if (ctx.storedClips > TimelineDoc::kMaxStoredVariationClips)
            return fail(Error::MalformedRoot, "Timeline stores more than " +
                                                  juce::String(TimelineDoc::kMaxStoredVariationClips) +
                                                  " clips in unplayed variations.");
        for (const auto& item : *list.getArray()) {
            if (const auto result = ctx.validateClip(item, text, ctx.totalNotes); !result.ok)
                return result;
            double start = 0.0;
            if (!readNumber(item.getDynamicObject()->getProperty("startBeat"), start) || !(start < ctx.length))
                return fail(Error::BeatOutOfBounds, text + " stores a clip that starts outside the section.");
        }
    }
    return {};
}

TimelineValidationResult validateStoredPoints(const juce::var& v, const StoredContext& ctx, const juce::String& text) {
    if (v.isVoid())
        return {};
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return fail(Error::MalformedRoot, text + " has a \"points\" value that is not an object.");
    for (int i = 0; i < obj->getProperties().size(); ++i) {
        std::int64_t laneId = 0;
        const auto& list = obj->getProperties().getValueAt(i);
        if (!readKeyId(obj->getProperties().getName(i).toString(), laneId))
            return fail(Error::MalformedRoot, text + " stores points under a key that is not a lane id.");
        const auto owner = ctx.file.laneOwner.find(laneId);
        if (owner == ctx.file.laneOwner.end() ||
            (ctx.allowedTracks != nullptr && ctx.allowedTracks->count(owner->second) == 0))
            return fail(Error::MalformedRoot, text + " stores points for a lane it does not hold.");
        if (!list.isArray())
            return fail(Error::MalformedRoot, text + " stores a point list that is not an array.");
        if (list.getArray()->size() > TimelineDoc::kMaxBreakpointsPerLane)
            return fail(Error::TooManyBreakpoints, text + " stores more points on one lane than the limit of " +
                                                       juce::String(TimelineDoc::kMaxBreakpointsPerLane) + ".");
        for (const auto& item : *list.getArray()) {
            auto* point = item.getDynamicObject();
            if (point == nullptr)
                return fail(Error::MalformedRoot, text + " stores a point that is not an object.");
            double beat = 0.0;
            double value = 0.0;
            double tension = 0.0;
            int curve = static_cast<int>(BreakpointCurve::Linear);
            const auto curveVar = point->getProperty("curve");
            const auto field = [&](const char* key, double& out) {
                const auto var = point->getProperty(key);
                return var.isVoid() || readNumber(var, out);
            };
            if (!field("beat", beat) || !field("value", value) || !field("tension", tension) ||
                (!curveVar.isVoid() && !(curveVar.isInt() || curveVar.isInt64())))
                return fail(Error::MalformedRoot, text + " stores a point with a non-numeric field.");
            if (!curveVar.isVoid())
                curve = static_cast<int>(static_cast<juce::int64>(curveVar));
            if (!inBounds(beat) || !(beat < ctx.length))
                return fail(Error::BeatOutOfBounds, text + " stores a point outside the section.");
            if (!std::isfinite(value) || !std::isfinite(tension) || tension < -1.0 || tension > 1.0 ||
                curve < static_cast<int>(BreakpointCurve::Hold) || curve > static_cast<int>(BreakpointCurve::Bezier))
                return fail(Error::MalformedRoot, text + " stores a point with a bad value, tension or curve.");
        }
    }
    return {};
}

TimelineValidationResult validateStoredContent(const juce::var& v, const StoredContext& ctx, const juce::String& text) {
    if (v.isVoid())
        return {};
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || !onlyKeys(*obj, {"clips", "points"}))
        return fail(Error::MalformedRoot,
                    text + " needs \"stored\" to be an object with only \"clips\" and \"points\".");
    if (const auto result = validateStoredClips(obj->getProperty("clips"), ctx, text); !result.ok)
        return result;
    return validateStoredPoints(obj->getProperty("points"), ctx, text);
}

TimelineValidationResult validateVariation(const juce::var& v, const FileTracks& file, double length,
                                           const StoredClipValidator& validateClip, std::int64_t& totalNotes,
                                           int& storedClips, std::set<int>& letters, const juce::String& sectionText) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || !onlyKeys(*obj, {"letter", "ownTracks", "stored"}))
        return fail(Error::MalformedRoot, sectionText + " has a variation that is not an object with only \"letter\", "
                                                        "\"ownTracks\" and \"stored\".");
    const auto letterVar = obj->getProperty("letter");
    const auto letter = (letterVar.isInt() || letterVar.isInt64())
                            ? static_cast<std::int64_t>(static_cast<juce::int64>(letterVar))
                            : std::int64_t{-1};
    if (letter < 1 || letter >= TimelineDoc::kMaxVariations)
        return fail(Error::MalformedRoot, sectionText + " has a variation \"letter\" outside 1..25 (A is implied).");
    if (!letters.insert(static_cast<int>(letter)).second)
        return fail(Error::MalformedRoot, sectionText + " names variation " + juce::String(letter) + " twice.");
    const juce::String text = sectionText + " variation " + juce::String(letter);

    std::set<std::int64_t> owned;
    const auto ownedVar = obj->getProperty("ownTracks");
    if (!ownedVar.isArray() || ownedVar.getArray()->size() > TimelineDoc::kMaxTracks)
        return fail(Error::MalformedRoot, text + " needs an \"ownTracks\" array.");
    for (const auto& item : *ownedVar.getArray()) {
        std::int64_t trackId = 0;
        if (!readIdValue(item, trackId) || file.tracks.count(trackId) == 0 || !owned.insert(trackId).second)
            return fail(Error::MalformedRoot, text + " owns a track the file lacks, or owns one twice.");
    }
    const StoredContext ctx{file, &owned, length, validateClip, totalNotes, storedClips};
    return validateStoredContent(obj->getProperty("stored"), ctx, text);
}

TimelineValidationResult validateSection(const juce::var& v, const FileTracks& file,
                                         const StoredClipValidator& validateClip, std::int64_t& totalNotes,
                                         int& storedClips, std::set<std::int64_t>& seenIds, std::vector<Span>& spans,
                                         int& placementCount) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return fail(Error::MalformedRoot, "A section entry is not an object.");
    if (!onlyKeys(*obj, {"id", "name", "colourArgb", "lengthBeats", "placements", "variations", "stored"}))
        return fail(Error::MalformedRoot, "A section carries only \"id\", \"name\", \"colourArgb\", \"lengthBeats\", "
                                          "\"placements\", \"variations\" and \"stored\".");
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

    std::set<int> letters{0};
    const auto variationList = obj->getProperty("variations");
    if (!variationList.isVoid()) {
        if (!variationList.isArray() || variationList.getArray()->size() >= TimelineDoc::kMaxVariations)
            return fail(Error::MalformedRoot, text + " needs \"variations\" to be an array of at most 25 entries.");
        for (const auto& item : *variationList.getArray())
            if (const auto result =
                    validateVariation(item, file, length, validateClip, totalNotes, storedClips, letters, text);
                !result.ok)
                return result;
    }
    const StoredContext aContext{file, nullptr, length, validateClip, totalNotes, storedClips};
    if (const auto result = validateStoredContent(obj->getProperty("stored"), aContext, text); !result.ok)
        return result;

    const auto list = obj->getProperty("placements");
    if (!list.isArray() || list.getArray()->isEmpty())
        return fail(Error::MalformedRoot, text + " needs a non-empty \"placements\" array.");
    for (const auto& item : *list.getArray()) {
        const auto result = validatePlacement(item, length, letters, seenIds, spans);
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

TimelineValidationResult validateTimelineSections(const juce::DynamicObject& root,
                                                  const StoredClipValidator& validateClip, std::int64_t& totalNotes) {
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
    int storedClips = 0;
    const auto file = indexFileTracks(root);
    for (const auto& item : *list) {
        const auto result =
            validateSection(item, file, validateClip, totalNotes, storedClips, seenIds, spans, placementCount);
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
