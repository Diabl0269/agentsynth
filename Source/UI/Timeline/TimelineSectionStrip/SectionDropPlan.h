#pragma once

// SectionDropPlan.h: where a dragged section block, or a group of them, lands and where every other block ends up, as
// one pure function the strip's preview (the drop line, the ghosts' settle, the neighbours making room) and its tests
// read. Every beat here is in the layout AFTER the drop, the one the user sees while dragging, so the drop line, the
// ghost's left edge and the final start are one number. TimelineDoc::movePlacement, movePlacementsTogether,
// placeSection and placeSectionsTogether take that number as the start the group gets; the strip's drag tests check,
// case by case, that the doc after a drop equals this plan.

#include "UI/Timeline/TimelineSectionStrip/SectionStripGeometry.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace synth::ui::sectionstrip {

/** One placement as the plan sees it: its id, its start and its section's length, in beats. */
struct PlacedSpan {
    std::int64_t id = 0;
    double start = 0.0;
    double length = 0.0;
};

struct DropPlan {
    double landing = 0.0; // the start the dropped block gets
    struct Other {
        std::int64_t id = 0;
        double start = 0.0;   // where it starts after the drop
        bool covered = false; // a cover drop removes it (its music is overwritten)
    };
    std::vector<Other> others; // every placement except the moved ones (a copy's originals are among them)
    // Each dragged placement's start after the drop, in time order (for a copy, its copy's start), keyed by its id.
    std::vector<std::pair<std::int64_t, double>> group;
    // Make-room only: false when the landing span overlaps no other block, so nothing is pushed later and the words say
    // "place" rather than "insert". Always true for a cover drop (it has no room to open).
    bool opensRoom = true;
    // A make-room move only: the blocks between the members (blocksBetween), which go along inside the group's shape.
    // They are among `others`, at their landed starts; the drop hint names them.
    std::vector<std::int64_t> carried;

    std::optional<double> startOf(std::int64_t id) const {
        for (const auto& other : others)
            if (other.id == id && !other.covered)
                return other.start;
        return std::nullopt;
    }
    bool isCovered(std::int64_t id) const {
        for (const auto& other : others)
            if (other.id == id)
                return other.covered;
        return false;
    }
    bool isCarried(std::int64_t id) const { return std::find(carried.begin(), carried.end(), id) != carried.end(); }
};

/** The placements inside the span of the group `dragged` (its first member's start to its last member's end) that are
 *  not members: the blocks between them. A make-room move takes the whole span out and puts it back at the landing, so
 *  they go along at their places in the group's shape and a drop at the group's own start changes nothing. */
inline std::vector<std::int64_t> blocksBetween(const std::vector<PlacedSpan>& placements,
                                               const std::vector<std::int64_t>& dragged) {
    const auto isMember = [&dragged](std::int64_t id) {
        return std::find(dragged.begin(), dragged.end(), id) != dragged.end();
    };
    double first = 0.0, end = 0.0;
    bool any = false;
    for (const auto& placement : placements)
        if (isMember(placement.id)) {
            first = any ? std::min(first, placement.start) : placement.start;
            end = any ? std::max(end, placement.start + placement.length) : placement.start + placement.length;
            any = true;
        }
    std::vector<std::int64_t> between;
    for (const auto& placement : placements)
        if (any && !isMember(placement.id) && placement.start >= first - kBarEpsilon &&
            placement.start < end - kBarEpsilon)
            between.push_back(placement.id);
    return between;
}

/** The plan for dropping the placements `dragged` (ids in `placements`, any order) as one group, its left edge over
 *  `rawStart`: every member keeps its distance from the group's left edge, so the gaps between members stay (the group
 *  spans first start to last end). `copy` places their sections again (the originals stay); `makeRoom` pushes what is
 *  at the landing later, else the drop covers it. With L that span:
 *
 *  Make room: a move takes the whole span out (every block after it moves earlier by L, what TimelineDoc::deleteTime
 *  does) and the blocks between the members (blocksBetween) go along at their places in the group's shape, so the
 *  closed-up layout is the same wherever the group sits and a drop at its own start changes nothing. Then the span is
 *  opened once at the landing and everything from there on moves later by L, but only when the span [landing,
 *  landing + L) overlaps another block (the originals of a copy stay and count) or a clip (`clips`: every clip's span;
 * music that sits loose between blocks is never covered): a free span opens nothing. A move always opens it (the blocks
 * past both places net out, so a move into a free spot moves nothing else). The landing snaps to a neighbour's start or
 * end within kBoundarySnapPx, else to the nearest bar; with the group's left edge over another block it goes before
 * that block until the group's centre passes the block's centre where it was drawn before the drag (the way a dragged
 * track passes a neighbour), then after it. It is never inside another block. A copy closes nothing up. Cover: the
 * landing is sectionstrip::dropBeat against the other blocks as they are; a block is covered only when it overlaps a
 * member's landed place, what sits between landed members is left alone. */
