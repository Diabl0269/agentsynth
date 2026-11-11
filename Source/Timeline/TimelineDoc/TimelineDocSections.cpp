// Concern: sections - the editing operations (make, place, move, remove, rename, time edits).
//
// Compound ops hold a SectionEditScope so an intermediate state never stamps one placement onto another; the scope
// runs one sync when the op ends, and only if that sync would change something.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace synth {

using namespace detail;

namespace {

bool isValidSectionName(const juce::String& name) noexcept {
    return name.trim().isNotEmpty() && name.trim().length() <= TimelineDoc::kMaxMarkerTextLength;
}

bool isValidSpan(double startBeat, double endBeat) noexcept {
    return isFiniteAtOrAfterZero(startBeat) && std::isfinite(endBeat) && endBeat > startBeat;
}

} // namespace

void TimelineDoc::insertPlacementSorted(const SectionPlacement& placement) {
    const auto pos = std::lower_bound(placements.begin(), placements.end(), placement,
                                      [](const SectionPlacement& a, const SectionPlacement& b) {
                                          if (a.startBeat != b.startBeat)
                                              return a.startBeat < b.startBeat;
                                          return a.id.value < b.id.value;
                                      });
    placements.insert(pos, placement);
}

void TimelineDoc::removePlacementsOverlapping(double startBeat, double endBeat, PlacementId except) {
    placements.erase(std::remove_if(placements.begin(), placements.end(),
                                    [&](const SectionPlacement& p) {
                                        return p.id != except && p.startBeat < endBeat &&
                                               startBeat < p.startBeat + sectionLength(p.section);
                                    }),
                     placements.end());
}

// ---------------------------------------------------------------- time edits --

bool TimelineDoc::insertTime(double atBeat, double lengthBeats, double secondsPerBeat) {
    if (!isFiniteAtOrAfterZero(atBeat) || !isFinitePositive(lengthBeats) || !isFinitePositive(secondsPerBeat))
        return false;

    SectionEditScope scope(*this);
    // A placement the new time lands inside would be cut in two; its music stays, it stops being a placement.
    const auto* inside = placementAt(atBeat);
    if (inside != nullptr && inside->startBeat < atBeat) {
        const auto id = inside->id;
        applyMutation([&] {
            placements.erase(std::remove_if(placements.begin(), placements.end(),
                                            [&](const SectionPlacement& p) { return p.id == id; }),
                             placements.end());
        });
    }

    for (const auto trackId : trackIds(true)) {
        std::vector<ClipId> crossing;
        if (const auto* track = findTrack(trackId))
            for (const auto& clip : track->clips)
                if (clip.startBeat < atBeat && clip.startBeat + clip.lengthBeats > atBeat)
                    crossing.push_back(clip.id);
        for (const auto id : crossing)
            if (const auto* clip = getClip(id))
                splitClipAtTempo(id, atBeat - clip->startBeat, secondsPerBeat);

        std::vector<std::pair<ClipId, double>> later;
        if (const auto* track = findTrack(trackId))
            for (const auto& clip : track->clips)
                if (clip.startBeat >= atBeat)
                    later.emplace_back(clip.id, clip.startBeat);
        for (auto it = later.rbegin(); it != later.rend(); ++it)
            moveClip(it->first, it->second + lengthBeats);
    }

    applyMutation([&] {
        for (auto& track : tracks)
            for (auto& lane : track.lanes)
                for (auto& point : lane.points)
                    if (point.beat >= atBeat)
                        point.beat += lengthBeats;
        for (auto& marker : markers)
            if (marker.beat >= atBeat)
                marker.beat += lengthBeats;
        for (auto& placement : placements)
            if (placement.startBeat >= atBeat)
                placement.startBeat += lengthBeats;
    });
    return true;
}

bool TimelineDoc::deleteTime(double startBeat, double endBeat, double secondsPerBeat) {
    if (!isValidSpan(startBeat, endBeat) || !isFinitePositive(secondsPerBeat))
        return false;

    SectionEditScope scope(*this);
    const double gap = endBeat - startBeat;
    applyMutation([&] { removePlacementsOverlapping(startBeat, endBeat, {}); });
    deleteRange(trackIds(true), startBeat, endBeat, true, secondsPerBeat);

    applyMutation([&] {
        for (auto& track : tracks)
            for (auto& lane : track.lanes) {
                auto& pts = lane.points;
                pts.erase(std::remove_if(pts.begin(), pts.end(),
                                         [&](const AutomationLane::Breakpoint& p) {
                                             return p.beat >= startBeat && p.beat < endBeat;
                                         }),
                          pts.end());
                for (auto& point : pts)
                    if (point.beat >= endBeat)
                        point.beat -= gap;
            }
        markers.erase(std::remove_if(markers.begin(), markers.end(),
                                     [&](const Marker& m) { return m.beat >= startBeat && m.beat < endBeat; }),
                      markers.end());
        for (auto& marker : markers)
            if (marker.beat >= endBeat)
                marker.beat -= gap;
        for (auto& placement : placements)
            if (placement.startBeat >= endBeat)
                placement.startBeat -= gap;
    });
    return true;
}

