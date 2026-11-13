// Concern: sections - the "sections" part of the file format.
//
// Written only when there are sections: {"id","name","colourArgb","lengthBeats","placements":[{"id","startBeat"}]}
// plus "nextSectionId". A section with variations beyond A also writes "variations":
// [{"letter","ownTracks","stored"}] (letters other than 0; A is implied) and a placement that plays one writes
// "variation". "stored" is written only for a variation no placement plays, in the clip and breakpoint dialect of the
// tracks; A's own music, when no placement plays A, goes in the section's "stored". The reader is strict like the
// marker reader: any malformed section, a duplicate id (one id space for sections and placements), a section with no
// placement, an overlap, a bad or repeated letter, a placement naming an absent letter, a track the file lacks or an
// unknown key rejects the load.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace synth {

using namespace detail;

namespace {

bool readNumber(const juce::var& v, double& out) {
    if (!(v.isDouble() || v.isInt() || v.isInt64()))
        return false;
    out = static_cast<double>(v);
    return std::isfinite(out);
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

bool readPlacement(const juce::var& v, double length, const std::set<int>& letters, std::set<std::int64_t>& seenIds,
                   SectionId section, std::vector<SectionPlacement>& out) {
    auto* obj = v.getDynamicObject();
    std::int64_t id = 0;
    SectionPlacement placement;
    if (obj == nullptr || !onlyKeys(*obj, {"id", "startBeat", "variation"}) ||
        !readIdValue(obj->getProperty("id"), id) || !seenIds.insert(id).second ||
        !readNumber(obj->getProperty("startBeat"), placement.startBeat) ||
        !isFiniteAtOrAfterZero(placement.startBeat) || !std::isfinite(placement.startBeat + length))
        return false;
    const auto variationVar = obj->getProperty("variation");
    if (!variationVar.isVoid()) {
        if (!(variationVar.isInt() || variationVar.isInt64()))
            return false;
        const auto letter = static_cast<std::int64_t>(static_cast<juce::int64>(variationVar));
        if (letters.count(static_cast<int>(letter)) == 0 || letter < 0 || letter >= TimelineDoc::kMaxVariations)
            return false;
        placement.variation = static_cast<int>(letter);
    }
    placement.id = PlacementId{id};
    placement.section = section;
    out.push_back(placement);
    return true;
}

// Finds a track by id in the tracks being loaded; nullptr when the file has none.
const Track* findParsedTrack(const std::vector<Track>& tracks, std::int64_t id) {
    for (const auto& track : tracks)
        if (track.id.value == id)
            return &track;
    return nullptr;
}

bool readKeyId(const juce::String& key, std::int64_t& out) {
    out = static_cast<std::int64_t>(key.getLargeIntValue());
    return out > 0 && out <= TimelineDoc::kMaxIdValue && juce::String(static_cast<juce::int64>(out)) == key;
}

// Everything a stored content needs to know about the file it is read from.
struct StoredContext {
    const std::vector<Track>& tracks;
    const std::set<std::int64_t>* allowedTracks; // nullptr: any track of the file
    double length;
    int& storedClips; // running total across the document
};

bool readStoredClips(const juce::var& v, const StoredContext& ctx, PlacementContent& out) {
    if (v.isVoid())
        return true;
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;
    for (int i = 0; i < obj->getProperties().size(); ++i) {
        std::int64_t trackId = 0;
        const auto& list = obj->getProperties().getValueAt(i);
        if (!readKeyId(obj->getProperties().getName(i).toString(), trackId) ||
            findParsedTrack(ctx.tracks, trackId) == nullptr ||
            (ctx.allowedTracks != nullptr && ctx.allowedTracks->count(trackId) == 0) || !list.isArray() ||
            list.getArray()->size() > TimelineDoc::kMaxClipsPerTrack)
            return false;
        ctx.storedClips += list.getArray()->size();
        if (ctx.storedClips > TimelineDoc::kMaxStoredVariationClips)
            return false;
        std::vector<Clip> clips;
        for (const auto& item : *list.getArray()) {
            Clip clip;
            std::set<std::int64_t> clipIds, noteIds;
            if (!readClipVar(item, clip, clipIds, noteIds) || !(clip.startBeat < ctx.length))
                return false;
            clip.linkGroup = 0; // a stored copy belongs to no link group
            clips.push_back(std::move(clip));
        }
        std::stable_sort(clips.begin(), clips.end(), clipLess);
        if (!clips.empty())
            out.clips[trackId] = std::move(clips);
    }
    return true;
}

bool readStoredPoints(const juce::var& v, const StoredContext& ctx, PlacementContent& out) {
    if (v.isVoid())
        return true;
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;
    for (int i = 0; i < obj->getProperties().size(); ++i) {
        std::int64_t laneId = 0;
        const auto& list = obj->getProperties().getValueAt(i);
        if (!readKeyId(obj->getProperties().getName(i).toString(), laneId) || !list.isArray() ||
            list.getArray()->size() > TimelineDoc::kMaxBreakpointsPerLane)
            return false;
        const AutomationLane* found = nullptr;
        for (const auto& track : ctx.tracks)
            for (const auto& lane : track.lanes)
                if (lane.id.value == laneId &&
                    (ctx.allowedTracks == nullptr || ctx.allowedTracks->count(track.id.value)))
                    found = &lane;
        if (found == nullptr)
            return false;
        std::vector<AutomationLane::Breakpoint> points;
        for (const auto& item : *list.getArray()) {
            AutomationLane::Breakpoint point;
            if (!readBreakpointVar(item, found->range, point) || !(point.beat < ctx.length))
                return false;
            points.push_back(point);
        }
        std::stable_sort(
            points.begin(), points.end(),
            [](const AutomationLane::Breakpoint& a, const AutomationLane::Breakpoint& b) { return a.beat < b.beat; });
        std::vector<AutomationLane::Breakpoint> deduped; // the last of a same-beat run wins, like the lanes
        for (const auto& point : points) {
            if (!deduped.empty() && deduped.back().beat == point.beat)
                deduped.back() = point;
            else
                deduped.push_back(point);
        }
        if (!deduped.empty())
            out.points[laneId] = std::move(deduped);
    }
    return true;
}

bool readStoredContent(const juce::var& v, const StoredContext& ctx, PlacementContent& out) {
    if (v.isVoid())
        return true;
    auto* obj = v.getDynamicObject();
    return obj != nullptr && onlyKeys(*obj, {"clips", "points"}) &&
           readStoredClips(obj->getProperty("clips"), ctx, out) &&
           readStoredPoints(obj->getProperty("points"), ctx, out);
}

bool readVariation(const juce::var& v, const std::vector<Track>& tracks, double length, int& storedClips,
                   std::set<int>& letters, std::vector<SectionVariation>& out) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || !onlyKeys(*obj, {"letter", "ownTracks", "stored"}))
        return false;
    const auto letterVar = obj->getProperty("letter");
    if (!(letterVar.isInt() || letterVar.isInt64()))
        return false;
    const auto letter = static_cast<std::int64_t>(static_cast<juce::int64>(letterVar));
    if (letter < 1 || letter >= TimelineDoc::kMaxVariations || !letters.insert(static_cast<int>(letter)).second)
        return false;

