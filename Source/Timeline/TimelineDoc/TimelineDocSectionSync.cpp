// Concern: sections - what a placement holds, and keeping every placement of a section in step.
//
// A placement holds every clip whose startBeat is in [start, end) (a clip may run past the end and still
// belongs to it) and every automation breakpoint whose beat is in that span. Content is compared relative to the
// placement's start and ignores ids and link groups. finishMutation calls syncSections(): when exactly the
// edited placement of a section differs from what it held after the previous mutation, it is stamped onto its
// siblings inside the same mutation, so one edit is one revision and one undo step. When several placements of
// one section changed at once, the one with the lowest start wins. A freshly loaded document is cached without
// stamping, so a file whose repeats differ keeps them until one is edited.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>

namespace synth {

using namespace detail;

namespace {

// Beats compare equal within this, so a placement that moved by a non-exact amount is not seen as edited.
constexpr double kBeatEpsilon = 1e-9;

bool nearlyEqual(double a, double b) noexcept { return std::abs(a - b) <= kBeatEpsilon; }

bool sameNote(const MidiNote& a, const MidiNote& b) noexcept {
    return nearlyEqual(a.startBeat, b.startBeat) && a.lengthBeats == b.lengthBeats && a.pitch == b.pitch &&
           a.velocity == b.velocity && a.channel == b.channel && a.muted == b.muted && a.chance == b.chance &&
           a.repeat == b.repeat && a.flam == b.flam && a.nudge == b.nudge && a.condHit == b.condHit &&
           a.condOf == b.condOf;
}

bool sameDrumRows(const std::vector<Clip::DrumRow>& a, const std::vector<Clip::DrumRow>& b) noexcept {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].pitch != b[i].pitch || a[i].lengthSteps != b[i].lengthSteps || a[i].swing != b[i].swing)
            return false;
    return true;
}

bool samePoint(const AutomationLane::Breakpoint& a, const AutomationLane::Breakpoint& b) noexcept {
    return nearlyEqual(a.beat, b.beat) && a.value == b.value && a.tension == b.tension && a.curve == b.curve;
}

} // namespace

bool sameClipContent(const Clip& a, const Clip& b) {
    if (a.notes.size() != b.notes.size())
        return false;
    for (size_t i = 0; i < a.notes.size(); ++i)
        if (!sameNote(a.notes[i], b.notes[i]))
            return false;
    return nearlyEqual(a.startBeat, b.startBeat) && a.name == b.name && a.lengthBeats == b.lengthBeats &&
           a.assetRef == b.assetRef && a.gainDb == b.gainDb && a.fadeInBeats == b.fadeInBeats &&
           a.fadeOutBeats == b.fadeOutBeats && a.sourceStartSeconds == b.sourceStartSeconds && a.muted == b.muted &&
           a.stepsPerBar == b.stepsPerBar && a.swing == b.swing && a.humanise == b.humanise &&
           sameDrumRows(a.drumRows, b.drumRows) && a.mutedPitches == b.mutedPitches && a.soloPitches == b.soloPitches &&
           a.scale == b.scale && a.scaleVisible == b.scaleVisible && a.noteRange == b.noteRange &&
           a.stretchOrigin == b.stretchOrigin;
}

bool samePlacementContent(const PlacementContent& a, const PlacementContent& b) {
    if (a.clips.size() != b.clips.size() || a.points.size() != b.points.size())
        return false;
    for (const auto& [track, clipsA] : a.clips) {
        const auto it = b.clips.find(track);
        if (it == b.clips.end() || it->second.size() != clipsA.size())
            return false;
        for (size_t i = 0; i < clipsA.size(); ++i)
            if (!sameClipContent(clipsA[i], it->second[i]))
                return false;
    }
    for (const auto& [lane, pointsA] : a.points) {
        const auto it = b.points.find(lane);
        if (it == b.points.end() || it->second.size() != pointsA.size())
            return false;
        for (size_t i = 0; i < pointsA.size(); ++i)
            if (!samePoint(pointsA[i], it->second[i]))
                return false;
    }
    return true;
}

// ------------------------------------------------------------------ lookups --

const Section* TimelineDoc::getSection(SectionId id) const {
    for (const auto& section : sections)
        if (section.id == id)
            return &section;
    return nullptr;
}

const SectionPlacement* TimelineDoc::getPlacement(PlacementId id) const {
    for (const auto& placement : placements)
        if (placement.id == id)
            return &placement;
    return nullptr;
}

