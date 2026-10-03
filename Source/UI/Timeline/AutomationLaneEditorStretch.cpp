// AutomationLaneEditor -- stretching the selection: the glue between the editor's mouse and keys and
// LanePointStretch (box, handles, preview math). The class is declared in AutomationLaneEditor.h.

#include "AutomationLaneEditor.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr double kKeyValueScale = 0.05; // Alt+Shift+Up/Down: the selection's value range grows or shrinks by this

bool isAltShift(const juce::KeyPress& key, int code) {
    return key == juce::KeyPress(
                      code, juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier), 0);
}

bool isSideHandle(StretchHandle handle) { return handle == StretchHandle::Left || handle == StretchHandle::Right; }

std::vector<double> beatsOf(const std::vector<LaneBreakpoint>& points) {
    std::vector<double> beats;
    beats.reserve(points.size());
    for (const auto& p : points)
        beats.push_back(p.beat);
    return beats;
}
} // namespace

//==============================================================================
// ---- Box and handles ----

bool AutomationLaneEditor::stretchBoxVisible() const {
    return tool_ == Tool::Pointer && selection_.size() >= 2 && lanePoints() != nullptr;
}

// The box follows the preview while a stretch is being dragged, so the handles ride with the points.
juce::Rectangle<float> AutomationLaneEditor::stretchBox() const {
    if (!stretchBoxVisible())
        return {};
    const auto map = pointMapper();
    const auto around =
        stretch_.isActive() ? pointsBounds(stretch_.result().moved, map) : selection_.boundingBox(*lanePoints(), map);
    return stretchBoxAround(around);
}

StretchHandle AutomationLaneEditor::stretchHandleAt(juce::Point<int> pos) const {
    return hitStretchHandle(pos.toFloat(), stretchBox(), getLocalBounds().toFloat());
}

juce::Rectangle<float> AutomationLaneEditor::getStretchHandleRectForTest(StretchHandle handle) const {
    const auto box = stretchBox();
    return box.isEmpty() ? juce::Rectangle<float>() : stretchHandleRect(handle, box, getLocalBounds().toFloat());
}

void AutomationLaneEditor::refreshStretchBox() { stretch_.setBoxShown(stretchBoxVisible()); }

void AutomationLaneEditor::paintStretchBox(juce::Graphics& g) {
    stretch_.paintBox(g, stretchBox(), getLocalBounds().toFloat(), hoverHandle_);
}

// A handle under the pointer wins over the point or segment beneath it; the pointer tells it by cursor and hint.
void AutomationLaneEditor::trackStretchHover(juce::Point<int> pos) {
    const auto handle = stretchBoxVisible() ? stretchHandleAt(pos) : StretchHandle::None;
    if (handle == hoverHandle_)
        return;
    hoverHandle_ = handle;
    updateMouseCursor();
    repaint();
}

std::optional<juce::MouseCursor> AutomationLaneEditor::stretchCursor() const {
    const auto handle =
        stretch_.isActive() ? stretch_.handle() : (stretchBoxVisible() ? hoverHandle_ : StretchHandle::None);
    if (handle == StretchHandle::None)
        return std::nullopt;
    return juce::MouseCursor(isSideHandle(handle) ? juce::MouseCursor::LeftRightResizeCursor
                                                  : juce::MouseCursor::UpDownResizeCursor);
}

juce::String AutomationLaneEditor::getTooltip() {
    if (stretchBoxVisible() && hoverHandle_ != StretchHandle::None)
        return isSideHandle(hoverHandle_) ? "Drag to stretch the selected points in time"
                                          : "Drag to scale their values";
    return juce::SettableTooltipClient::getTooltip();
}

//==============================================================================
// ---- The drag ----

// How far an unselected point is kept beyond a pushing edge: a grid step, a tiny gap with snap off.
double AutomationLaneEditor::pushGapBeats() const {
    const double grid = viewState_.divisionBeats(currentBeatsPerBar());
    return grid > 0.0 ? grid : kStretchMinGapBeats;
}