// ------------------------------------------------------------------ sections --

SectionId TimelineDoc::makeSection(double startBeat, double endBeat, const juce::String& name, juce::uint32 colourArgb,
                                   double secondsPerBeat) {
    if (!isValidSpan(startBeat, endBeat) || !isFinitePositive(secondsPerBeat) || !isValidSectionName(name))
        return {};
    if (static_cast<int>(sections.size()) >= kMaxSections || static_cast<int>(placements.size()) >= kMaxPlacements)
        return {};
    for (const auto& placement : placements)
        if (startBeat < placement.startBeat + sectionLength(placement.section) && placement.startBeat < endBeat)
            return {};

    SectionEditScope scope(*this);
    splitClipsAtRangeEdges(trackIds(false), startBeat, endBeat, secondsPerBeat);
    return applyMutation([&] {
        Section section;
        section.id = SectionId{nextSectionId++};
        section.name = name.trim();
        section.colourArgb = colourArgb;
        section.lengthBeats = endBeat - startBeat;
        sections.push_back(section);
        insertPlacementSorted({PlacementId{nextSectionId++}, section.id, startBeat});
        return section.id;
    });
}

PlacementId TimelineDoc::placeSection(SectionId id, double atBeat, SectionDropMode mode, double secondsPerBeat,
                                      int letter) {
    const auto ids = placementsOf(id);
    if (ids.empty() || !isFiniteAtOrAfterZero(atBeat) || !isFinitePositive(secondsPerBeat) ||
        static_cast<int>(placements.size()) >= kMaxPlacements || getVariation(id, letter) == nullptr)
        return {};

    const double length = sectionLength(id);
    const auto content = variationContent(*getSection(id), letter);

    SectionEditScope scope(*this);
    if (mode == SectionDropMode::MakeRoom) {
        insertTime(atBeat, length, secondsPerBeat);
    } else {
        applyMutation([&] { removePlacementsOverlapping(atBeat, atBeat + length, {}); });
        deleteRange(trackIds(true), atBeat, atBeat + length, false, secondsPerBeat);
        applyMutation([&] { eraseLanePoints(atBeat, atBeat + length); });
    }
    return applyMutation([&] {
        const PlacementId placementId{nextSectionId++};
        insertPlacementSorted({placementId, id, atBeat, letter});
        stampContent(content, atBeat, length);
        return placementId;
    });
}

bool TimelineDoc::movePlacement(PlacementId id, double newStartBeat, SectionDropMode mode, double secondsPerBeat) {
    const auto* found = getPlacement(id);
    if (found == nullptr || !isFiniteAtOrAfterZero(newStartBeat) || !isFinitePositive(secondsPerBeat))
        return false;
    if (found->startBeat == newStartBeat)
        return true;

    const SectionPlacement old = *found;
    const double length = sectionLength(old.section);
    const double oldEnd = old.startBeat + length;
    const auto content = contentOf(old.startBeat, length);

    // `newStartBeat` is the start the block has AFTER the move, in the layout the user sees while dragging (the strip's
    // sectionstrip::planSectionDrop): with make-room its old bars close up first, so a later target is already counted
    // without them.
    SectionEditScope scope(*this);
    const double target = newStartBeat;
    if (mode == SectionDropMode::MakeRoom) {
        deleteTime(old.startBeat, oldEnd, secondsPerBeat); // takes the placement with it; the section stays alive
        insertTime(target, length, secondsPerBeat);
    } else {
        deleteRange(trackIds(true), old.startBeat, oldEnd, false, secondsPerBeat);
        applyMutation([&] {
            eraseLanePoints(old.startBeat, oldEnd);
            placements.erase(std::remove_if(placements.begin(), placements.end(),
                                            [&](const SectionPlacement& p) { return p.id == id; }),
                             placements.end());
            removePlacementsOverlapping(target, target + length, {});
        });
        deleteRange(trackIds(true), target, target + length, false, secondsPerBeat);
        applyMutation([&] { eraseLanePoints(target, target + length); });
    }
    applyMutation([&] {
        insertPlacementSorted({id, old.section, target, old.variation});
        stampContent(content, target, length);
    });
    return true;
}

bool TimelineDoc::removePlacementKeepMusic(PlacementId id) {
    if (getPlacement(id) == nullptr)
        return false;
    return applyMutation([&] {
        placements.erase(
            std::remove_if(placements.begin(), placements.end(), [&](const SectionPlacement& p) { return p.id == id; }),
            placements.end());
        return true;
    });
}

