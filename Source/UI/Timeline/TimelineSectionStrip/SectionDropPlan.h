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
};

/** The plan for dropping the placements `dragged` (ids in `placements`, any order) as one group, its left edge over
 *  `rawStart`: the members land back to back in time order from the landing (gaps between them close). `copy` places
 *  their sections again (the originals stay); `makeRoom` pushes what is at the landing later, else the drop covers it.
 *  With L the members' summed length:
 *
 *  Make room: the members first leave their places and the gaps close (every other block moves earlier by the lengths
 *  of the members that end at or before it, what TimelineDoc::deleteTime does), then the group is inserted at the
 *  landing and everything from there on moves later by L. The landing snaps to a neighbour's start or end within
 *  kBoundarySnapPx, else to the nearest bar; with the group's left edge over another block it goes before that block
 *  until the group's centre passes the block's centre where it was drawn before the drag (the way a dragged track
 *  passes a neighbour), then after it. It is never inside another block. A copy closes nothing up.
 *  Cover: the landing is sectionstrip::dropBeat against the other blocks as they are; what it overlaps is covered. */
inline DropPlan planGroupDrop(const std::vector<PlacedSpan>& placements, const std::vector<std::int64_t>& dragged,
                              double rawStart, bool copy, bool makeRoom, double beatsPerBar, double pixelsPerBeat) {
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
    double length = 0.0;
    for (const auto& member : members)
        length += member.length;
    const bool closesGap = makeRoom && !copy; // the members' old bars are taken out before the group lands

    struct Basis {
        std::int64_t id;
        double start; // in the layout the landing is chosen in
        double length;
        double restMid; // its centre where it is drawn before the drag
    };
    std::vector<Basis> basis;
    std::vector<Span> spans;
    for (const auto& placement : placements) {
        if (!copy && isMember(placement.id))
            continue;
        double start = placement.start;
        if (closesGap)
            for (const auto& member : members)
                if (placement.start >= member.start + member.length - kBarEpsilon)
                    start -= member.length;
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
            landing = dropBeat(rawStart, length, beatsPerBar, pixelsPerBeat, spans, false);
        }
    } else {
        landing = dropBeat(rawStart, length, beatsPerBar, pixelsPerBeat, spans);
    }
    plan.landing = std::max(0.0, landing);

    double at = plan.landing;
    for (const auto& member : members) {
        plan.group.emplace_back(member.id, at);
        at += member.length;
    }
    for (const auto& other : basis) {
        DropPlan::Other entry{other.id, other.start, false};
        if (makeRoom) {
            if (other.start >= plan.landing - kBarEpsilon)
                entry.start += length;
        } else {
            entry.covered = other.start < plan.landing + length && plan.landing < other.start + other.length;
        }
        plan.others.push_back(entry);
    }
    return plan;
}

/** planGroupDrop with the one block `dragged`: the drag of a single block. */
inline DropPlan planSectionDrop(const std::vector<PlacedSpan>& placements, std::int64_t dragged, double rawStart,
                                bool copy, bool makeRoom, double beatsPerBar, double pixelsPerBeat) {
    return planGroupDrop(placements, {dragged}, rawStart, copy, makeRoom, beatsPerBar, pixelsPerBeat);
}

} // namespace synth::ui::sectionstrip