bool AutomationLaneEditor::beginStretchAt(juce::Point<int> pos) {
    if (!stretchBoxVisible())
        return false;
    const auto handle = stretchHandleAt(pos);
    const auto* points = lanePoints();
    if (handle == StretchHandle::None || doc_->getLane(laneId_) == nullptr)
        return false;
    const auto& range = doc_->getLane(laneId_)->range;
    stretch_.begin(handle, selection_.selectedPoints(*points), *points, range.minValue, range.maxValue, pushGapBeats());
    if (!stretch_.isActive())
        return false;
    dragMode_ = DragMode::Stretch;
    stretchGrabBeat_ = viewState_.xToBeat((double)pos.x);
    stretchGrabValue_ = yToValue((double)pos.y);
    hoverHandle_ = handle;
    hoveredBeat_.reset();
    bubble_.hide();
    updateMouseCursor();
    return true;
}

// The dragged edge moves by how far the pointer has travelled since the press, so grabbing a handle off-centre
// does not jump; a beat edge snaps, a value edge does not.
void AutomationLaneEditor::dragStretch(juce::Point<int> pos) {
    if (!stretch_.isActive())
        return;
    if (isSideHandle(stretch_.handle())) {
        const double raw = stretch_.edgeStart() + viewState_.xToBeat((double)pos.x) - stretchGrabBeat_;
        stretch_.update(std::max(0.0, snappedBeatAt(raw)));
    } else {
        stretch_.update(stretch_.edgeStart() + yToValue((double)pos.y) - stretchGrabValue_);
    }
}

void AutomationLaneEditor::cancelStretch() {
    if (!stretch_.isActive())
        return;
    stretch_.cancel();
    repaint();
}

void AutomationLaneEditor::commitStretch() {
    const auto original = stretch_.original();
    const auto result = stretch_.result();
    stretch_.cancel();
    commitStretchResult(original, result);
}

// The stretched points and any pushed ones replace the originals in ONE edit, so a stretch is one revision bump and one
// undo step; the same points, at their new beats, stay selected.
void AutomationLaneEditor::commitStretchResult(const std::vector<LaneBreakpoint>& original,
                                               const StretchResult& result) {
    if (!stretchChanged(original, result))
        return;
    std::optional<double> cursor;
    for (std::size_t i = 0; i < original.size(); ++i)
        if (selection_.getCursor() == original[i].beat)
            cursor = result.moved[i].beat;
    commitPointEdit(editTarget(), stretchRemoveBeats(original, result), stretchAddPoints(result));
    selection_.setSelection(beatsOf(result.moved));
    selection_.setCursor(cursor);
}

//==============================================================================
// ---- Keys ----

// Alt+Shift+Left/Right move the right edge by one grid step (squeeze/stretch, pushing like the drag); Alt+Shift+Up/Down
// grow or shrink the value range by 5% about the lowest selected value. Consumed whenever two points are selected,
// even when a limit left nothing to do.
bool AutomationLaneEditor::stretchKey(const juce::KeyPress& key) {
    using K = juce::KeyPress;
    const bool sideways = isAltShift(key, K::leftKey) || isAltShift(key, K::rightKey);
    const bool vertical = isAltShift(key, K::upKey) || isAltShift(key, K::downKey);
    const auto* points = lanePoints();
    if ((!sideways && !vertical) || points == nullptr || selection_.size() < 2)
        return false;
    const auto picked = selection_.selectedPoints(*points);
    if (picked.size() < 2)
        return false;

    StretchResult result;
    if (sideways) {
        const double direction = key.getKeyCode() == K::rightKey ? 1.0 : -1.0;
        result = stretchBeats(picked, *points, StretchHandle::Right,
                              std::max(0.0, picked.back().beat + direction * gridStepBeats()), pushGapBeats());
    } else {
        const auto& range = doc_->getLane(laneId_)->range;
        double lowest = picked.front().value, highest = lowest;
        for (const auto& p : picked) {
            lowest = std::min(lowest, p.value);
            highest = std::max(highest, p.value);
        }
        const double factor = 1.0 + (key.getKeyCode() == K::upKey ? kKeyValueScale : -kKeyValueScale);
        result.moved = scaleValues(picked, StretchHandle::Top, lowest + (highest - lowest) * factor, range.minValue,
                                   range.maxValue);
    }
    commitStretchResult(picked, result);
    return true;
}

} // namespace synth::ui
