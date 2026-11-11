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

namespace {

// The placements `ids` name, in time order, without repeats; empty when any id is unknown.
std::vector<SectionPlacement> placementsInTimeOrder(const TimelineDoc& doc, const std::vector<PlacementId>& ids) {
    std::vector<SectionPlacement> out;
    for (const auto id : ids) {
        const auto* placement = doc.getPlacement(id);
        if (placement == nullptr)
            return {};
        if (std::none_of(out.begin(), out.end(), [id](const SectionPlacement& p) { return p.id == id; }))
            out.push_back(*placement);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const SectionPlacement& a, const SectionPlacement& b) { return a.startBeat < b.startBeat; });
    return out;
}

} // namespace

// The group form of movePlacement, and with one id exactly it. `landingBeat` is the group's start after the move in
// the layout the strip shows while dragging (sectionstrip::planGroupDrop): with make-room every member's old bars
// close up first (latest first, so an earlier member's start still names its bars), then the group's summed length
// opens at the landing and each member is stamped back with the music it had. Cover clears every member's old bars,
// then whatever the group's span covers, and stamps the members over it.
bool TimelineDoc::movePlacementsTogether(const std::vector<PlacementId>& ids, double landingBeat, SectionDropMode mode,
                                         double secondsPerBeat) {
    const auto members = placementsInTimeOrder(*this, ids);
    if (members.empty() || !isFiniteAtOrAfterZero(landingBeat) || !isFinitePositive(secondsPerBeat))
        return false;
    if (members.size() == 1)
        return movePlacement(members.front().id, landingBeat, mode, secondsPerBeat);

    std::vector<PlacementContent> contents;
    double total = 0.0;
    for (const auto& member : members) {
        const double length = sectionLength(member.section);
        contents.push_back(contentOf(member.startBeat, length));
        total += length;
    }

    SectionEditScope scope(*this);
    if (mode == SectionDropMode::MakeRoom) {
        for (auto it = members.rbegin(); it != members.rend(); ++it)
            deleteTime(it->startBeat, it->startBeat + sectionLength(it->section), secondsPerBeat);
        insertTime(landingBeat, total, secondsPerBeat);
    } else {
        for (const auto& member : members) {
            const double end = member.startBeat + sectionLength(member.section);
            deleteRange(trackIds(true), member.startBeat, end, false, secondsPerBeat);
            applyMutation([&] {
                eraseLanePoints(member.startBeat, end);
                placements.erase(std::remove_if(placements.begin(), placements.end(),
                                                [&](const SectionPlacement& p) { return p.id == member.id; }),
                                 placements.end());
            });
        }
        applyMutation([&] { removePlacementsOverlapping(landingBeat, landingBeat + total, {}); });
        deleteRange(trackIds(true), landingBeat, landingBeat + total, false, secondsPerBeat);
        applyMutation([&] { eraseLanePoints(landingBeat, landingBeat + total); });
    }
    applyMutation([&] {
        double at = landingBeat;
        for (std::size_t i = 0; i < members.size(); ++i) {
            const double length = sectionLength(members[i].section);
            insertPlacementSorted({members[i].id, members[i].section, at, members[i].variation});
            stampContent(contents[i], at, length);
            at += length;
        }
    });
    return true;
}

// The group form of placeSection, and with one id exactly it. Each section's music is read before anything changes and
// the group's span is opened (or cleared) once, so a copy that covers another member's original still places the music
// that original had.
std::vector<PlacementId> TimelineDoc::placeSectionsTogether(const std::vector<PlacementId>& ids, double atBeat,
                                                            SectionDropMode mode, double secondsPerBeat) {
    const auto members = placementsInTimeOrder(*this, ids);
    if (members.empty() || !isFiniteAtOrAfterZero(atBeat) || !isFinitePositive(secondsPerBeat) ||
        static_cast<int>(placements.size() + members.size()) > kMaxPlacements)
        return {};
    if (members.size() == 1) {
        const auto placed = placeSection(members.front().section, atBeat, mode, secondsPerBeat);
        return placed.isValid() ? std::vector<PlacementId>{placed} : std::vector<PlacementId>{};
    }

    std::vector<PlacementContent> contents;
    double total = 0.0;
    for (const auto& member : members) {
        contents.push_back(variationContent(*getSection(member.section), 0));
        total += sectionLength(member.section);
    }

    SectionEditScope scope(*this);
    if (mode == SectionDropMode::MakeRoom) {
        insertTime(atBeat, total, secondsPerBeat);
    } else {
        applyMutation([&] { removePlacementsOverlapping(atBeat, atBeat + total, {}); });
        deleteRange(trackIds(true), atBeat, atBeat + total, false, secondsPerBeat);
        applyMutation([&] { eraseLanePoints(atBeat, atBeat + total); });
    }
    return applyMutation([&] {
        std::vector<PlacementId> placed;
        double at = atBeat;
        for (std::size_t i = 0; i < members.size(); ++i) {
            const double length = sectionLength(members[i].section);
            const PlacementId placementId{nextSectionId++};
            insertPlacementSorted({placementId, members[i].section, at, 0});
            stampContent(contents[i], at, length);
            placed.push_back(placementId);
            at += length;
        }
        return placed;
    });
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
