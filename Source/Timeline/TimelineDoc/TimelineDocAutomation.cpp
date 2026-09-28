// Concern: automation lanes, plus binding reconciliation for tracks and lanes.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace synth {

using namespace detail;

namespace {

// First point at or after `beat`. The list is sorted, so every beat lookup is a binary search.
std::vector<AutomationLane::Breakpoint>::iterator lowerBoundByBeat(std::vector<AutomationLane::Breakpoint>& points,
                                                                   double beat) {
    return std::lower_bound(points.begin(), points.end(), beat,
                            [](const AutomationLane::Breakpoint& p, double b) { return p.beat < b; });
}

// Inserts (or replaces, on an exact beat match) into an already-sorted point list.
void insertBreakpoint(std::vector<AutomationLane::Breakpoint>& points, const AutomationLane::Breakpoint& point) {
    const auto pos = lowerBoundByBeat(points, point.beat);
    if (pos != points.end() && pos->beat == point.beat)
        *pos = point;
    else
        points.insert(pos, point);
}

} // namespace

// ---------------------------------------------------------------------- lanes --

LaneId TimelineDoc::addLane(TrackId trackId, const juce::String& nodeUuid, const juce::String& paramId,
                            const AutomationLane::RangeSnapshot& range, int paramIndexHint) {
    // Identity check first, and doc-wide: one lane per bound parameter, whichever track it
    // happens to sit on. Returning the existing id is a lookup, not a mutation.
    if (auto* existing = findLaneForParam(nodeUuid, paramId))
        return existing->id;

    auto* track = findTrack(trackId);
    if (track == nullptr)
        return {};
    if (nodeUuid.isEmpty() || paramId.isEmpty() || !isValidRange(range))
        return {};
    if (static_cast<int>(track->lanes.size()) >= kMaxLanesPerTrack)
        return {};

    return applyMutation([&] {
        AutomationLane lane;
        lane.id = LaneId{nextLaneId++};
        lane.nodeUuid = nodeUuid;
        lane.paramId = paramId;
        lane.range = range;
        lane.paramIndexHint = paramIndexHint;
        track->lanes.push_back(std::move(lane));
        return track->lanes.back().id;
    });
}

bool TimelineDoc::removeLane(LaneId id) {
    Track* owner = nullptr;
    auto* lane = findLane(id, &owner);
    if (lane == nullptr)
        return false;

    return applyMutation([&] {
        owner->lanes.erase(owner->lanes.begin() + (lane - owner->lanes.data()));
        return true;
    });
}

bool TimelineDoc::addBreakpoint(LaneId laneId, double beat, double value, float tension, int curve) {
    auto* lane = findLane(laneId);
    if (lane == nullptr)
        return false;
    if (!isFiniteAtOrAfterZero(beat) || !std::isfinite(value) || !std::isfinite(tension) || !isValidCurve(curve))
        return false;

    const auto point = makeBreakpoint(lane->range, beat, value, tension, curve);
    // Replacing an existing point doesn't grow the lane, so it stays legal at the cap.
    const auto existing = lowerBoundByBeat(lane->points, beat);
    const bool replacesExisting = existing != lane->points.end() && existing->beat == beat;
    if (!replacesExisting && static_cast<int>(lane->points.size()) >= kMaxBreakpointsPerLane)
        return false;

    return applyMutation([&] {
        insertBreakpoint(lane->points, point);
        return true;
    });
}

bool TimelineDoc::removeBreakpoint(LaneId laneId, double beat) {
    auto* lane = findLane(laneId);
    if (lane == nullptr)
        return false;
    const auto pos = lowerBoundByBeat(lane->points, beat);
    if (pos == lane->points.end() || pos->beat != beat)
        return false;

    return applyMutation([&] {
        lane->points.erase(pos);
        return true;
    });
}

