// SectionMotionOrigins.cpp
//
// The rule, in one place: blocks that arrive or leave together (a run of them touching end to start) share one
// origin. A run that copies the sections of the blocks it touches, in order (Cmd+D, Place again after, an Option-drag
// dropped beside its original), grows out of that neighbour's edge; a make-drag grows from where the drag was
// anchored; anything else (a menu make, a copy dropped away from its original) grows from its middle. A forward
// delete always shrinks toward the middle. An undo or redo plays the edit it reverses backwards: every block keeps the
// origin it last grew from or shrank to (remembered by placement id), so the undo of a copy shrinks back into the edge
// it grew out of and the undo of a delete grows from the middle it shrank to. Blocks with no remembered origin (a
// history from before this session's first edit) fall back to the copy rule.

#include "UI/Timeline/TimelineSectionStrip/SectionMotionOrigins.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace synth::ui {

namespace {

constexpr double kTouchEpsilon = 1.0e-6;
using Spans = std::vector<PlacementSpan>;

bool touches(double a, double b) { return std::abs(a - b) < kTouchEpsilon; }

Spans sortedByStart(Spans spans) {
    std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return spans;
}

std::vector<Spans> runsOf(const Spans& changed) {
    std::vector<Spans> runs;
    for (const auto& span : sortedByStart(changed)) {
        if (runs.empty() || !touches(runs.back().back().end, span.start))
            runs.emplace_back();
        runs.back().push_back(span);
    }
    return runs;
}

// The side of `run` where the survivors touching it play the same sections in the same order: the original a copy
// follows (Left: it sits before the run) or precedes (Right).
std::optional<MotionOrigin> sourceSide(const Spans& run, const Spans& survivors) {
    const auto n = run.size();
    const auto sameSections = [&](std::size_t first) {
        for (std::size_t i = 0; i < n; ++i)
            if (survivors[first + i].section != run[i].section)
                return false;
        for (std::size_t i = 1; i < n; ++i)
            if (!touches(survivors[first + i - 1].end, survivors[first + i].start))
                return false;
        return true;
    };
    for (std::size_t i = 0; i < survivors.size(); ++i) {
        if (touches(survivors[i].end, run.front().start) && i + 1 >= n && sameSections(i + 1 - n))
            return MotionOrigin::Left;
        if (touches(survivors[i].start, run.back().end) && i + n <= survivors.size() && sameSections(i))
            return MotionOrigin::Right;
    }
    return std::nullopt;
}

SectionMotionAnchor anchorFor(MotionOrigin origin, const Spans& run, const PlacementSpan& span) {
    SectionMotionAnchor anchor{origin, (span.start + span.end) * 0.5, span.start, span.end};
    if (origin == MotionOrigin::Left)
        anchor.beat = run.front().start;
    else if (origin == MotionOrigin::Right)
        anchor.beat = run.back().end;
    return anchor;
}

const SectionMotionAnchor* anchorAt(const std::map<std::int64_t, SectionMotionAnchor>& anchors, double beat) {
    for (const auto& [id, anchor] : anchors)
        if (beat >= anchor.start - kTouchEpsilon && beat < anchor.end - kTouchEpsilon)
            return &anchor;
    return nullptr;
}

} // namespace

std::vector<PlacementSpan> placementSpansOf(const synth::TimelineDoc& doc) {
    std::vector<PlacementSpan> spans;
    for (const auto& placement : doc.getPlacements())
        if (const auto* section = doc.getSection(placement.section))
            spans.push_back({placement.id.value, placement.section.value, placement.startBeat,
                             placement.startBeat + section->lengthBeats});
    return spans;
}

const SectionMotionAnchor* SectionMotionOrigins::Moves::growAt(double beat) const { return anchorAt(grows, beat); }

const SectionMotionAnchor* SectionMotionOrigins::Moves::exitAt(double beat) const { return anchorAt(exits, beat); }

SectionMotionOrigins::Moves SectionMotionOrigins::diff(const std::vector<PlacementSpan>& before,
                                                       const std::vector<PlacementSpan>& after, bool restored,
                                                       std::optional<MotionOrigin> madeFrom) {
    std::set<std::int64_t> beforeIds, afterIds;
    for (const auto& span : before)
        beforeIds.insert(span.id);
    for (const auto& span : after)
        afterIds.insert(span.id);
    Spans added, removed, keptBefore, keptAfter;
    for (const auto& span : after)
        (beforeIds.count(span.id) != 0 ? keptAfter : added).push_back(span);
    for (const auto& span : before)
        (afterIds.count(span.id) != 0 ? keptBefore : removed).push_back(span);
    keptBefore = sortedByStart(std::move(keptBefore));
    keptAfter = sortedByStart(std::move(keptAfter));

    const auto decide = [&](const Spans& run, const Spans& kept, bool arriving) {
        if (restored) {
            const auto first = remembered_.find(run.front().id);
            const bool allSame = first != remembered_.end() && std::all_of(run.begin(), run.end(), [&](const auto& s) {
                                     const auto it = remembered_.find(s.id);
                                     return it != remembered_.end() && it->second == first->second;
                                 });
            if (allSame)
                return first->second;
        }
        if (arriving && madeFrom)
            return *madeFrom;
        if (!arriving && !restored)
            return MotionOrigin::Centre; // a delete or a cover shrinks toward the middle
        return sourceSide(run, kept).value_or(MotionOrigin::Centre);
    };

    Moves moves;
    for (const auto& run : runsOf(added)) {
        const auto origin = decide(run, keptAfter, true);
        for (const auto& span : run) {
            moves.grows[span.id] = anchorFor(origin, run, span);
            remembered_[span.id] = origin;
        }
    }
    for (const auto& run : runsOf(removed)) {
        const auto origin = decide(run, keptBefore, false);
        for (const auto& span : run) {
            moves.exits[span.id] = anchorFor(origin, run, span);
            remembered_[span.id] = origin;
        }
    }
    return moves;
}

} // namespace synth::ui