double TimelineDoc::sectionLength(SectionId id) const {
    const auto* section = getSection(id);
    return section != nullptr ? section->lengthBeats : 0.0;
}

const SectionPlacement* TimelineDoc::placementAt(double beat) const {
    for (const auto& placement : placements)
        if (beat >= placement.startBeat && beat < placement.startBeat + sectionLength(placement.section))
            return &placement;
    return nullptr;
}

std::vector<PlacementId> TimelineDoc::placementsOf(SectionId id) const {
    std::vector<PlacementId> out;
    for (const auto& placement : placements)
        if (placement.section == id)
            out.push_back(placement.id);
    return out;
}

int TimelineDoc::placementOrdinal(PlacementId id) const {
    const auto* target = getPlacement(id);
    if (target == nullptr)
        return 0;
    int ordinal = 0;
    for (const auto& placement : placements) {
        if (placement.section == target->section)
            ++ordinal;
        if (placement.id == id)
            return ordinal;
    }
    return 0;
}

const SectionPlacement* TimelineDoc::placementForClip(ClipId id) const {
    const auto* clip = getClip(id);
    return clip != nullptr ? placementAt(clip->startBeat) : nullptr;
}

// ------------------------------------------------------------------ content --

PlacementContent TimelineDoc::contentOf(double startBeat, double lengthBeats) const {
    PlacementContent content;
    const double endBeat = startBeat + lengthBeats;
    for (const auto& track : tracks) {
        std::vector<Clip> held;
        const auto first = std::lower_bound(track.clips.begin(), track.clips.end(), startBeat,
                                            [](const Clip& c, double beat) { return c.startBeat < beat; });
        for (auto it = first; it != track.clips.end() && it->startBeat < endBeat; ++it) {
            Clip copy = *it;
            copy.startBeat -= startBeat;
            held.push_back(std::move(copy));
        }
        if (!held.empty())
            content.clips[track.id.value] = std::move(held);

        for (const auto& lane : track.lanes) {
            std::vector<AutomationLane::Breakpoint> held2;
            const auto pFirst =
                std::lower_bound(lane.points.begin(), lane.points.end(), startBeat,
                                 [](const AutomationLane::Breakpoint& p, double beat) { return p.beat < beat; });
            for (auto it = pFirst; it != lane.points.end() && it->beat < endBeat; ++it) {
                auto copy = *it;
                copy.beat -= startBeat;
                held2.push_back(copy);
            }
            if (!held2.empty())
                content.points[lane.id.value] = std::move(held2);
        }
    }
    return content;
}

std::vector<TrackId> TimelineDoc::trackIds(bool includeAutomation) const {
    std::vector<TrackId> ids;
    for (const auto& track : tracks)
        if (includeAutomation || track.kind != TrackKind::Automation)
            ids.push_back(track.id);
    return ids;
}

void TimelineDoc::eraseLanePoints(double startBeat, double endBeat) {
    for (auto& track : tracks)
        for (auto& lane : track.lanes)
            lane.points.erase(std::remove_if(lane.points.begin(), lane.points.end(),
                                             [&](const AutomationLane::Breakpoint& p) {
                                                 return p.beat >= startBeat && p.beat < endBeat;
                                             }),
                              lane.points.end());
}

