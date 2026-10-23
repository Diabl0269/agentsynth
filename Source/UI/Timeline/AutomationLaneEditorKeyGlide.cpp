// AutomationLaneEditor -- the keyboard drag: a held Alt+Left/Right or Up/Down moves the selection like a mouse drag of
// its points (same preview state, one undo step on release), and a discrete jump of the drawn points eases. The class
// is declared in AutomationLaneEditor.h.

#include "AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace synth::ui {

namespace {
std::vector<double> beatsOf(const std::vector<LaneBreakpoint>& points) {
    std::vector<double> beats;
    beats.reserve(points.size());
    for (const auto& p : points)
        beats.push_back(p.beat);
    return beats;
}
} // namespace

juce::Point<float> AutomationLaneEditor::pointPosition(const LaneBreakpoint& point) const {
    return pointMapper()(point.beat, point.value);
}

// The drawn points trail their final place by an eased offset (KeyedPointTween): each continues from where it is drawn
// now, so a second step in quick succession does not restart from the old spot.
void AutomationLaneEditor::easeMovedPoints(const std::vector<LaneBreakpoint>& from,
                                           const std::vector<LaneBreakpoint>& to, const std::vector<double>& keysFrom,
                                           const std::vector<double>& keysTo) {
    for (std::size_t i = 0; i < from.size() && i < to.size(); ++i) {
        const auto oldKey = lane_key::tweenKey(keysFrom[i]);
        const auto newKey = lane_key::tweenKey(keysTo[i]);
        const auto drawnFrom = pointTween_.drawnAt(oldKey, pointPosition(from[i]));
        if (oldKey != newKey)
            pointTween_.erase(oldKey);
        pointTween_.retarget(newKey, drawnFrom, pointPosition(to[i]));
    }
}

double AutomationLaneEditor::keyDragSpeedFactor() const {
    const auto& host = keyHold_.host;
    const bool shift = host.isShiftDown && host.isShiftDown();
    const bool alt = host.isAltDown && host.isAltDown();
    if (alt && shift)
        return lane_key::kFineScale;
    return shift && !keyDrag_.time ? lane_key::kValueShiftSpeedFactor : 1.0;
}

// The tap is the first step of the drag, so a tap and a hold are one undo step. The grabbed point is the keyboard
// cursor's (or the first selected), the same fields a mouse drag of it would fill.
bool AutomationLaneEditor::beginKeyDrag(int keyCode, const std::vector<LaneBreakpoint>& picked, double beatDirection,
                                        double valueDirection, NudgeSize size) {
    if (dragMode_ != DragMode::None)
        return true;
    const bool time = beatDirection != 0.0;
    const double fine = size == NudgeSize::Fine ? lane_key::kFineScale : 1.0;
    keyDrag_ = {};
    keyDrag_.time = time;
    keyDrag_.sign = (time ? beatDirection : valueDirection) < 0.0 ? -1 : 1;
    keyDrag_.snapped = time && viewState_.divisionBeats(currentBeatsPerBar()) > 0.0;
    keyDrag_.unit = keyStepBeats() * fine;

    const LaneBreakpoint* grabbed = &picked.front();
    for (const auto& p : picked)
        if (selection_.getCursor() == p.beat)
            grabbed = &p;
    dragMode_ = DragMode::MoveHandle;
    dragPoints_ = picked;
    dragOriginalBeat_ = previewBeat_ = grabbed->beat;
    dragOriginalValue_ = previewValue_ = grabbed->value;

    namespace k = lane_key;
    const KeyGlideConfig config =
        time ? KeyGlideConfig{k::kTimeStartBeatsPerSecond, k::kTimeMaxBeatsPerSecond, k::kTapMs, k::kRampMs}
             : KeyGlideConfig{k::kValueStartRangePerSecond, k::kValueMaxRangePerSecond, k::kTapMs, k::kRampMs};
    const double valueTap = size == NudgeSize::Coarse ? k::kCoarseValueStep : k::kFineValueStep * fine;
    keyHold_.begin(keyCode, config, time ? keyDrag_.unit : valueTap);
    applyKeyDrag(true);
    return true;
}