bool TimelineDoc::editBreakpoints(LaneId laneId, const std::vector<double>& removeBeats,
                                  const std::vector<AutomationLane::Breakpoint>& addPoints) {
    auto* lane = findLane(laneId);
    if (lane == nullptr)
        return false;
    if (removeBeats.empty() && addPoints.empty())
        return true; // nothing to do: no-op, no revision bump

    for (const auto& p : addPoints)
        if (!isFiniteAtOrAfterZero(p.beat) || !std::isfinite(p.value) || !std::isfinite(p.tension) ||
            !isValidCurve(p.curve))
            return false;

    // Plan against a COPY first — same "simulate, then commit" shape as splitClip/reconcileBindings
    // — so a cap violation is rejected before the live lane is ever touched.
    auto simulated = lane->points;
    for (double beat : removeBeats) {
        const auto pos = lowerBoundByBeat(simulated, beat);
        if (pos != simulated.end() && pos->beat == beat)
            simulated.erase(pos);
    }
    for (const auto& p : addPoints)
        insertBreakpoint(simulated, makeBreakpoint(lane->range, p.beat, p.value, p.tension, p.curve));

    if (static_cast<int>(simulated.size()) > kMaxBreakpointsPerLane)
        return false;

    return applyMutation([&] {
        lane->points = std::move(simulated);
        return true;
    });
}

namespace {

bool inSpan(double beat, double start, double end) noexcept { return beat >= start && beat < end; }

void eraseSpan(std::vector<AutomationLane::Breakpoint>& points, double start, double end) {
    points.erase(
        std::remove_if(points.begin(), points.end(),
                       [start, end](const AutomationLane::Breakpoint& p) { return inSpan(p.beat, start, end); }),
        points.end());
}

bool isValidSpanEdit(const TimelineDoc::AutomationSpanEdit& edit) noexcept {
    if (!std::isfinite(edit.startBeat) || !std::isfinite(edit.endBeat) || edit.startBeat < 0.0 ||
        edit.endBeat <= edit.startBeat)
        return false;
    if (edit.kind == TimelineDoc::AutomationSpanEdit::Kind::Remove)
        return true;
    return isFiniteAtOrAfterZero(edit.destStartBeat);
}

} // namespace

// "Automation follows events" (docs/timeline/track-automation.md#automation-follows-events): the
// clip edit's span of automation on the SOURCE track's own lanes travels with it.
//
// Rules, per edit and per lane stored on `sourceTrack`:
//   - Only points in the half-open span [startBeat, endBeat) are carried; a lane with none there
//     is untouched (the destination span is NOT cleared for an empty carry, so moving a clip with
//     no automation under it never wipes automation where it lands).
//   - Move/Copy to the same track: the carried points replace whatever sits in the destination
//     span on the same lane. To a DIFFERENT track the lane identity cannot be the same one (lanes
//     are unique per (nodeUuid, paramId) doc-wide), so the target is the destination track's lane
//     with the same paramId when there is exactly one such lane; otherwise a Move leaves the points
//     where they are and a Copy writes nothing. Values are clamped into the target lane's range.
//   - Remove drops the span's points.
// Every read comes from the lanes' ORIGINAL points and the writes run in three passes (all source
// removals, then all destination clears, then all inserts), so a multi-clip batch whose spans
// overlap never reads a point another edit in the same batch already moved. One applyMutation for
// the whole batch; rejected outright (no mutation) on any malformed edit, an unresolved track, or a
// lane that would exceed kMaxBreakpointsPerLane.
int TimelineDoc::planAutomationSpanTransfer(const std::vector<AutomationSpanEdit>& edits,
                                            std::vector<SpanTransferPlanEntry>& planOut) {
    planOut.clear();
    struct Carry {
        AutomationLane* dest = nullptr;
        double destStart = 0.0;
        double destEnd = 0.0;
        std::vector<AutomationLane::Breakpoint> points;
    };
    struct Removal {
        AutomationLane* lane = nullptr;
        double start = 0.0;
        double end = 0.0;
    };
    std::vector<Carry> carries;
    std::vector<Removal> removals;

    auto uniqueLaneWithParam = [](Track& track, const juce::String& paramId) -> AutomationLane* {
        AutomationLane* found = nullptr;
        for (auto& lane : track.lanes) {
            if (lane.paramId != paramId)
                continue;
            if (found != nullptr)
                return nullptr; // ambiguous: two candidates, pick neither
            found = &lane;
        }
        return found;
    };

    for (const auto& edit : edits) {
        if (!isValidSpanEdit(edit))
            return -1;
        auto* source = findTrack(edit.sourceTrack);
        if (source == nullptr)
            return -1;
        Track* destTrack = nullptr;
        if (edit.kind != AutomationSpanEdit::Kind::Remove && (destTrack = findTrack(edit.destTrack)) == nullptr)
            return -1;

        for (auto& lane : source->lanes) {
            std::vector<AutomationLane::Breakpoint> carried;
            for (const auto& p : lane.points)
                if (inSpan(p.beat, edit.startBeat, edit.endBeat))
                    carried.push_back(p);
            if (carried.empty())
                continue;

            if (edit.kind == AutomationSpanEdit::Kind::Remove) {
                removals.push_back({&lane, edit.startBeat, edit.endBeat});
                continue;
            }
            auto* dest = destTrack == source ? &lane : uniqueLaneWithParam(*destTrack, lane.paramId);
            if (dest == nullptr)
                continue; // no matching lane on the destination track: leave the points alone
            if (edit.kind == AutomationSpanEdit::Kind::Move)
                removals.push_back({&lane, edit.startBeat, edit.endBeat});
            const double offset = edit.destStartBeat - edit.startBeat;
            for (auto& p : carried)
                p = makeBreakpoint(dest->range, p.beat + offset, p.value, p.tension, p.curve);
            carries.push_back(
                {dest, edit.destStartBeat, edit.destStartBeat + (edit.endBeat - edit.startBeat), std::move(carried)});
        }
    }

    if (removals.empty() && carries.empty())
        return 0; // nothing under any span: no-op, no revision bump

    std::map<AutomationLane*, std::vector<AutomationLane::Breakpoint>> simulated;
    auto planFor = [&simulated](AutomationLane* lane) -> std::vector<AutomationLane::Breakpoint>& {
        return simulated.try_emplace(lane, lane->points).first->second;
    };
    for (const auto& removal : removals)
        eraseSpan(planFor(removal.lane), removal.start, removal.end);
    for (const auto& carry : carries)
        eraseSpan(planFor(carry.dest), carry.destStart, carry.destEnd);
    for (const auto& carry : carries)
        for (const auto& p : carry.points)
            insertBreakpoint(planFor(carry.dest), p);

    for (auto& [lane, points] : simulated) {
        if (static_cast<int>(points.size()) > kMaxBreakpointsPerLane)
            return -1;
        planOut.push_back({lane, std::move(points)});
    }
    return 1;
}

