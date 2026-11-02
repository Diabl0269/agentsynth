// Concern: TimelineAutomationLanes' per-row height -- the strip on the bottom edge of each lane row and each modulator
// row that resizes just that row by dragging (a drag previews, one doc write and one undo step on release), and the
// taller / shorter / default keys of a focused lane header or modulator row. A lane row's height is
// AutomationLane::heightScale, a modulator row's is its entry in AutomationLane::modulatorHeightScales (by
// modulatorOrderKey), both on top of the shared vertical zoom; the rows below follow through the layout-change path.
// It mirrors the track row's height handle (TimelinePanelTrackHeight.cpp).
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kHeightHandleThickness = 5; // the strip along a row's bottom edge, as on a track row

const juce::KeyPress kIncreaseHeightKey('=', juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kDecreaseHeightKey('-', juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kResetHeightKey('0', juce::ModifierKeys::altModifier, 0);
} // namespace

double TimelineAutomationLanes::rowHeightScale(const RowKey& row) const {
    if (heightDragLane_.isValid() && heightDragLane_ == row.first && heightDragKey_ == row.second)
        return heightDragScale_;
    const auto* l = doc_ != nullptr ? doc_->getLane(row.first) : nullptr;
    if (l == nullptr)
        return 1.0;
    return row.second.isEmpty() ? l->heightScale : l->modulatorRowScale(row.second);
}

double TimelineAutomationLanes::laneHeightScale(synth::LaneId lane) const { return rowHeightScale({lane, {}}); }

double TimelineAutomationLanes::modulatorHeightScale(synth::LaneId lane, const juce::String& orderKey) const {
    return orderKey.isEmpty() ? 1.0 : rowHeightScale({lane, orderKey});
}

EdgeResizeHandle* TimelineAutomationLanes::heightHandleFor(synth::LaneId lane) const {
    const auto it = heightHandles_.find({lane, {}});
    return it != heightHandles_.end() ? it->second.get() : nullptr;
}

EdgeResizeHandle* TimelineAutomationLanes::heightHandleFor(synth::LaneId lane, int modulatorIndex) const {
    const auto* row = modulatorRowFor(lane, modulatorIndex);
    if (row == nullptr)
        return nullptr;
    const auto it = heightHandles_.find({lane, modulatorOrderKey(row->getInfo())});
    return it != heightHandles_.end() ? it->second.get() : nullptr;
}

// The row's full height now (a folded modulator row resizes its full height; its title line follows), px.
int TimelineAutomationLanes::rowFullHeight(const RowKey& row) const {
    if (row.second.isEmpty())
        return laneRowHeight(row.first);
    const auto it = modulators_.find(row.first);
    if (it != modulators_.end())
        for (int i = 0; i < (int)it->second.infos.size(); ++i)
            if (modulatorOrderKey(it->second.infos[(size_t)i]) == row.second)
                return modulatorRowHeight(row.first, i);
    return 0;
}

juce::String TimelineAutomationLanes::heightKeysText() const {
    const auto text = [this](const char* action, const juce::KeyPress& fallback) {
        const auto key = shortcuts_ != nullptr ? shortcuts_->getBinding(action) : fallback;
        return key.isValid() ? key.getTextDescriptionWithIcons() : juce::String("unbound");
    };
    return "Keys on the row: " + text("timelineIncreaseTrackHeight", kIncreaseHeightKey) + " taller, " +
           text("timelineDecreaseTrackHeight", kDecreaseHeightKey) + " shorter, " +
           text("timelineResetTrackHeight", kResetHeightKey) + " default.";
}

// One strip per row, on its bottom edge (past the row's edit tray, where the next row starts). `name` is what the
// screen reader and the tooltip call the row.
void TimelineAutomationLanes::placeHeightHandle(const RowKey& row, const juce::String& name, int rowBottom, int width,
                                                bool visible) {
    auto& slot = heightHandles_[row];
    if (slot == nullptr) {
        slot = std::make_unique<EdgeResizeHandle>(EdgeResizeHandle::Axis::Vertical);
        slot->setComponentID(row.second.isEmpty() ? "laneHeightHandle" : "modulatorHeightHandle");
        slot->onDragStarted = [this, row] { beginRowHeightDrag(row); };
        slot->onDragged = [this, row](int delta) { dragRowHeight(row, delta); };
        slot->onDragEnded = [this, row] { endRowHeightDrag(row); };
        slot->onResetRequested = [this, row] { stepRowHeight(row, 0); };
        headerParent_.addAndMakeVisible(*slot);
    }
    slot->setTitle("Resize " + name);
    slot->setTooltip("Drag to resize the " + name + " only; double-click for the default height. " + heightKeysText());
    slot->setVisible(visible);
    slot->setBounds(0, rowBottom - kHeightHandleThickness, width, kHeightHandleThickness);
    slot->toFront(false);
}

// The handles of a lane's row and of each of its modulator rows, placed after the rows themselves. The lowest row of a
// track's last lane block shares its bottom edge with the track's own resize strip, so its strip sits just above it:
// one row edge, then the track edge, never two strips on one line.
void TimelineAutomationLanes::placeLaneHeightHandles(const synth::AutomationLane& lane, int laneY, bool lastBlock,
                                                     bool visible, int width) {
    const auto laneName = laneParameterName(lane, host_);
    const int count = modulatorCount(lane.id);
    const int shift = lastBlock ? kHeightHandleThickness : 0;
    const int laneBottom = laneY + laneRowHeight(lane.id) + trayExtentOf(lane.id);
    placeHeightHandle({lane.id, {}}, laneName + " lane", laneBottom - (count == 0 ? shift : 0), width, visible);
    for (int i = 0; i < count; ++i) {
        const auto* row = modulatorRowFor(lane.id, i);
        const int bottom = row->getBottom() + trayExtentOf(lane.id, i) - (i == count - 1 ? shift : 0);
        placeHeightHandle({lane.id, modulatorOrderKey(row->getInfo())},
                          row->getInfo().sourceTitle + " mod lane on " + laneName, bottom, width, visible);
    }
    pruneHeightHandles(lane.id);
}

// Drops the strips of the lane's modulator rows that are gone (a routing removed, or re-keyed by Change source).
void TimelineAutomationLanes::pruneHeightHandles(synth::LaneId lane) {
    std::set<juce::String> keys;
    const auto it = modulators_.find(lane);
    if (it != modulators_.end())
        for (const auto& info : it->second.infos)
            keys.insert(modulatorOrderKey(info));
    for (auto h = heightHandles_.lower_bound({lane, {}}); h != heightHandles_.end() && h->first.first == lane;) {
        if (h->first.second.isEmpty() || keys.count(h->first.second) > 0) {
            ++h;
            continue;
        }
        if (heightDragLane_ == lane && heightDragKey_ == h->first.second)
            heightDragLane_ = {}; // its row went mid-drag: the gesture goes with it
        h = heightHandles_.erase(h);
    }
}

void TimelineAutomationLanes::beginRowHeightDrag(const RowKey& row) {
    if (doc_ == nullptr || doc_->getLane(row.first) == nullptr)
        return;
    const double scale = rowHeightScale(row);
    const int full = rowFullHeight(row);
    heightDragLane_ = row.first;
    heightDragKey_ = row.second;
    heightDragStartScale_ = scale;
    heightDragStartPx_ = std::max(1, full);
    heightDragScale_ = scale;
}

// Previews only: a doc write per mouse move would republish the audio snapshot and dirty the project every pixel.
void TimelineAutomationLanes::dragRowHeight(const RowKey& row, int deltaPx) {
    if (!(row.first == heightDragLane_) || row.second != heightDragKey_)
        return;
    heightDragScale_ =
        std::clamp(heightDragStartScale_ * (double)(heightDragStartPx_ + deltaPx) / (double)heightDragStartPx_,
                   synth::AutomationLane::kMinHeightScale, synth::AutomationLane::kMaxHeightScale);
    if (onLayoutChanged)
        onLayoutChanged();
}

void TimelineAutomationLanes::endRowHeightDrag(const RowKey& row) {
    if (!(row.first == heightDragLane_) || row.second != heightDragKey_)
        return;
    const double scale = heightDragScale_;
    if (doc_ != nullptr) { // one undo step; the preview still shows the same height
        if (row.second.isEmpty())
            setLaneHeightScaleUndoable(*doc_, undo_, row.first, scale);
        else
            setModulatorHeightScaleUndoable(*doc_, undo_, row.first, row.second, scale);
    }
    heightDragLane_ = {};
    heightDragKey_ = {};
    if (onLayoutChanged)
        onLayoutChanged(); // also when the doc did not change (a drag back to where it started)
}

bool TimelineAutomationLanes::stepRowHeight(const RowKey& row, int direction) {
    if (doc_ == nullptr || doc_->getLane(row.first) == nullptr)
        return false;
    const double current = rowHeightScale(row);
    const double scale = direction > 0   ? current * kLaneHeightStepFactor
                         : direction < 0 ? current / kLaneHeightStepFactor
                                         : 1.0;
    return row.second.isEmpty() ? setLaneHeightScaleUndoable(*doc_, undo_, row.first, scale)
                                : setModulatorHeightScaleUndoable(*doc_, undo_, row.first, row.second, scale);
}

bool TimelineAutomationLanes::stepLaneHeight(synth::LaneId lane, int direction) {
    return stepRowHeight({lane, {}}, direction);
}

bool TimelineAutomationLanes::stepModulatorHeight(synth::LaneId lane, const juce::String& orderKey, int direction) {
    return !orderKey.isEmpty() && stepRowHeight({lane, orderKey}, direction);
}

// Taller / Shorter / Default (the track row's keys, rebindable) act on the focused lane header or modulator row only.
bool TimelineAutomationLanes::handleRowHeightKey(const RowKey& row, const juce::KeyPress& key) {
    const auto matches = [this, &key](const char* action, const juce::KeyPress& fallback) {
        return shortcuts_ == nullptr ? key == fallback
                                     : ShortcutManager::keyPressMatches(shortcuts_->getBinding(action), key);
    };
    int step = 2; // none
    if (matches("timelineIncreaseTrackHeight", kIncreaseHeightKey))
        step = 1;
    else if (matches("timelineDecreaseTrackHeight", kDecreaseHeightKey))
        step = -1;
    else if (matches("timelineResetTrackHeight", kResetHeightKey))
        step = 0;
    if (step == 2)
        return false;
    stepRowHeight(row, step);
    return true;
}

bool TimelineAutomationLanes::handleLaneHeightKey(synth::LaneId lane, const juce::KeyPress& key) {
    return handleRowHeightKey({lane, {}}, key);
}

} // namespace synth::ui