// Sets the preview from how far the key has moved things. With snap on a time move is whole steps, so it jumps from
// grid line to grid line and each jump eases; a free move and any value move follow the glide continuously.
void AutomationLaneEditor::applyKeyDrag(bool tapStep) {
    const auto* lane = dragMode_ == DragMode::MoveHandle && doc_ != nullptr ? doc_->getLane(laneId_) : nullptr;
    if (lane == nullptr)
        return;
    const auto before = dragMovedPoints();
    const double distance = keyHold_.distance();
    if (keyDrag_.time) {
        double delta = keyDrag_.sign * distance;
        if (keyDrag_.snapped)
            delta = keyDrag_.sign * std::floor(distance / keyDrag_.unit + 1e-9) * keyDrag_.unit;
        previewBeat_ = dragOriginalBeat_ + clampBeatDelta(dragPoints_, delta);
    } else {
        previewValue_ =
            dragOriginalValue_ + keyDrag_.sign * distance * (double)(lane->range.maxValue - lane->range.minValue);
    }
    showBubbleAt(previewBeat_, clampValue(previewValue_));
    if (tapStep || keyDrag_.snapped) {
        const auto keys = beatsOf(dragPoints_);
        easeMovedPoints(before, dragMovedPoints(), keys, keys);
    }
    refreshStretchBox();
    repaint();
}

// Commits the preview as ONE edit (nothing when it never moved), then hands the drawn offsets still running to the
// points' new beats so a quick tap keeps easing across the commit.
void AutomationLaneEditor::endKeyDrag() {
    if (dragMode_ != DragMode::MoveHandle)
        return;
    const bool cancelled = keyDrag_.cancelled;
    keyDrag_.cancelled = false;
    const auto keys = beatsOf(dragPoints_);
    std::vector<juce::Point<float>> offsets;
    for (const double beat : keys)
        offsets.push_back(pointTween_.offsetFor(lane_key::tweenKey(beat)));
    const auto landed = dragMovedPoints();

    if (!cancelled)
        commitPointMove();
    dragMode_ = DragMode::None;
    dragPoints_.clear();
    bubble_.hide();
    updateMouseCursor();
    refreshStretchBox();

    for (const double beat : keys)
        pointTween_.erase(lane_key::tweenKey(beat));
    if (!cancelled)
        for (std::size_t i = 0; i < landed.size(); ++i)
            if (offsets[i] != juce::Point<float>())
                pointTween_.retarget(lane_key::tweenKey(landed[i].beat), pointPosition(landed[i]) + offsets[i],
                                     pointPosition(landed[i]));
    repaint();
}

void AutomationLaneEditor::cancelKeyDrag() {
    if (!keyHold_.active())
        return;
    keyDrag_.cancelled = true;
    keyHold_.end();
    pointTween_.clear();
}

bool AutomationLaneEditor::keyStateChanged(bool) {
    keyHold_.checkKeyUp();
    return false;
}

// The line is drawn through the gliding dots: each point moves by its dot's offset (the key the dots are drawn with,
// see paintHandles) and the kernel evaluates the moved points, so the line meets every dot and keeps its tensions.
std::vector<LaneBreakpoint> AutomationLaneEditor::tweenedPoints(const std::vector<LaneBreakpoint>& points) const {
    std::map<double, double> keyBeat; // a point's beat -> the beat whose key holds its offset
    if (dragMode_ == DragMode::MoveHandle) {
        const auto now = dragMovedPoints();
        for (std::size_t i = 0; i < dragPoints_.size() && i < now.size(); ++i)
            keyBeat[now[i].beat] = dragPoints_[i].beat;
    } else if (dragMode_ == DragMode::Stretch) {
        const auto& result = stretch_.result();
        for (std::size_t i = 0; i < result.moved.size(); ++i)
            keyBeat[result.moved[i].beat] = stretch_.original()[i].beat;
        for (std::size_t i = 0; i < result.pushed.size(); ++i)
            keyBeat[result.pushed[i].beat] = result.pushedFrom[i];
    }
    std::vector<LaneBreakpoint> drawn = points;
    for (auto& point : drawn) {
        const auto found = keyBeat.find(point.beat);
        const auto offset =
            pointTween_.offsetFor(lane_key::tweenKey(found != keyBeat.end() ? found->second : point.beat));
        if (offset.isOrigin())
            continue;
        const auto at = pointPosition(point) + offset;
        point.beat = viewState_.xToBeat((double)at.x);
        point.value = yToValue((double)at.y);
    }
    std::stable_sort(drawn.begin(), drawn.end(),
                     [](const LaneBreakpoint& a, const LaneBreakpoint& b) { return a.beat < b.beat; });
    return drawn;
}

juce::Path AutomationLaneEditor::getDrawnCurvePathForTest() const {
    const auto* lane = doc_ != nullptr ? doc_->getLane(laneId_) : nullptr;
    return lane != nullptr ? curvePath(tweenedPoints(lane->points), *lane) : juce::Path();
}

} // namespace synth::ui