bool TimelineDoc::transferAutomationSpans(const std::vector<AutomationSpanEdit>& edits) {
    std::vector<SpanTransferPlanEntry> plan;
    const int outcome = planAutomationSpanTransfer(edits, plan);
    if (outcome <= 0)
        return outcome == 0;
    return applyMutation([&] {
        for (auto& entry : plan)
            entry.lane->points = std::move(entry.points);
        return true;
    });
}

// The dry run callers use BEFORE a clip edit: the automation half of "automation follows events"
// is only allowed to go ahead together with the clip edit, so a batch this would refuse (a cap, an
// unresolved track) must be known before the clip moves. Clip edits never touch lanes, so the answer
// cannot change between this check and the transfer inside the same mutation.
bool TimelineDoc::canTransferAutomationSpans(const std::vector<AutomationSpanEdit>& edits) const {
    std::vector<SpanTransferPlanEntry> plan;
    return const_cast<TimelineDoc*>(this)->planAutomationSpanTransfer(edits, plan) >= 0;
}

bool TimelineDoc::setLaneRecordMode(LaneId id, int mode) {
    auto* lane = findLane(id);
    if (lane == nullptr || !isValidRecordMode(mode))
        return false;
    if (lane->recordMode == mode)
        return true; // already there: no revision bump, no notification
    return applyMutation([&] {
        lane->recordMode = mode;
        return true;
    });
}

// -------------------------------------------------------- bindings --

