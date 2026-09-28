// PianoRollControllerLanes — the CC lane's gestures (declared in PianoRollControllerLanes.h):
//   plain drag on empty lane -> FREEHAND (raw samples, thinned on mouse-up, replace the dragged span)
//   plain click on empty lane -> adds one point at the snapped beat
//   drag a handle            -> MOVE it (beat snapped, value follows the pointer)
//   Shift+drag               -> LINE (replaces the span with its two snapped endpoints)
//   Alt+click/drag           -> ERASE every handle the pointer passes over
// Each previews into preview_ (a full point list) and commits it with ONE
// TimelineDoc::setControllerLanePoints call, which also creates the lane on its first stroke.

#include "PianoRollControllerLanes.h"

#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

std::vector<synth::ControllerPoint> sortedByBeat(std::vector<synth::ControllerPoint> points) {
    std::stable_sort(points.begin(), points.end(),
                     [](const synth::ControllerPoint& a, const synth::ControllerPoint& b) { return a.beat < b.beat; });
    return points;
}

bool samePoints(const std::vector<synth::ControllerPoint>& a, const std::vector<synth::ControllerPoint>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
               return x.beat == y.beat && x.value == y.value && x.curve == y.curve;
           });
}

} // namespace

void PianoRollControllerLanes::beginCcGesture(juce::Point<int> pos, const juce::ModifierKeys& mods) {
    // Resolved against the doc BEFORE gesture_ is set, so handleAt's indices are origPoints_ indices.
    const auto handle = handleAt(pos);
    origPoints_ = docPoints();
    preview_ = origPoints_;
    erased_.clear();
    stroke_.clear();
    movingIndex_.reset();

    const int curve = lanes::defaultCurveFor(selectedLane_);
    if (mods.isAltDown()) {
        gesture_ = Gesture::CcErase;
        dragCcGesture(pos);
    } else if (mods.isShiftDown()) {
        gesture_ = Gesture::CcLine;
        anchorBeat_ = clipBeatAtX(pos.x, true);
        anchorValue_ = valueForY(pos.y);
        dragCcGesture(pos);
    } else if (handle) {
        gesture_ = Gesture::CcMove;
        movingIndex_ = handle;
    } else {
        gesture_ = Gesture::CcFreehand;
        stroke_.push_back({clipBeatAtX(pos.x, false), valueForY(pos.y), curve});
        preview_ = lanes::replaceSpan(origPoints_, stroke_.front().beat, stroke_.front().beat, stroke_);
    }
    repaint();
}

void PianoRollControllerLanes::dragCcGesture(juce::Point<int> pos) {
    const int curve = lanes::defaultCurveFor(selectedLane_);
    switch (gesture_) {
    case Gesture::CcErase: {
        // Everything inside the box the pointer swept since the last event, padded by the handle size.
        const auto swept =
            juce::Rectangle<int>::leftTopRightBottom(std::min(lastPos_.x, pos.x), std::min(lastPos_.y, pos.y),
                                                     std::max(lastPos_.x, pos.x), std::max(lastPos_.y, pos.y))
                .expanded(kHandleHitPx);
        for (size_t i = 0; i < origPoints_.size(); ++i)
            if (swept.contains(xForClipBeat(origPoints_[i].beat), yForValue(origPoints_[i].value)))
                erased_.insert(i);
        preview_.clear();
        for (size_t i = 0; i < origPoints_.size(); ++i)
            if (erased_.count(i) == 0)
                preview_.push_back(origPoints_[i]);
        break;
    }
    case Gesture::CcLine: {
        const double beat = clipBeatAtX(pos.x, true);
        std::vector<synth::ControllerPoint> ends = {{anchorBeat_, anchorValue_, curve},
                                                    {beat, valueForY(pos.y), curve}};
        preview_ = lanes::replaceSpan(origPoints_, anchorBeat_, beat, sortedByBeat(ends));
        break;
    }
    case Gesture::CcMove: {
        if (!movingIndex_ || *movingIndex_ >= origPoints_.size())
            return;
        auto rest = origPoints_;
        const auto moved =
            synth::ControllerPoint{clipBeatAtX(pos.x, true), valueForY(pos.y), rest[*movingIndex_].curve};
        rest.erase(rest.begin() + (std::ptrdiff_t)*movingIndex_);
        preview_ = lanes::replaceSpan(rest, moved.beat, moved.beat, {moved});
        break;
    }
    case Gesture::CcFreehand: {
        stroke_.push_back({clipBeatAtX(pos.x, false), valueForY(pos.y), curve});
        const auto sorted = sortedByBeat(stroke_);
        preview_ = lanes::replaceSpan(origPoints_, sorted.front().beat, sorted.back().beat, sorted);
        break;
    }
    default:
        return;
    }
    repaint();
}

void PianoRollControllerLanes::endCcGesture(bool dragged) {
    if (gesture_ == Gesture::CcFreehand && !stroke_.empty()) {
        const int curve = lanes::defaultCurveFor(selectedLane_);
        if (!dragged) {
            // A click, not a stroke: one point, on the grid like every other placed point.
            const double beat = clipBeatAtX(anchor_.x, true);
            preview_ = lanes::replaceSpan(origPoints_, beat, beat, {{beat, stroke_.front().value, curve}});
        } else {
            const auto sorted = sortedByBeat(stroke_);
            preview_ =
                lanes::replaceSpan(origPoints_, sorted.front().beat, sorted.back().beat,
                                   lanes::thinStroke(sorted, curve, 127.0 / std::max(1, getValueArea().getHeight())));
        }
    }
    const auto result = preview_;
    const bool changed = !samePoints(result, origPoints_);
    gesture_ = Gesture::None; // before the commit: its timelineChanged repaints from the doc
    preview_.clear();
    stroke_.clear();
    erased_.clear();
    movingIndex_.reset();
    if (changed)
        commitPoints(result);
    repaint();
}

} // namespace synth::ui
