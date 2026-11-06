// Concern: sections - variations (A, B, C...): adding, switching and removing them, and keeping their stored music.
//
// A placement plays one variation. Letter 0 (A) owns every track; any other variation owns the tracks in ownTracks
// and shares the rest with A (variationKey, TimelineDocSectionSync.cpp). Each variation keeps its music in `stored`
// so it survives while no placement plays it; the sync refreshes it from a placement that plays it after every
// change. Every op validates first and changes nothing when it rejects.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>

namespace synth {

using namespace detail;

namespace {

bool hasLetter(const Section& section, int letter) {
    return std::any_of(section.variations.begin(), section.variations.end(),
                       [&](const SectionVariation& v) { return v.letter == letter; });
}

int lowestUnusedLetter(const Section& section) {
    for (int letter = 0; letter < TimelineDoc::kMaxVariations; ++letter)
        if (!hasLetter(section, letter))
            return letter;
    return -1;
}

// A stored copy belongs to no link group.
void dropLinkGroups(PlacementContent& content, const Track& track) {
    const auto it = content.clips.find(track.id.value);
    if (it != content.clips.end())
        for (auto& clip : it->second)
            clip.linkGroup = 0;
}

bool isPlayed(const std::vector<SectionPlacement>& placements, SectionId section, int letter) {
    return std::any_of(placements.begin(), placements.end(),
                       [&](const SectionPlacement& p) { return p.section == section && p.variation == letter; });
}

} // namespace

// ------------------------------------------------------------------- lookups --

const SectionVariation* TimelineDoc::getVariation(SectionId id, int letter) const {
    const auto* section = getSection(id);
    if (section == nullptr)
        return nullptr;
    for (const auto& variation : section->variations)
        if (variation.letter == letter)
            return &variation;
    return nullptr;
}

void TimelineDoc::forEachStoredVariationClip(const std::function<void(const Clip&)>& visit) const {
    for (const auto& section : sections)
        for (const auto& variation : section.variations) {
            if (isPlayed(placements, section.id, variation.letter))
                continue;
            for (const auto& entry : variation.stored.clips)
                for (const auto& clip : entry.second)
                    visit(clip);
        }
}

bool TimelineDoc::remapStoredAssetRefs(const std::map<juce::String, juce::String>& rewrite) {
    const auto remap = [&](juce::String& ref) {
        const auto it = rewrite.find(ref);
        if (it == rewrite.end())
            return false;
        ref = it->second;
        return true;
    };
    const auto visitUnplayed = [&](const std::function<void(Clip&)>& visit) {
        for (auto& section : sections)
            for (auto& variation : section.variations)
                if (!isPlayed(placements, section.id, variation.letter))
                    for (auto& entry : variation.stored.clips)
                        for (auto& clip : entry.second)
                            visit(clip);
    };
    bool any = false;
    visitUnplayed([&](Clip& clip) {
        any |= rewrite.count(clip.assetRef) != 0 ||
               (clip.stretchOrigin && rewrite.count(clip.stretchOrigin->assetRef) != 0);
    });
    if (!any)
        return false;
    return applyMutation([&] {
        visitUnplayed([&](Clip& clip) {
            remap(clip.assetRef);
            if (clip.stretchOrigin)
                remap(clip.stretchOrigin->assetRef);
        });
        return true;
    });
}

// ------------------------------------------------------------------ the music --

// Re-captures each variation's stored music from a placement that plays it; a track no placement plays keeps what it
// had.
void TimelineDoc::refreshStoredVariations(Section& section) {
    for (auto& variation : section.variations)
        for (const auto& track : tracks) {
            if (variation.letter != 0 && variation.ownTracks.count(track.id.value) == 0)
                continue;
            for (const auto& placement : placements) {
                if (placement.section != section.id ||
                    variationKey(section, placement.variation, track.id.value) != variation.letter)
                    continue;
                PlacementContent live;
                appendTrackContent(track, placement.startBeat, section.lengthBeats, live);
                dropLinkGroups(live, track);
                copyTrackSlice(track, live, variation.stored);
                break;
            }
        }
}

// The music a placement playing `letter` holds, track by track: a placement that plays the same copy of the track,
// else the stored music.
PlacementContent TimelineDoc::variationContent(const Section& section, int letter) const {
    PlacementContent out;
    for (const auto& track : tracks) {
        const int key = variationKey(section, letter, track.id.value);
        const SectionPlacement* member = nullptr;
        for (const auto& placement : placements)
            if (placement.section == section.id && variationKey(section, placement.variation, track.id.value) == key) {
                member = &placement;
                break;
            }
        if (member != nullptr) {
            appendTrackContent(track, member->startBeat, section.lengthBeats, out);
            continue;
        }
        for (const auto& variation : section.variations)
            if (variation.letter == key)
                copyTrackSlice(track, variation.stored, out);
    }
    return out;
}

// Raw edit for a caller inside a mutation: rewrites the placement's music on each track whose copy changes.
bool TimelineDoc::switchPlacementVariation(PlacementId id, int letter) {
    const auto placementIt =
        std::find_if(placements.begin(), placements.end(), [&](const SectionPlacement& p) { return p.id == id; });
    const auto sectionIt = placementIt == placements.end()
                               ? sections.end()
                               : std::find_if(sections.begin(), sections.end(),
                                              [&](const Section& s) { return s.id == placementIt->section; });
    if (sectionIt == sections.end() || placementIt->variation == letter || !hasLetter(*sectionIt, letter))
        return false;

    refreshStoredVariations(*sectionIt); // the variation being left may now be played by nobody
    const auto target = variationContent(*sectionIt, letter);
    const double start = placementIt->startBeat;
    const double length = sectionIt->lengthBeats;
    const auto cached = sectionContentCache.find(id.value);
    for (auto& track : tracks) {
        if (variationKey(*sectionIt, placementIt->variation, track.id.value) ==
            variationKey(*sectionIt, letter, track.id.value))
            continue;
        stampTrack(track, target, start, length);
        if (cached != sectionContentCache.end()) { // the switch is not an edit of the placement
            PlacementContent live;
            appendTrackContent(track, start, length, live);
            copyTrackSlice(track, live, cached->second);
        }
    }
    placementIt->variation = letter;
    storedStale.insert(sectionIt->id.value);
    return true;
}

// Raw edit for a caller inside a mutation: adds a variation holding a copy of the placement's own tracks, which the
// placement then plays. Returns its letter.
int TimelineDoc::newVariation(const SectionPlacement& placement, const std::set<std::int64_t>& ownTracks) {
    const auto sectionIt =
        std::find_if(sections.begin(), sections.end(), [&](const Section& s) { return s.id == placement.section; });
    const int letter = lowestUnusedLetter(*sectionIt);
    SectionVariation variation;
    variation.letter = letter;
    variation.ownTracks = ownTracks;
    for (const auto& track : tracks)
        if (ownTracks.count(track.id.value) != 0) {
            PlacementContent live;
            appendTrackContent(track, placement.startBeat, sectionIt->lengthBeats, live);
            dropLinkGroups(live, track);
            copyTrackSlice(track, live, variation.stored);
        }
    sectionIt->variations.push_back(std::move(variation));
    for (auto& p : placements)
        if (p.id == placement.id)
            p.variation = letter;
    storedStale.insert(sectionIt->id.value);
    return letter;
}

// ------------------------------------------------------------------------ ops --

int TimelineDoc::addVariation(PlacementId id) {
    const auto* placement = getPlacement(id);
    const auto* section = placement != nullptr ? getSection(placement->section) : nullptr;
    if (section == nullptr || lowestUnusedLetter(*section) < 0)
        return -1;
    std::set<std::int64_t> own;
    for (const auto trackId : trackIds(false))
        own.insert(trackId.value);
    return applyMutation([&] { return newVariation(*getPlacement(id), own); });
}

int TimelineDoc::addVariationForTrack(ClipId clip) {
    Track* owner = nullptr;
    if (findClip(clip, &owner) == nullptr)
        return -1;
    const auto* placement = placementForClip(clip);
    const auto* section = placement != nullptr ? getSection(placement->section) : nullptr;
    if (section == nullptr || lowestUnusedLetter(*section) < 0)
        return -1;
    std::set<std::int64_t> own;
    if (const auto* current = getVariation(section->id, placement->variation); current != nullptr)
        if (current->letter != 0)
            own = current->ownTracks;
    own.insert(owner->id.value);
    const auto placementId = placement->id;
    return applyMutation([&] { return newVariation(*getPlacement(placementId), own); });
}

bool TimelineDoc::setPlacementVariation(PlacementId id, int letter) {
    const auto* placement = getPlacement(id);
    if (placement == nullptr || placement->variation == letter || getVariation(placement->section, letter) == nullptr)
        return false;
    return applyMutation([&] { return switchPlacementVariation(id, letter); });
}

bool TimelineDoc::playVariationEverywhere(SectionId id, int letter) {
    if (getVariation(id, letter) == nullptr)
        return false;
    const auto ids = placementsOf(id);
    const bool anyToSwitch =
        std::any_of(ids.begin(), ids.end(), [&](PlacementId p) { return getPlacement(p)->variation != letter; });
    if (!anyToSwitch)
        return false;
    return applyMutation([&] {
        for (const auto placement : ids)
            switchPlacementVariation(placement, letter);
        return true;
    });
}

bool TimelineDoc::removeVariation(SectionId id, int letter) {
    if (letter == 0 || getVariation(id, letter) == nullptr)
        return false;
    const auto ids = placementsOf(id);
    return applyMutation([&] {
        for (const auto placement : ids)
            if (getPlacement(placement)->variation == letter)
                switchPlacementVariation(placement, 0);
        for (auto& section : sections)
            if (section.id == id)
                section.variations.erase(std::remove_if(section.variations.begin(), section.variations.end(),
                                                        [&](const SectionVariation& v) { return v.letter == letter; }),
                                         section.variations.end());
        storedStale.insert(id.value);
        return true;
    });
}

// ------------------------------------------------------------ removed tracks --

void TimelineDoc::forgetTrackInVariations(TrackId id) {
    const auto* track = findTrack(id);
    for (auto& section : sections)
        for (auto& variation : section.variations) {
            variation.ownTracks.erase(id.value);
            variation.stored.clips.erase(id.value);
            if (track != nullptr)
                for (const auto& lane : track->lanes)
                    variation.stored.points.erase(lane.id.value);
        }
}

void TimelineDoc::forgetLaneInVariations(LaneId id) {
    for (auto& section : sections)
        for (auto& variation : section.variations)
            variation.stored.points.erase(id.value);
}

} // namespace synth