inline DropPlan planGroupDrop(const std::vector<PlacedSpan>& placements, const std::vector<std::int64_t>& dragged,
                              double rawStart, bool copy, bool makeRoom, double gridBeats, double pixelsPerBeat,
                              const std::vector<Span>& clips = {}) {
    DropPlan plan;
    const auto isMember = [&dragged](std::int64_t id) {
        return std::find(dragged.begin(), dragged.end(), id) != dragged.end();
    };
    std::vector<PlacedSpan> members;
    for (const auto& placement : placements)
        if (isMember(placement.id))
            members.push_back(placement);
    if (members.empty())
        return plan;
    std::stable_sort(members.begin(), members.end(),
                     [](const PlacedSpan& a, const PlacedSpan& b) { return a.start < b.start; });
    const double firstStart = members.front().start;
    double length = 0.0; // the group's span, first start to last end
    for (const auto& member : members)
        length = std::max(length, member.start + member.length - firstStart);
    const double groupEnd = firstStart + length;
    const bool closesGap = makeRoom && !copy; // the group's span is taken out before it lands
    const auto carried = closesGap ? blocksBetween(placements, dragged) : std::vector<std::int64_t>{};
    const auto isCarried = [&carried](std::int64_t id) {
        return std::find(carried.begin(), carried.end(), id) != carried.end();
    };

    struct Basis {
        std::int64_t id;
        double start; // in the layout the landing is chosen in
        double length;
        double restMid; // its centre where it is drawn before the drag
    };
    std::vector<Basis> basis;
    std::vector<Span> spans;
    for (const auto& placement : placements) {
        if ((!copy && isMember(placement.id)) || isCarried(placement.id))
            continue;
        double start = placement.start;
        if (closesGap && placement.start >= groupEnd - kBarEpsilon)
            start -= length;
        basis.push_back({placement.id, start, placement.length, placement.start + placement.length * 0.5});
        spans.push_back({start, start + placement.length});
    }

    double landing = 0.0;
    if (makeRoom) {
        const Basis* over = nullptr;
        for (const auto& other : basis)
            if (rawStart > other.start + kBarEpsilon && rawStart < other.start + other.length - kBarEpsilon)
                over = &other;
        if (over != nullptr) {
            const double centre = rawStart + length * 0.5;
            landing = centre > over->restMid ? over->start + over->length : over->start;
        } else {
            // A copy may also snap its right edge to a block's left edge (just before the original).
            landing = dropBeat(rawStart, length, gridBeats, pixelsPerBeat, spans, false, copy);
        }
    } else {
        landing = dropBeat(rawStart, length, gridBeats, pixelsPerBeat, spans);
    }
    plan.landing = std::max(0.0, landing);

    for (const auto& member : members)
        plan.group.emplace_back(member.id, plan.landing + (member.start - firstStart));
    // A move always opens its span again (its own bars closed up first, so the blocks past both places net out); a copy
    // opens it only when something is in the way.
    if (makeRoom && copy) {
        const auto overlaps = [&](double start, double end) {
            return start < plan.landing + length - kBarEpsilon && plan.landing + kBarEpsilon < end;
        };
        plan.opensRoom =
            std::any_of(basis.begin(), basis.end(),
                        [&](const Basis& other) { return overlaps(other.start, other.start + other.length); }) ||
            std::any_of(clips.begin(), clips.end(), [&](const Span& clip) { return overlaps(clip.start, clip.end); });
    }
    for (const auto& other : basis) {
        DropPlan::Other entry{other.id, other.start, false};
        if (makeRoom) {
            if (plan.opensRoom && other.start >= plan.landing - kBarEpsilon)
                entry.start += length;
        } else {
            for (const auto& member : members) {
                const double from = plan.landing + (member.start - firstStart);
                entry.covered |= other.start < from + member.length && from < other.start + other.length;
            }
        }
        plan.others.push_back(entry);
    }
    for (const auto& placement : placements)
        if (isCarried(placement.id))
            plan.others.push_back({placement.id, plan.landing + (placement.start - firstStart), false});
    plan.carried = carried;
    return plan;
}

/** planGroupDrop with the one block `dragged`: the drag of a single block. */
inline DropPlan planSectionDrop(const std::vector<PlacedSpan>& placements, std::int64_t dragged, double rawStart,
                                bool copy, bool makeRoom, double gridBeats, double pixelsPerBeat,
                                const std::vector<Span>& clips = {}) {
    return planGroupDrop(placements, {dragged}, rawStart, copy, makeRoom, gridBeats, pixelsPerBeat, clips);
}

} // namespace synth::ui::sectionstrip