bool TimelineDoc::deletePlacementAndBars(PlacementId id, double secondsPerBeat) {
    const auto* placement = getPlacement(id);
    if (placement == nullptr || !isFinitePositive(secondsPerBeat))
        return false;
    const double start = placement->startBeat;
    return deleteTime(start, start + sectionLength(placement->section), secondsPerBeat);
}

SectionId TimelineDoc::makePlacementUnique(PlacementId id, const juce::String& newName, juce::uint32 newColourArgb) {
    const auto* placement = getPlacement(id);
    if (placement == nullptr || !isValidSectionName(newName) || static_cast<int>(sections.size()) >= kMaxSections)
        return {};
    if (placementsOf(placement->section).size() < 2)
        return {};
    return applyMutation([&] {
        Section section;
        section.id = SectionId{nextSectionId++};
        section.name = newName.trim();
        section.colourArgb = newColourArgb;
        section.lengthBeats = sectionLength(placement->section);
        sections.push_back(section);
        storedStale.insert(section.id.value); // its A is what the placement plays now
        for (auto& p : placements)
            if (p.id == id) {
                p.section = section.id;
                p.variation = 0;
            }
        return section.id;
    });
}

bool TimelineDoc::renameSection(SectionId id, const juce::String& name) {
    auto it = std::find_if(sections.begin(), sections.end(), [&](const Section& s) { return s.id == id; });
    if (it == sections.end() || !isValidSectionName(name))
        return false;
    if (it->name == name.trim())
        return true;
    return applyMutation([&] {
        it->name = name.trim();
        return true;
    });
}

bool TimelineDoc::setSectionColour(SectionId id, juce::uint32 colourArgb) {
    auto it = std::find_if(sections.begin(), sections.end(), [&](const Section& s) { return s.id == id; });
    if (it == sections.end())
        return false;
    if (it->colourArgb == colourArgb)
        return true;
    return applyMutation([&] {
        it->colourArgb = colourArgb;
        return true;
    });
}

int TimelineDoc::makeSectionsFromMarkers(double endBeat, const std::function<juce::uint32(int index)>& colourFor,
                                         double secondsPerBeat) {
    if (!std::isfinite(endBeat) || !isFinitePositive(secondsPerBeat))
        return 0;

    struct Span {
        double start;
        double end;
        juce::String text;
    };
    std::vector<Span> spans;
    for (size_t i = 0; i < markers.size(); ++i) {
        const double end = i + 1 < markers.size() ? markers[i + 1].beat : endBeat;
        const double start = markers[i].beat;
        if (end - start <= 0.0)
            continue;
        bool overlaps = false;
        for (const auto& placement : placements)
            overlaps |= start < placement.startBeat + sectionLength(placement.section) && placement.startBeat < end;
        if (!overlaps)
            spans.push_back({start, end, markers[i].text.trim()});
    }

    SectionEditScope scope(*this);
    for (const auto& span : spans)
        splitClipsAtRangeEdges(trackIds(false), span.start, span.end, secondsPerBeat);

    int made = 0;
    std::map<juce::String, std::vector<SectionId>> createdByText;
    for (const auto& span : spans) {
        if (static_cast<int>(placements.size()) >= kMaxPlacements)
            break;
        const double length = span.end - span.start;
        const auto content = contentOf(span.start, length);

        SectionId target;
        auto& candidates = createdByText[span.text];
        for (const auto candidate : candidates) {
            const auto first = getPlacement(placementsOf(candidate).front());
            if (sectionLength(candidate) == length &&
                samePlacementContent(content, contentOf(first->startBeat, length))) {
                target = candidate;
                break;
            }
        }
        if (!target.isValid()) {
            if (static_cast<int>(sections.size()) >= kMaxSections)
                break;
            const int ordinal = static_cast<int>(candidates.size()) + 1;
            juce::String name = span.text.isEmpty() ? "Section " + juce::String(static_cast<int>(sections.size()) + 1)
                                                    : span.text.substring(0, kMaxMarkerTextLength - 8);
            if (span.text.isNotEmpty() && ordinal > 1)
                name += " " + juce::String(ordinal);
            applyMutation([&] {
                Section section;
                section.id = SectionId{nextSectionId++};
                section.name = name;
                section.colourArgb = colourFor ? colourFor(static_cast<int>(sections.size())) : section.colourArgb;
                section.lengthBeats = length;
                sections.push_back(section);
                target = section.id;
            });
            candidates.push_back(target);
        }
        applyMutation([&] { insertPlacementSorted({PlacementId{nextSectionId++}, target, span.start}); });
        ++made;
    }
    return made;
}

} // namespace synth
