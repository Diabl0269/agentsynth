// AutomationLaneEditor -- an undo or redo that moves points: the dots glide from where they were drawn to their
// restored place (KeyedPointTween), matched by order (a move never lets points cross). What was selected stays
// selected at its restored beat, the keyboard cursor with it; when that puts the selection outside the view, the view
// follows it there, gliding with the dot as a key hold does, or at once when nothing glides (Reduce Motion,
// Animations off, a dot already off screen). A restore that only changes segment shapes melts the curve, as the menu
// pick did. The class is declared in AutomationLaneEditor.h.

#include "AppUndoManager.h"
#include "AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include "UI/Timeline/ScrollIntoView.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

bool AutomationLaneEditor::restoreSinceLastSync() {
    return undoManager_ != nullptr && restoreWatch_.take(undoManager_->isRestoring(), undoManager_->getRestoreSerial());
}

// Only a same-count change is a move; adding or removing points is LanePointGlide's. Every start is read before any
// key is erased, because one point's new beat can be another's old key.
void AutomationLaneEditor::glideRestoredPoints(const std::vector<LaneBreakpoint>& before,
                                               const std::vector<LaneBreakpoint>& now) {
    if (before.size() != now.size())
        return;
    struct Move {
        KeyedPointTween::Key from, to;
        juce::Point<float> drawnFrom, target;
    };
    std::vector<Move> moves;
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (before[i].beat == now[i].beat && before[i].value == now[i].value)
            continue;
        const auto oldKey = lane_key::tweenKey(before[i].beat);
        moves.push_back({oldKey, lane_key::tweenKey(now[i].beat), pointTween_.drawnAt(oldKey, pointPosition(before[i])),
                         pointPosition(now[i])});
    }
    for (const auto& move : moves)
        pointTween_.erase(move.from);
    for (const auto& move : moves)
        pointTween_.retarget(move.to, move.drawnFrom, move.target, true);
}

// A restore that added or removed points has no pairing, so only the points still there stay selected.
void AutomationLaneEditor::carrySelectionThroughRestore(const std::vector<LaneBreakpoint>& before,
                                                        const std::vector<LaneBreakpoint>& now) {
    if (before.size() == now.size()) {
        std::vector<double> selected;
        std::optional<double> cursor;
        bool changed = false;
        for (std::size_t i = 0; i < now.size(); ++i) {
            if (selection_.contains(before[i].beat)) {
                selected.push_back(now[i].beat);
                changed = changed || before[i].beat != now[i].beat;
            }
            if (selection_.getCursor() == before[i].beat)
                cursor = now[i].beat;
        }
        if (changed) {
            selection_.setSelection(selected);
            selection_.setCursor(cursor);
        }
    }
    std::vector<double> alive;
    alive.reserve(now.size());
    for (const auto& p : now)
        alive.push_back(p.beat);
    selection_.retainOnly(alive);
}

// Only the shapes changed (same points, same places): the restored curve melts in over the changed segments.
void AutomationLaneEditor::meltRestoredCurves(const std::vector<LaneBreakpoint>& before,
                                              const std::vector<LaneBreakpoint>& now) {
    if (before.size() != now.size())
        return;
    std::optional<double> start;
    double end = 0.0;
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (before[i].beat != now[i].beat || before[i].value != now[i].value)
            return;
        if (before[i].curve == now[i].curve && before[i].tension == now[i].tension)
            continue;
        if (!start)
            start = now[i].beat;
        end = i + 1 < now.size() ? now[i + 1].beat : now[i].beat;
    }
    if (start)
        meltRangeNext(*start, end);
}

// The view moves only when a selected point of this lane moved and its restored place is out of sight
// (firstBeatShowing). The follow rides the dot that leads the way the view goes, by its drawn place, so the dot and the
// view glide together and the dot comes to rest at the margin line; a dot that starts off screen has no drawn place to
// ride, so the view lands at once.
void AutomationLaneEditor::followRestoredSelection(const std::vector<LaneBreakpoint>& before,
                                                   const std::vector<LaneBreakpoint>& now) {
    if (before.size() != now.size() || getWidth() <= 0 || viewState_.pixelsPerBeat <= 0.0)
        return;
    std::optional<std::size_t> first, last;
    bool moved = false;
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (!selection_.contains(now[i].beat))
            continue;
        first = first.value_or(i);
        last = i;
        moved = moved || before[i].beat != now[i].beat;
    }
    if (!moved || !first)
        return;

    const double visibleBeats = (double)getWidth() / viewState_.pixelsPerBeat;
    const double from = viewState_.firstVisibleBeat;
    const double target = firstBeatShowing(from, visibleBeats, now[*first].beat, now[*last].beat);
    if (std::abs(target - from) < 1e-9)
        return;

    const int direction = target < from ? -1 : 1;
    const std::size_t lead = direction < 0 ? *first : *last;
    const auto key = lane_key::tweenKey(now[lead].beat);
    const double drawnX = viewState_.beatToX(now[lead].beat) + (double)pointTween_.offsetFor(key).x;
    const bool fits = now[*last].beat - now[*first].beat <= (1.0 - kScrollIntoViewMargin) * visibleBeats;
    if (fits && pointTween_.isAnimating() && drawnX >= 0.0 && drawnX <= (double)getWidth()) {
        keyFollow_ = {true, now[lead].beat, key, direction};
        followKeyDragDot();
        return;
    }
    viewState_.firstVisibleBeat = target;
    viewScrolledByDrag();
    repaint();
}

} // namespace synth::ui
