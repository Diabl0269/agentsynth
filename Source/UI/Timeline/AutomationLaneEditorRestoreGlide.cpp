// AutomationLaneEditor -- an undo or redo that moves points: the dots glide from where they were drawn to their
// restored place (KeyedPointTween), matched by order. The class is declared in AutomationLaneEditor.h.

#include "AppUndoManager.h"
#include "AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"

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
        pointTween_.retarget(move.to, move.drawnFrom, move.target);
}

} // namespace synth::ui