bool TimelineDoc::reconcileBindings(const std::function<bool(const juce::String& uuid)>& uuidResolves,
                                    const std::function<bool(const juce::String& uuid, const juce::String& paramId,
                                                             int paramIndexHint)>& laneResolves) {
    // Plan first, exactly like splitClip/applySnapshotPreservingNodes: compute what every flag
    // SHOULD be without touching anything, so a reconcile that changes nothing never enters
    // applyMutation (no bump, no notification) and uuidResolves is called exactly once per
    // binding rather than once per binding per pass.
    struct TrackPlan {
        bool orphaned = false;
        std::vector<bool> laneOrphaned;
    };

    std::vector<TrackPlan> plans;
    plans.reserve(tracks.size());
    bool anyChanged = false;

    for (auto& track : tracks) {
        TrackPlan plan;
        // A track binds to a node, never a parameter — uuidResolves alone is always the whole story.
        plan.orphaned = track.bindingUuid.isNotEmpty() && !uuidResolves(track.bindingUuid);
        if (plan.orphaned != track.orphaned)
            anyChanged = true;

        plan.laneOrphaned.reserve(track.lanes.size());
        for (auto& lane : track.lanes) {
            // laneResolves (when the caller supplied one) is the richer predicate that also
            // accounts for a HostedPluginModule's parameter set having changed shape; unset, this is
            // just the uuid-only check.
            const bool resolved = laneResolves ? laneResolves(lane.nodeUuid, lane.paramId, lane.paramIndexHint)
                                               : uuidResolves(lane.nodeUuid);
            const bool laneOrphaned = lane.nodeUuid.isNotEmpty() && !resolved;
            plan.laneOrphaned.push_back(laneOrphaned);
            if (laneOrphaned != lane.orphaned)
                anyChanged = true;
        }
        plans.push_back(std::move(plan));
    }

    if (!anyChanged)
        return false; // every flag already matches: no bump, no notification

    return applyMutation([&] {
        for (size_t i = 0; i < tracks.size(); ++i) {
            tracks[i].orphaned = plans[i].orphaned;
            for (size_t j = 0; j < tracks[i].lanes.size(); ++j)
                tracks[i].lanes[j].orphaned = plans[i].laneOrphaned[j];
        }
        return true;
    });
}

bool TimelineDoc::rebindLane(LaneId id, const juce::String& newNodeUuid) {
    Track* owner = nullptr;
    auto* lane = findLane(id, &owner);
    if (lane == nullptr || newNodeUuid.isEmpty())
        return false;

    // Doc-wide one-lane-per-parameter invariant: reject if some OTHER lane already owns
    // (newNodeUuid, this lane's paramId). The lane being rebound is allowed to "collide" with
    // itself (rebinding to the uuid it already has is a legal no-op path below).
    if (auto* existing = findLaneForParam(newNodeUuid, lane->paramId))
        if (existing->id != id)
            return false;

    if (lane->nodeUuid == newNodeUuid && !lane->orphaned)
        return true; // already bound here and already resolved: no-op, no bump

    return applyMutation([&] {
        lane->nodeUuid = newNodeUuid;
        // Optimistic, same reasoning as setTrackBinding: the next reconcileBindings re-derives
        // whether this uuid actually resolves.
        lane->orphaned = false;
        return true;
    });
}

// FRO296: whichever side HAS a lane gets the OTHER paramId; a side with no lane stays that way (no
// lane is created) -- so a mixer send-slot swap (docs/mixer/sends-and-buses.md#reordering-sends)
// carries an automation lane along with it, without a delete+recreate that would lose its points or
// record mode. ONE revision bump / one Listener::timelineChanged call for BOTH retargets together,
// so a caller wrapping this in AppUndoManager::recordGraphTimelineAndMacroChange (the send-reorder
// caller) sees it as part of the same combined edit, not a second one.
bool TimelineDoc::swapLaneParams(const juce::String& nodeUuid, const juce::String& paramA, const juce::String& paramB) {
    if (paramA.isEmpty() || paramB.isEmpty() || paramA == paramB)
        return false;

    // findLaneForParam is a plain lookup (no `id` to disambiguate a self-collision the way
    // rebindLane's caller-supplied id does), which is fine here: paramA and paramB are guaranteed
    // distinct above, so at most one lane can match each.
    auto* laneA = findLaneForParam(nodeUuid, paramA);
    auto* laneB = findLaneForParam(nodeUuid, paramB);
    if (laneA == nullptr && laneB == nullptr)
        return false; // neither side has a lane: nothing to swap

    return applyMutation([&] {
        if (laneA != nullptr)
            laneA->paramId = paramB;
        if (laneB != nullptr)
            laneB->paramId = paramA;
        return true;
    });
}

} // namespace synth