    SectionVariation variation;
    variation.letter = static_cast<int>(letter);
    const auto& owned = obj->getProperty("ownTracks");
    if (!owned.isArray() || owned.getArray()->size() > TimelineDoc::kMaxTracks)
        return false;
    for (const auto& item : *owned.getArray()) {
        std::int64_t trackId = 0;
        if (!readIdValue(item, trackId) || findParsedTrack(tracks, trackId) == nullptr ||
            !variation.ownTracks.insert(trackId).second)
            return false;
    }
    const StoredContext ctx{tracks, &variation.ownTracks, length, storedClips};
    if (!readStoredContent(obj->getProperty("stored"), ctx, variation.stored))
        return false;
    out.push_back(std::move(variation));
    return true;
}

bool readSection(const juce::var& v, const std::vector<Track>& tracks, int& storedClips,
                 std::set<std::int64_t>& seenIds, std::vector<Section>& sectionsOut,
                 std::vector<SectionPlacement>& placementsOut) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr ||
        !onlyKeys(*obj, {"id", "name", "colourArgb", "lengthBeats", "placements", "variations", "stored"}))
        return false;

    Section section;
    std::int64_t id = 0;
    const auto nameVar = obj->getProperty("name");
    const auto colourVar = obj->getProperty("colourArgb");
    if (!readIdValue(obj->getProperty("id"), id) || !seenIds.insert(id).second || !nameVar.isString() ||
        !readNumber(obj->getProperty("lengthBeats"), section.lengthBeats) || !isFinitePositive(section.lengthBeats))
        return false;
    section.id = SectionId{id};
    section.name = nameVar.toString();
    if (section.name != section.name.trim() || section.name.isEmpty() ||
        section.name.length() > TimelineDoc::kMaxMarkerTextLength)
        return false;
    if (!colourVar.isVoid()) {
        if (!(colourVar.isInt() || colourVar.isInt64()))
            return false;
        const auto colour = static_cast<std::int64_t>(static_cast<juce::int64>(colourVar));
        if (colour < 0 || colour > 0xffffffffLL)
            return false;
        section.colourArgb = static_cast<juce::uint32>(colour);
    }

    std::set<int> letters{0};
    const auto& variationList = obj->getProperty("variations");
    if (!variationList.isVoid()) {
        if (!variationList.isArray() || variationList.getArray()->size() >= TimelineDoc::kMaxVariations)
            return false;
        for (const auto& item : *variationList.getArray())
            if (!readVariation(item, tracks, section.lengthBeats, storedClips, letters, section.variations))
                return false;
    }
    const StoredContext aContext{tracks, nullptr, section.lengthBeats, storedClips};
    if (!readStoredContent(obj->getProperty("stored"), aContext, section.variations.front().stored))
        return false;

    const auto& list = obj->getProperty("placements");
    if (!list.isArray() || list.getArray()->isEmpty())
        return false;
    for (const auto& item : *list.getArray())
        if (!readPlacement(item, section.lengthBeats, letters, seenIds, section.id, placementsOut))
            return false;
    sectionsOut.push_back(std::move(section));
    return true;
}

} // namespace

