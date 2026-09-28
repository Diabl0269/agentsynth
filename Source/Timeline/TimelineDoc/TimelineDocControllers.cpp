// Concern: clip controller lanes — the piano roll's velocity lane (batched velocity edits) and the
// per-clip MIDI CC lanes (Clip::controllers). See docs/timeline/piano-roll-lanes.md.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <map>

namespace synth {

using namespace detail;

namespace {

ClipControllerLane* findControllerLane(Clip& clip, int ccNumber) {
    for (auto& lane : clip.controllers)
        if (lane.ccNumber == ccNumber)
            return &lane;
    return nullptr;
}

} // namespace

namespace detail {

// splitClip's CC half. Each lane is cut so BOTH halves keep playing exactly what the unsplit clip
// played: the left half gains a boundary point at `atBeat` and the right half one at beat 0, both
// carrying the value (and the shaping curve) in force at the cut. Without them a Linear segment that
// crosses the cut would lose an endpoint and flatten. Points after the cut are re-based onto the
// right clip's own start. A lane with no points stays a lane (with no points) on both sides.
//
// Neither half may exceed kMaxControllerPointsPerLane (a saved project would otherwise refuse to
// load). A half can only overflow when EVERY point of a full lane sits on its side of the cut — and
// then the boundary point is redundant (the lane is flat across the cut: after its last point on
// the left, before its first on the right), so it is simply left out. Lossless; the split is never
// refused for this.
void splitControllerLanes(const std::vector<ClipControllerLane>& lanes, double atBeat,
                          std::vector<ClipControllerLane>& leftOut, std::vector<ClipControllerLane>& rightOut) {
    leftOut.clear();
    rightOut.clear();
    for (const auto& lane : lanes) {
        ClipControllerLane left;
        ClipControllerLane right;
        left.ccNumber = right.ccNumber = lane.ccNumber;
        if (!lane.points.empty()) {
            int curve = static_cast<int>(BreakpointCurve::Linear);
            const double value = controllerValueAt(lane.points, atBeat, curve);
            for (const auto& point : lane.points)
                if (point.beat < atBeat)
                    left.points.push_back(point);
            if ((int)left.points.size() < TimelineDoc::kMaxControllerPointsPerLane)
                left.points.push_back({atBeat, value, curve});
            right.points.push_back({0.0, value, curve});
            for (const auto& point : lane.points)
                if (point.beat > atBeat)
                    right.points.push_back({point.beat - atBeat, point.value, point.curve});
            if ((int)right.points.size() > TimelineDoc::kMaxControllerPointsPerLane)
                right.points.erase(right.points.begin());
        }
        leftOut.push_back(std::move(left));
        rightOut.push_back(std::move(right));
    }
}

// joinClips' CC half: b's lanes are re-based by `rebaseB` (b.start - a.start) and merged into a's by
// ccNumber; a CC only b had becomes a lane of the joined clip. Same-beat collisions resolve the
// fromVar way (b's point, being later in the list, wins). Returns false — and the join is refused —
// if any merged lane would exceed kMaxControllerPointsPerLane.
bool mergeControllerLanes(const std::vector<ClipControllerLane>& a, const std::vector<ClipControllerLane>& b,
                          double rebaseB, std::vector<ClipControllerLane>& out) {
    out = a;
    for (const auto& laneB : b) {
        ClipControllerLane* target = nullptr;
        for (auto& lane : out)
            if (lane.ccNumber == laneB.ccNumber)
                target = &lane;
        if (target == nullptr) {
            ClipControllerLane created;
            created.ccNumber = laneB.ccNumber;
            const auto pos = std::lower_bound(out.begin(), out.end(), created, controllerLaneLess);
            target = &*out.insert(pos, std::move(created));
        }
        for (const auto& point : laneB.points)
            target->points.push_back({point.beat + rebaseB, point.value, point.curve});
        target->points = normalisedControllerPoints(std::move(target->points));
        if (static_cast<int>(target->points.size()) > TimelineDoc::kMaxControllerPointsPerLane)
            return false;
    }
    return true;
}

} // namespace detail

// The velocity lane's ONE commit primitive: a ramp or a freehand stroke touches many notes, and the
// whole gesture must cost one revision bump / one timelineChanged (one snapshot republish), not one
// per note. Validates everything before mutating anything, so a bad entry leaves the doc untouched.
// A repeated id is legal and the LAST entry wins. Velocity is not part of the note sort key, so no
// re-sort is needed. A call where every note already has the asked-for velocity is a no-op.
bool TimelineDoc::setNoteVelocities(const std::vector<std::pair<NoteId, int>>& velocities) {
    // Keyed by note so a repeated id collapses to its LAST entry before "does anything change" is
    // decided: a list that sets a note to 90 and then back to its current value changes nothing.
    std::map<MidiNote*, int> targets;
    for (const auto& [id, velocity] : velocities) {
        if (velocity < 1 || velocity > 127)
            return false;
        auto* note = findNote(id);
        if (note == nullptr)
            return false;
        targets[note] = velocity;
    }
    const bool anyChange = std::any_of(targets.begin(), targets.end(),
                                       [](const auto& entry) { return entry.first->velocity != entry.second; });
    if (!anyChange)
        return true;

    return applyMutation([&] {
        for (const auto& [note, velocity] : targets)
            note->velocity = velocity;
        return true;
    });
}

// Lanes stay sorted by ccNumber so a reader (the snapshot build, the lane selector menu) walks them
// in a stable order. Adding a lane that already exists is a successful no-op, like addLane.
bool TimelineDoc::addControllerLane(ClipId clipId, int ccNumber) {
    if (!isValidCcNumber(ccNumber))
        return false;
    auto* clip = findClip(clipId);
    if (clip == nullptr)
        return false;
    if (findControllerLane(*clip, ccNumber) != nullptr)
        return true;

    return applyMutation([&] {
        ClipControllerLane lane;
        lane.ccNumber = ccNumber;
        const auto pos = std::lower_bound(clip->controllers.begin(), clip->controllers.end(), lane, controllerLaneLess);
        clip->controllers.insert(pos, std::move(lane));
        return true;
    });
}

bool TimelineDoc::removeControllerLane(ClipId clipId, int ccNumber) {
    auto* clip = findClip(clipId);
    if (clip == nullptr)
        return false;
    auto* lane = findControllerLane(*clip, ccNumber);
    if (lane == nullptr)
        return false;

    return applyMutation([&] {
        clip->controllers.erase(clip->controllers.begin() + (lane - clip->controllers.data()));
        return true;
    });
}

// Replace-all rather than incremental add/remove: every lane gesture (a freehand stroke, a line, a
// dragged handle, an eraser sweep, "clear lane") previews its full result locally and commits it in
// one call, so one gesture == one mutation == one undo step, and the lane is CREATED here when absent
// so the first stroke on a lane the user merely picked from the selector is still one mutation.
//
// Validation is all-or-nothing (non-finite or negative beat, non-finite value, a curve other than
// Hold/Linear, more than kMaxControllerPointsPerLane points). A valid list is then normalised
// exactly like fromVar normalises a file: sorted by beat, same-beat duplicates collapsed (last wins)
// and values clamped into 0..127 — a hand on the lane means "as far as this goes". Beats past the
// clip's end are legal (a clip can be lengthened later); the snapshot only plays the clip window.
// An empty list on a missing lane is a no-op, as is a list identical to what the lane already holds.
bool TimelineDoc::setControllerLanePoints(ClipId clipId, int ccNumber, const std::vector<ControllerPoint>& points) {
    if (!isValidCcNumber(ccNumber))
        return false;
    auto* clip = findClip(clipId);
    if (clip == nullptr)
        return false;
    if (static_cast<int>(points.size()) > kMaxControllerPointsPerLane)
        return false;
    for (const auto& point : points)
        if (!isValidControllerPoint(point))
            return false;

    auto normalised = normalisedControllerPoints(points);
    auto* lane = findControllerLane(*clip, ccNumber);
    if (lane == nullptr && normalised.empty())
        return true;
    if (lane != nullptr && lane->points.size() == normalised.size() &&
        std::equal(lane->points.begin(), lane->points.end(), normalised.begin(),
                   [](const ControllerPoint& a, const ControllerPoint& b) {
                       return a.beat == b.beat && a.value == b.value && a.curve == b.curve;
                   }))
        return true;

    return applyMutation([&] {
        if (lane == nullptr) {
            ClipControllerLane created;
            created.ccNumber = ccNumber;
            const auto pos =
                std::lower_bound(clip->controllers.begin(), clip->controllers.end(), created, controllerLaneLess);
            lane = &*clip->controllers.insert(pos, std::move(created));
        }
        lane->points = std::move(normalised);
        return true;
    });
}

const ClipControllerLane* TimelineDoc::getControllerLane(ClipId clipId, int ccNumber) const {
    auto* clip = const_cast<TimelineDoc*>(this)->findClip(clipId);
    return clip != nullptr ? findControllerLane(*clip, ccNumber) : nullptr;
}

} // namespace synth
