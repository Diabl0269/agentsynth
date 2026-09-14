// Concern: automation lanes, plus binding reconciliation for tracks and lanes.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>

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

} // namespace synth