// Writes `source` (relative content) over the span at targetStart. Raw edits only: the
// caller is inside a mutation.
void TimelineDoc::stampContent(const PlacementContent& source, double targetStart, double length) {
    const double targetEnd = targetStart + length;
    for (auto& track : tracks) {
        std::vector<Clip> empty;
        const auto found = source.clips.find(track.id.value);
        const auto& wanted = found != source.clips.end() ? found->second : empty;

        std::vector<size_t> held;
        for (size_t i = 0; i < track.clips.size(); ++i)
            if (track.clips[i].startBeat >= targetStart && track.clips[i].startBeat < targetEnd)
                held.push_back(i);

        const auto fresh = [&](Clip copy) {
            copy.startBeat += targetStart;
            for (auto& note : copy.notes)
                note.id = NoteId{nextNoteId++};
            return copy;
        };

        if (held.size() == wanted.size()) {
            for (size_t i = 0; i < held.size(); ++i) {
                const auto keepId = track.clips[held[i]].id;
                track.clips[held[i]] = fresh(wanted[i]);
                track.clips[held[i]].id = keepId;
            }
        } else {
            // Past the per-track cap a copy cannot be added; leave this track as it is.
            if (static_cast<int>(track.clips.size() - held.size() + wanted.size()) > kMaxClipsPerTrack)
                continue;
            for (auto it = held.rbegin(); it != held.rend(); ++it)
                track.clips.erase(track.clips.begin() + static_cast<std::ptrdiff_t>(*it));
            for (const auto& clip : wanted) {
                auto copy = fresh(clip);
                copy.id = ClipId{nextClipId++};
                track.clips.push_back(std::move(copy));
            }
        }
        std::stable_sort(track.clips.begin(), track.clips.end(), clipLess);

        for (auto& lane : track.lanes) {
            lane.points.erase(std::remove_if(lane.points.begin(), lane.points.end(),
                                             [&](const AutomationLane::Breakpoint& p) {
                                                 return p.beat >= targetStart && p.beat < targetEnd;
                                             }),
                              lane.points.end());
            const auto pts = source.points.find(lane.id.value);
            if (pts != source.points.end())
                for (auto point : pts->second) {
                    point.beat += targetStart;
                    lane.points.push_back(point);
                }
            std::stable_sort(lane.points.begin(), lane.points.end(),
                             [](const AutomationLane::Breakpoint& a, const AutomationLane::Breakpoint& b) {
                                 return a.beat < b.beat;
                             });
        }
    }
}

// ----------------------------------------------------------------- the sync --

bool TimelineDoc::sectionsNeedSync() const {
    for (const auto& placement : placements)
        if (getSection(placement.section) == nullptr)
            return true;
    for (const auto& section : sections) {
        const auto ids = placementsOf(section.id);
        if (ids.empty())
            return true;
        if (ids.size() < 2)
            continue;
        for (const auto id : ids) {
            const auto cached = sectionContentCache.find(id.value);
            if (cached != sectionContentCache.end() &&
                !samePlacementContent(cached->second, contentOf(getPlacement(id)->startBeat, section.lengthBeats)))
                return true;
        }
    }
    return false;
}

void TimelineDoc::syncSections() {
    if (sectionSyncSuspended > 0)
        return;

    // Invariant: every placement names a section and every section has a placement.
    placements.erase(std::remove_if(placements.begin(), placements.end(),
                                    [&](const SectionPlacement& p) { return getSection(p.section) == nullptr; }),
                     placements.end());
    sections.erase(
        std::remove_if(sections.begin(), sections.end(), [&](const Section& s) { return placementsOf(s.id).empty(); }),
        sections.end());
    for (auto it = sectionContentCache.begin(); it != sectionContentCache.end();)
        it = getPlacement(PlacementId{it->first}) == nullptr ? sectionContentCache.erase(it) : std::next(it);

    for (const auto& section : sections) {
        const auto ids = placementsOf(section.id);
        if (ids.size() < 2)
            continue;
        const SectionPlacement* source = nullptr; // placements are sorted by start: the first changed one is lowest
        for (const auto id : ids) {
            const auto* placement = getPlacement(id);
            const auto cached = sectionContentCache.find(id.value);
            if (cached != sectionContentCache.end() &&
                !samePlacementContent(cached->second, contentOf(placement->startBeat, section.lengthBeats))) {
                source = placement;
                break;
            }
        }
        if (source == nullptr)
            continue;
        const double sourceStart = source->startBeat;
        const auto sourceId = source->id;
        const auto content = contentOf(sourceStart, section.lengthBeats);
        for (const auto id : ids) {
            if (id == sourceId)
                continue;
            stampContent(content, getPlacement(id)->startBeat, section.lengthBeats);
            lastStampedPlacements.push_back(id);
        }
        ++sectionStampCount;
    }

    for (const auto& placement : placements)
        sectionContentCache[placement.id.value] = contentOf(placement.startBeat, sectionLength(placement.section));
}

// ---------------------------------------------------------------- the scope --

TimelineDoc::SectionEditScope::SectionEditScope(TimelineDoc& d)
    : doc(d) {
    ++doc.sectionSyncSuspended;
}

TimelineDoc::SectionEditScope::~SectionEditScope() {
    if (--doc.sectionSyncSuspended > 0)
        return;
    if (doc.sectionsNeedSync())
        doc.applyMutation([] {});
    else
        doc.syncSections(); // nothing to stamp: only refreshes the cache, no revision bump
}

} // namespace synth
