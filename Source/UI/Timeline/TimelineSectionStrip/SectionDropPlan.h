#pragma once

// SectionDropPlan.h: where a dragged section block lands and where every other block ends up, as one pure function the
// strip's preview (the drop line, the ghost's settle, the neighbours making room) and its tests read. Every beat here
// is in the layout AFTER the drop, the one the user sees while dragging, so the drop line, the ghost's left edge and
// the block's final start are one number. TimelineDoc::movePlacement and placeSection take that number as the start the
// block gets; TimelineSectionStripDragTests check, case by case, that the doc after a drop equals this plan.

#include "UI/Timeline/TimelineSectionStrip/SectionStripGeometry.h"
#include <cstdint>
#include <optional>
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
    std::vector<Other> others; // every placement except the moved one (a copy's original is one of them)

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

/** The plan for dropping `dragged` (a placement in `placements`) with its left edge over `rawStart`. `copy` places the
 *  section again (the original stays); `makeRoom` pushes what is at the landing later, else the drop covers it.
 *
 *  Make room: a moved block first leaves its place and the gap it leaves closes (what TimelineDoc::deleteTime does),
 *  then it is inserted at the landing and everything from there on moves later by its length. The landing snaps to a
 *  neighbour's start or end within kBoundarySnapPx, else to the nearest bar; with the ghost's left edge over another
 *  block it goes before that block until the ghost's centre passes the block's centre where it was drawn before the
 *  drag (the way a dragged track passes a neighbour), then after it. It is never inside another block.
 *  Cover: the landing is sectionstrip::dropBeat against the other blocks as they are; what it overlaps is covered. */
inline DropPlan planSectionDrop(const std::vector<PlacedSpan>& placements, std::int64_t dragged, double rawStart,
                                bool copy, bool makeRoom, double beatsPerBar, double pixelsPerBeat) {
    DropPlan plan;
    const PlacedSpan* self = nullptr;
    for (const auto& placement : placements)
        if (placement.id == dragged)
            self = &placement;
    if (self == nullptr)
        return plan;
    const double length = self->length;
    const double oldEnd = self->start + length;
    const bool closesGap = makeRoom && !copy; // the moved block's old bars are taken out before it lands

    struct Basis {
        std::int64_t id;
        double start; // in the layout the landing is chosen in
        double length;
        double restMid; // its centre where it is drawn before the drag
    };
    std::vector<Basis> basis;
    std::vector<Span> spans;
    for (const auto& placement : placements) {
        if (!copy && placement.id == dragged)
            continue;
        double start = placement.start;
        if (closesGap && placement.start >= oldEnd - kBarEpsilon)
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
            landing = dropBeat(rawStart, length, beatsPerBar, pixelsPerBeat, spans, false);
        }
    } else {
        landing = dropBeat(rawStart, length, beatsPerBar, pixelsPerBeat, spans);
    }
    plan.landing = std::max(0.0, landing);

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

} // namespace synth::ui::sectionstrip
