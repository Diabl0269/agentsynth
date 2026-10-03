// AutomationLaneEditor -- point selection: the glue between the editor's gestures and keys and LanePointSelection,
// LanePointEdits and LanePointGlide. The class is declared in AutomationLaneEditor.h.

#include "AutomationLaneEditor.h"
#include "Transport/TransportService.h"
#include "UI/PianoRoll/NoteAccessibilityText.h"
#include "UI/Timeline/TimelineBeatsPerBar.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kFineValueStep = 0.01;   // of the lane's range, per Up/Down press
constexpr double kCoarseValueStep = 0.10; // with Shift
constexpr double kFreeBeatStep = 0.25;    // a sixteenth, when snap is off

bool isPlain(const juce::KeyPress& key, int code, int mods = 0) {
    return key == juce::KeyPress(code, juce::ModifierKeys(mods), 0);
}

std::vector<double> beatsOf(const std::vector<LaneBreakpoint>& points) {
    std::vector<double> beats;
    beats.reserve(points.size());
    for (const auto& p : points)
        beats.push_back(p.beat);
    return beats;
}
} // namespace

const std::vector<LaneBreakpoint>* AutomationLaneEditor::lanePoints() const {
    const auto* lane = doc_ != nullptr && laneId_.isValid() ? doc_->getLane(laneId_) : nullptr;
    return lane != nullptr ? &lane->points : nullptr;
}

LanePointMapper AutomationLaneEditor::pointMapper() const {
    return [this](double beat, double value) {
        return juce::Point<float>((float)viewState_.beatToX(beat), (float)valueToY(value));
    };
}

LaneEditTarget AutomationLaneEditor::editTarget() const { return {doc_, undoManager_, laneId_}; }

//==============================================================================
// ---- Following the doc ----

// Runs on every doc notification the lane pool forwards, and lazily from paint and the input entry points so an
// editor without a pool (or between notifications) never shows a stale selection. A selected point that is gone
// leaves the selection; a change that only removed or only added points glides.
void AutomationLaneEditor::syncToDoc() {
    if (doc_ == nullptr)
        return;
    const auto revision = doc_->getRevision();
    if (revision == syncedRevision_)
        return;
    const bool first = syncedRevision_ < 0;
    syncedRevision_ = revision;

    const auto* points = lanePoints();
    selection_.retainOnly(points != nullptr ? beatsOf(*points) : std::vector<double>{});
    std::vector<LaneBreakpoint> now = points != nullptr ? *points : std::vector<LaneBreakpoint>{};
    if (first)
        glide_.reset(std::move(now));
    else
        glide_.pointsChanged(std::move(now));
    refreshDescription();
    closeValueFieldIfPointGone();
}

void AutomationLaneEditor::laneDocChanged() {
    syncToDoc();
    repaint();
}

void AutomationLaneEditor::selectionChanged() {
    refreshStretchBox();
    refreshDescription();
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

//==============================================================================
// ---- Accessibility ----

juce::String AutomationLaneEditor::describePoints() const {
    const auto* points = lanePoints();
    const int total = points != nullptr ? (int)points->size() : 0;
    if (total == 0)
        return "Automation curve with no points. Double-click to add one.";

    juce::String text = juce::String(total) + (total == 1 ? " point" : " points") + ". ";
    const int selected = selection_.size();
    if (selected == 0) {
        text += "None selected.";
    } else if (const auto cursor = selection_.getCursor(); selected == 1 && cursor.has_value()) {
        for (const auto& p : *points)
            if (p.beat == *cursor)
                text += "Selected: " + describeNotePosition(p.beat, currentBeatsPerBar()) + ", value " +
                        valueText(p.value) + ".";
    } else {
        text += juce::String(selected) + " selected.";
    }
    text += " Arrow keys nudge the selection, Alt with Left or Right picks another point, Delete removes it.";
    if (selected >= 2)
        text += " Alt and Shift with Left or Right stretch or squeeze the selection in time, with Up or Down they "
                "scale its values.";
    return text;
}

void AutomationLaneEditor::refreshDescription() {
    const auto text = describePoints();
    if (text == getDescription())
        return;
    setDescription(text);
    if (isShowing())
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::titleChanged);
}

//==============================================================================
// ---- Gestures ----

