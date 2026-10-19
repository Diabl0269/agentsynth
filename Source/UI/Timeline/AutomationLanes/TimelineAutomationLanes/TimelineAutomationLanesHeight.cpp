// Concern: TimelineAutomationLanes' per-lane height -- the strip on the bottom edge of a lane's block that resizes it
// by dragging (a drag previews, one doc write and one undo step on release), and the taller / shorter / default keys of
// a focused lane header. The height is AutomationLane::heightScale (saved with the project) on top of the shared
// vertical zoom; the lane's modulator rows scale with it, and the tracks below follow through the layout-change path.
// It mirrors the track row's height handle (TimelinePanelTrackHeight.cpp).
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kHeightHandleThickness = 5; // the strip along the block's bottom edge, as on a track row

const juce::KeyPress kIncreaseHeightKey('=', juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kDecreaseHeightKey('-', juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kResetHeightKey('0', juce::ModifierKeys::altModifier, 0);
} // namespace

double TimelineAutomationLanes::laneHeightScale(synth::LaneId lane) const {
    if (heightDragLane_.isValid() && heightDragLane_ == lane)
        return heightDragScale_;
    const auto* l = doc_ != nullptr ? doc_->getLane(lane) : nullptr;
    return l != nullptr ? l->heightScale : 1.0;
}

EdgeResizeHandle* TimelineAutomationLanes::heightHandleFor(synth::LaneId lane) const {
    const auto it = heightHandles_.find(lane);
    return it != heightHandles_.end() ? it->second.get() : nullptr;
}

// The edit trays of the lane's header and modulator rows are fixed height; everything else in the block scales.
int TimelineAutomationLanes::laneTrayExtent(synth::LaneId lane) const {
    int extent = trayExtentOf(lane);
    for (int i = 0; i < modulatorCount(lane); ++i)
        extent += trayExtentOf(lane, i);
    return extent;
}

juce::String TimelineAutomationLanes::heightKeysText() const {
    const auto text = [this](const char* action, const juce::KeyPress& fallback) {
        const auto key = shortcuts_ != nullptr ? shortcuts_->getBinding(action) : fallback;
        return key.isValid() ? key.getTextDescriptionWithIcons() : juce::String("unbound");
    };
    return "Keys on the lane: " + text("timelineIncreaseTrackHeight", kIncreaseHeightKey) + " taller, " +
           text("timelineDecreaseTrackHeight", kDecreaseHeightKey) + " shorter, " +
           text("timelineResetTrackHeight", kResetHeightKey) + " default.";
}

// The strip sits on the bottom edge of the lane's block (its row plus its modulator rows). The last lane of a track's
// block shares that edge with the track's own resize strip, so it sits just above it.
void TimelineAutomationLanes::placeHeightHandle(synth::LaneId lane, const synth::Track& track, int blockBottom,
                                                int width) {
    auto& slot = heightHandles_[lane];
    if (slot == nullptr) {
        slot = std::make_unique<EdgeResizeHandle>(EdgeResizeHandle::Axis::Vertical);
        slot->setComponentID("laneHeightHandle");
        slot->onDragStarted = [this, lane] { beginLaneHeightDrag(lane); };
        slot->onDragged = [this, lane](int delta) { dragLaneHeight(lane, delta); };
        slot->onDragEnded = [this, lane] { endLaneHeightDrag(lane); };
        slot->onResetRequested = [this, lane] { stepLaneHeight(lane, 0); };
        headerParent_.addAndMakeVisible(*slot);
    }
    const auto* l = doc_ != nullptr ? doc_->getLane(lane) : nullptr;
    const auto name = l != nullptr ? laneParameterName(*l, host_) : juce::String("lane");
    slot->setTitle("Resize " + name + " lane");
    slot->setTooltip("Drag to resize the " + name + " lane and its modulators; double-click for the default height. " +
                     heightKeysText());
    int shift = 0;
    if (track.kind != synth::TrackKind::Automation) {
        const auto lastShown = std::find_if(track.lanes.rbegin(), track.lanes.rend(),
                                            [this](const synth::AutomationLane& x) { return !isAmountLane(x.id); });
        if (lastShown != track.lanes.rend() && lastShown->id == lane)
            shift = kHeightHandleThickness;
    }
    slot->setVisible(!laneDrag_.isReordering());
    slot->setBounds(0, blockBottom - kHeightHandleThickness - shift, width, kHeightHandleThickness);
    slot->toFront(false);
}

void TimelineAutomationLanes::beginLaneHeightDrag(synth::LaneId lane) {
    const auto* l = doc_ != nullptr ? doc_->getLane(lane) : nullptr;
    if (l == nullptr)
        return;
    heightDragLane_ = lane;
    heightDragStartScale_ = l->heightScale;
    heightDragStartPx_ = std::max(1, laneBlockHeight(*l) - laneTrayExtent(lane));
    heightDragScale_ = l->heightScale;
}

// Previews only: a doc write per mouse move would republish the audio snapshot and dirty the project every pixel.
void TimelineAutomationLanes::dragLaneHeight(synth::LaneId lane, int deltaPx) {
    if (!(lane == heightDragLane_))
        return;
    heightDragScale_ =
        std::clamp(heightDragStartScale_ * (double)(heightDragStartPx_ + deltaPx) / (double)heightDragStartPx_,
                   synth::AutomationLane::kMinHeightScale, synth::AutomationLane::kMaxHeightScale);
    if (onLayoutChanged)
        onLayoutChanged();
}

void TimelineAutomationLanes::endLaneHeightDrag(synth::LaneId lane) {
    if (!(lane == heightDragLane_))
        return;
    const double scale = heightDragScale_;
    if (doc_ != nullptr)
        setLaneHeightScaleUndoable(*doc_, undo_, lane, scale); // one undo step; the preview still shows the same height
    heightDragLane_ = {};
    if (onLayoutChanged)
        onLayoutChanged(); // also when the doc did not change (a drag back to where it started)
}

bool TimelineAutomationLanes::stepLaneHeight(synth::LaneId lane, int direction) {
    if (doc_ == nullptr || doc_->getLane(lane) == nullptr)
        return false;
    const double current = laneHeightScale(lane);
    const double scale = direction > 0   ? current * kLaneHeightStepFactor
                         : direction < 0 ? current / kLaneHeightStepFactor
                                         : 1.0;
    return setLaneHeightScaleUndoable(*doc_, undo_, lane, scale);
}

// Taller / Shorter / Default (the track row's keys, rebindable) act on the focused lane header.
bool TimelineAutomationLanes::handleLaneHeightKey(synth::LaneId lane, const juce::KeyPress& key) {
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
    stepLaneHeight(lane, step);
    return true;
}

} // namespace synth::ui