namespace {

// {"clips": {"<trackId>": [clip...]}, "points": {"<laneId>": [breakpoint...]}}
juce::var writeStoredContent(const PlacementContent& content) {
    juce::DynamicObject::Ptr clipsObj = new juce::DynamicObject();
    for (const auto& [trackId, clips] : content.clips) {
        juce::Array<juce::var> list;
        for (const auto& clip : clips)
            list.add(writeClipVar(clip));
        clipsObj->setProperty(juce::Identifier(juce::String(static_cast<juce::int64>(trackId))), list);
    }
    juce::DynamicObject::Ptr pointsObj = new juce::DynamicObject();
    for (const auto& [laneId, points] : content.points) {
        juce::Array<juce::var> list;
        for (const auto& point : points)
            list.add(writeBreakpointVar(point));
        pointsObj->setProperty(juce::Identifier(juce::String(static_cast<juce::int64>(laneId))), list);
    }
    juce::DynamicObject::Ptr out = new juce::DynamicObject();
    out->setProperty("clips", juce::var(clipsObj.get()));
    out->setProperty("points", juce::var(pointsObj.get()));
    return juce::var(out.get());
}

} // namespace

void TimelineDoc::writeSections(juce::DynamicObject& root) const {
    if (sections.empty())
        return;
    juce::Array<juce::var> sectionVars;
    for (const auto& section : sections) {
        juce::DynamicObject::Ptr s = new juce::DynamicObject();
        s->setProperty("id", static_cast<juce::int64>(section.id.value));
        s->setProperty("name", section.name);
        s->setProperty("colourArgb", static_cast<juce::int64>(section.colourArgb));
        s->setProperty("lengthBeats", section.lengthBeats);
        std::set<int> played;
        juce::Array<juce::var> placementVars;
        for (const auto& placement : placements) {
            if (placement.section != section.id)
                continue;
            played.insert(placement.variation);
            juce::DynamicObject::Ptr p = new juce::DynamicObject();
            p->setProperty("id", static_cast<juce::int64>(placement.id.value));
            p->setProperty("startBeat", placement.startBeat);
            if (placement.variation != 0)
                p->setProperty("variation", placement.variation);
            placementVars.add(juce::var(p.get()));
        }
        s->setProperty("placements", placementVars);

        juce::Array<juce::var> variationVars;
        for (const auto& variation : section.variations) {
            if (variation.letter == 0) {
                if (played.count(0) == 0)
                    s->setProperty("stored", writeStoredContent(variation.stored));
                continue;
            }
            juce::DynamicObject::Ptr v = new juce::DynamicObject();
            v->setProperty("letter", variation.letter);
            juce::Array<juce::var> owned;
            for (const auto trackId : variation.ownTracks)
                owned.add(static_cast<juce::int64>(trackId));
            v->setProperty("ownTracks", owned);
            if (played.count(variation.letter) == 0)
                v->setProperty("stored", writeStoredContent(variation.stored));
            variationVars.add(juce::var(v.get()));
        }
        if (!variationVars.isEmpty())
            s->setProperty("variations", variationVars);
        sectionVars.add(juce::var(s.get()));
    }
    root.setProperty("sections", sectionVars);
    root.setProperty("nextSectionId", static_cast<juce::int64>(nextSectionId));
}

bool TimelineDoc::readSections(const juce::DynamicObject& root, const std::vector<Track>& parsedTracks,
                               std::vector<Section>& sectionsOut, std::vector<SectionPlacement>& placementsOut,
                               std::int64_t& nextIdOut) {
    nextIdOut = 1;
    const auto counterVar = root.getProperty("nextSectionId");
    if (!counterVar.isVoid()) {
        if (!readIdValue(counterVar, nextIdOut))
            return false;
    }
    const auto listVar = root.getProperty("sections");
    if (listVar.isVoid())
        return true;
    if (!listVar.isArray())
        return false;
    const auto* list = listVar.getArray();
    if (list->size() > kMaxSections)
        return false;

    std::set<std::int64_t> seenIds;
    int storedClips = 0;
    for (const auto& item : *list)
        if (!readSection(item, parsedTracks, storedClips, seenIds, sectionsOut, placementsOut))
            return false;
    if (static_cast<int>(placementsOut.size()) > kMaxPlacements)
        return false;

    std::stable_sort(placementsOut.begin(), placementsOut.end(),
                     [](const SectionPlacement& a, const SectionPlacement& b) {
                         return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.id.value < b.id.value;
                     });
    const auto lengthOf = [&](SectionId id) {
        for (const auto& s : sectionsOut)
            if (s.id == id)
                return s.lengthBeats;
        return 0.0;
    };
    for (size_t i = 1; i < placementsOut.size(); ++i)
        if (placementsOut[i].startBeat < placementsOut[i - 1].startBeat + lengthOf(placementsOut[i - 1].section))
            return false;

    for (const auto& id : seenIds)
        nextIdOut = std::max(nextIdOut, id + 1);
    return true;
}

} // namespace synth
