#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager; // Source/AppUndoManager.h

namespace synth::ui {

struct TrackHeaderHost; // UI/Timeline/TimelineTrackHeaderComponent.h

/** "NodeName . Parameter name" for `lane` (host may be null: falls back to the uuid head / paramId). */
juce::String automationLaneLabel(TrackHeaderHost* host, const synth::AutomationLane& lane);

// The header half of one expanded automation lane row, in the track-header column under its track
// (docs/timeline/track-automation.md#lane-rows): the track's colour stripe, "Module . Param", a
// record-mode selector and (right-click) Remove lane. Pooled and re-bound by TrackAutomationLanes,
// never created per refresh; headless-safe.
class TrackLaneHeaderComponent
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kRemoveLaneMenuId = 1;

    TrackLaneHeaderComponent();

    /** Re-points the row at `lane`. All pointers non-owning and nullable. */
    void bind(synth::TimelineDoc* doc, AppUndoManager* undoManager, TrackHeaderHost* host, synth::LaneId lane);
    synth::LaneId getLane() const noexcept { return lane_; }

    /** Re-reads the label, orphan state, record mode and track colour from the doc. */
    void refreshFromDoc();

    void setFocusedLane(bool focused);
    bool isFocusedLane() const noexcept { return focused_; }

    /** Fired on a left click anywhere on the row (not the combo). */
    std::function<void(synth::LaneId)> onFocusRequested;

    juce::String getLabelText() const { return label_; }
    bool isOrphanWarning() const noexcept { return orphaned_; }
    juce::ComboBox& getRecordModeCombo() noexcept { return recordModeCombo_; }

    /** Combo id = LaneRecordMode + 1; one recordTimelineChange (the strip's own undo path). */
    void applyRecordModeChoice(int comboId);
    /** kRemoveLaneMenuId removes the lane in one recordTimelineChange; anything else is ignored. */
    void applyContextMenuChoice(int menuId);
    juce::PopupMenu buildContextMenu() const;
    /** Test seam: a right-click hands the built menu here instead of showing it. */
    void setShowContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) { menuHook_ = std::move(hook); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void runEdit(const std::function<void()>& mutation);

    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    TrackHeaderHost* host_ = nullptr;
    synth::LaneId lane_;
    juce::String label_;
    juce::Colour trackColour_{0xff808080};
    bool orphaned_ = false;
    bool focused_ = false;
    juce::ComboBox recordModeCombo_;
    std::function<void(juce::PopupMenu&)> menuHook_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackLaneHeaderComponent)
};

} // namespace synth::ui