// A plain press on a point keeps a selection it is part of (so the group can be dragged) and otherwise selects just
// it; either way the drag then moves every selected point.
void AutomationLaneEditor::grabPoint(const HandleHit& hit) {
    if (!selection_.contains(hit.beat))
        selection_.setSelection({hit.beat});
    selection_.setCursor(hit.beat);
    dragMode_ = DragMode::MoveHandle;
    dragPoints_ = selection_.selectedPoints(*lanePoints());
    dragOriginalBeat_ = hit.beat;
    dragOriginalValue_ = hit.value;
    previewBeat_ = hit.beat;
    previewValue_ = hit.value;
    hoveredBeat_ = hit.beat;
    showBubbleAt(previewBeat_, previewValue_);
    updateMouseCursor();
}

// Pressing inside the stretch box moves the selection like grabbing one of its points: the drag's delta is how far the
// pointer has gone from where it was pressed, with the beat snapped on both ends so the block stays on the grid.
bool AutomationLaneEditor::beginBoxMove(juce::Point<int> pos) {
    const auto* points = lanePoints();
    if (points == nullptr || !stretchBoxVisible() || !stretchBox().contains(pos.toFloat()))
        return false;
    dragMode_ = DragMode::MoveHandle;
    dragPoints_ = selection_.selectedPoints(*points);
    dragOriginalBeat_ = snappedBeatAt(viewState_.xToBeat((double)pos.x));
    dragOriginalValue_ = clampValue(yToValue((double)pos.y));
    previewBeat_ = dragOriginalBeat_;
    previewValue_ = dragOriginalValue_;
    hoveredBeat_.reset();
    bubble_.hide();
    updateMouseCursor();
    return true;
}

std::vector<LaneBreakpoint> AutomationLaneEditor::dragMovedPoints() const {
    double minValue = 0.0, maxValue = 1.0;
    if (doc_ != nullptr && laneId_.isValid())
        if (const auto* lane = doc_->getLane(laneId_)) {
            minValue = lane->range.minValue;
            maxValue = lane->range.maxValue;
        }
    return movePoints(dragPoints_, previewBeat_ - dragOriginalBeat_, previewValue_ - dragOriginalValue_, minValue,
                      maxValue);
}

// The moved points replace the originals in ONE edit, so a multi-point drag is one revision bump and one undo step.
// The notification the edit fires prunes the selection (the old beats are gone), so the new beats are selected
// after it returns.
void AutomationLaneEditor::commitPointMove() {
    if (std::abs(previewBeat_ - dragOriginalBeat_) <= 1e-9 && std::abs(previewValue_ - dragOriginalValue_) <= 1e-9)
        return;
    const auto moved = dragMovedPoints();
    std::optional<double> cursor;
    for (std::size_t i = 0; i < dragPoints_.size(); ++i)
        if (selection_.getCursor() == dragPoints_[i].beat)
            cursor = moved[i].beat;
    commitPointEdit(editTarget(), beatsOf(dragPoints_), moved);
    selection_.setSelection(beatsOf(moved));
    selection_.setCursor(cursor);
}

//==============================================================================
// ---- Keys and the edit commands ----

bool AutomationLaneEditor::handleSelectionKey(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (selection_.isEmpty())
            return false;
        selection_.clear();
        selection_.setCursor(std::nullopt);
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return deleteSelectedPoints();
    if (stretchKey(key))
        return true;

    const auto* points = lanePoints();
    if (points == nullptr || points->empty())
        return false;
    using K = juce::KeyPress;
    if (isPlain(key, K::leftKey, juce::ModifierKeys::altModifier))
        return stepCursor(-1);
    if (isPlain(key, K::rightKey, juce::ModifierKeys::altModifier))
        return stepCursor(1);
    if (isPlain(key, K::leftKey) || isPlain(key, K::rightKey)) {
        const int direction = key.getKeyCode() == K::rightKey ? 1 : -1;
        if (selection_.isEmpty()) // Tabbing in: the first press picks the nearest end
            return stepCursor(direction);
        return nudgePoints(direction, 0, false);
    }
    if (isPlain(key, K::upKey) || isPlain(key, K::downKey))
        return nudgePoints(0, key.getKeyCode() == K::upKey ? 1 : -1, false);
    if (isPlain(key, K::upKey, juce::ModifierKeys::shiftModifier) ||
        isPlain(key, K::downKey, juce::ModifierKeys::shiftModifier))
        return nudgePoints(0, key.getKeyCode() == K::upKey ? 1 : -1, true);
    return false;
}

// One grid step, a sixteenth with snap off.
double AutomationLaneEditor::gridStepBeats() const {
    const double grid = viewState_.divisionBeats(currentBeatsPerBar());
    return grid > 0.0 ? grid : kFreeBeatStep;
}

// One grid step (a sixteenth with snap off) for the whole block, stopping at beat 0 as a unit; Up/Down move every value
// by a fraction of the lane's range, each clamped on its own. Consumed whenever something is selected, even when a
// limit left nothing to do.
bool AutomationLaneEditor::nudgePoints(double beatDirection, double valueDirection, bool coarse) {
    const auto* points = lanePoints();
    if (points == nullptr || selection_.isEmpty())
        return false;
    const auto picked = selection_.selectedPoints(*points);
    if (picked.empty())
        return false;

    const auto& range = doc_->getLane(laneId_)->range;
    const double beatDelta = clampBeatDelta(picked, beatDirection * gridStepBeats());
    const double valueDelta =
        valueDirection * (coarse ? kCoarseValueStep : kFineValueStep) * (double)(range.maxValue - range.minValue);
    const auto moved = movePoints(picked, beatDelta, valueDelta, range.minValue, range.maxValue);

    bool same = true;
    for (std::size_t i = 0; i < picked.size(); ++i)
        same = same && picked[i].beat == moved[i].beat && picked[i].value == moved[i].value;
    if (same)
        return true;

    std::optional<double> cursor;
    for (std::size_t i = 0; i < picked.size(); ++i)
        if (selection_.getCursor() == picked[i].beat)
            cursor = moved[i].beat;
    commitPointEdit(editTarget(), beatsOf(picked), moved);
    selection_.setSelection(beatsOf(moved));
    selection_.setCursor(cursor);
    return true;
}

void AutomationLaneEditor::selectOnly(const LaneBreakpoint& point) {
    selection_.setSelection({point.beat});
    selection_.setCursor(point.beat);
    showBubbleAt(point.beat, point.value);
}

// Moves the keyboard cursor to the neighbouring point and selects only it; with no cursor yet the first press lands
// on the nearest end. At the last point it stays put (still consumed).
bool AutomationLaneEditor::stepCursor(int direction) {
    const auto* points = lanePoints();
    if (points == nullptr || points->empty())
        return false;
    int index = direction > 0 ? 0 : (int)points->size() - 1;
    if (const auto cursor = selection_.getCursor(); cursor.has_value()) {
        for (int i = 0; i < (int)points->size(); ++i)
            if ((*points)[(std::size_t)i].beat == *cursor)
                index = juce::jlimit(0, (int)points->size() - 1, i + direction);
    }
    selectOnly((*points)[(std::size_t)index]);
    return true;
}

bool AutomationLaneEditor::selectAllPoints() {
    const auto* points = lanePoints();
    if (points == nullptr || points->empty())
        return false;
    selection_.setSelection(beatsOf(*points));
    if (!selection_.getCursor().has_value())
        selection_.setCursor(points->front().beat);
    return true;
}

bool AutomationLaneEditor::deleteSelectedPoints() {
    if (selection_.isEmpty())
        return false;
    commitPointEdit(editTarget(), selection_.getSelected(), {});
    selection_.clear();
    selection_.setCursor(std::nullopt);
    return true;
}

bool AutomationLaneEditor::copySelectedPoints() {
    const auto* points = lanePoints();
    if (clipboard_ == nullptr || points == nullptr || selection_.isEmpty())
        return false;
    *clipboard_ = copyPoints(selection_.selectedPoints(*points));
    return !clipboard_->isEmpty();
}

bool AutomationLaneEditor::cutSelectedPoints() { return copySelectedPoints() && deleteSelectedPoints(); }

bool AutomationLaneEditor::canPastePoints() const {
    return clipboard_ != nullptr && !clipboard_->isEmpty() && lanePoints() != nullptr;
}

bool AutomationLaneEditor::pasteAtPlayhead() {
    const double beat = transport_ != nullptr ? transport_->getPositionSnapshot().ppq : 0.0;
    return pasteAtBeat(beat);
}

bool AutomationLaneEditor::pasteAtBeat(double beat) {
    if (!canPastePoints() || !std::isfinite(beat))
        return false;
    const auto& range = doc_->getLane(laneId_)->range;
    const auto pasted = pastedPoints(*clipboard_, std::max(0.0, snappedBeatAt(beat)), range.minValue, range.maxValue);
    if (!commitPointEdit(editTarget(), {}, pasted))
        return false;
    selection_.setSelection(beatsOf(pasted));
    selection_.setCursor(pasted.front().beat);
    return true;
}

} // namespace synth::ui
