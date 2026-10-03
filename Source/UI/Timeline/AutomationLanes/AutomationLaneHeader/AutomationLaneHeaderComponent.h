#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/IconButton.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/LaneValueReadout.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth::ui {

struct TrackHeaderHost;

// The header-column half of one automation lane row, under the track that owns the lane: a stripe
// in the track's colour, the parameter and module names, the curve's value at the playhead (or the
// selected point's value, see setSelectedPointValue), the
// lane's record mode and a menu (add a modulator, move to another track, delete). Holds no lane state of its own;
// every value is re-read from the doc by refreshFromDoc() / setReadoutBeat().
// Message thread only. A Delete or Move from the menu destroys this component before the call
// returns (the owning panel prunes it on the doc notification).
class AutomationLaneHeaderComponent : public juce::Component {
public:
    static constexpr int kIndent = 16;       // px the row is inset from the track header's left edge
    static constexpr int kStripeWidth = 4;   // px of track colour at the row's left edge
    static constexpr int kReadoutWidth = 44; // px for the value at the playhead
    static constexpr int kDeleteLaneMenuId = 1;
    static constexpr int kAddModulatorMenuId = 2;
    static constexpr int kMoveToTrackMenuIdBase = 100; // + index into the menu's move targets

    /** `doc` must outlive this component; `host` and `undo` may be null. */
    AutomationLaneHeaderComponent(synth::TimelineDoc& doc, synth::LaneId lane, TrackHeaderHost* host,
                                  AppUndoManager* undo);

    synth::LaneId getLaneId() const noexcept { return laneId_; }

    /** Re-reads names, colour and record mode. A no-op once the lane is gone. */
    void refreshFromDoc();
    /** Re-evaluates the value readout at `beat`; repaints only when the text changed. */
    void setReadoutBeat(double beat);
    /** The value text at the playhead, whatever the slot is showing. */
    juce::String getValueText() const { return valueText_; }
    /** The selected point's value text in the accent colour, or an empty optional for the playhead
     *  value again. Cheap to call repeatedly: nothing repaints unless the text or the mode changed. */
    void setSelectedPointValue(const std::optional<juce::String>& text);
    LaneValueReadout& getReadout() noexcept { return readout_; }

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    juce::ComboBox& getRecordModeCombo() noexcept { return recordMode_; }
    juce::Button& getMenuButton() noexcept { return menuButton_; }

    /** The "..." menu as the button shows it; item ids are the k*MenuId constants above. */
    juce::PopupMenu buildMenu();
    /** Applies a menu choice; may destroy this component (see the class comment). */
    void applyMenuChoice(int menuId);

    /** A combo id (LaneRecordMode + 1) as the record-mode selector applies it. */
    void applyRecordModeChoice(int comboId);

private:
    class MenuButton : public IconButton {
    public:
        MenuButton();
    };

    void showMenu();
    void addModulatorItem(juce::PopupMenu& menu) const;
    void openAddModulatorPicker();
    void applyRecordModeColour();

    synth::TimelineDoc& doc_;
    synth::LaneId laneId_;
    TrackHeaderHost* host_ = nullptr;
    AppUndoManager* undo_ = nullptr;

    juce::String parameterName_;
    juce::String moduleName_;
    juce::String valueText_;
    juce::Colour trackColour_{juce::Colours::grey};
    juce::ComboBox recordMode_;
    MenuButton menuButton_;
    LaneValueReadout readout_;
    std::vector<synth::TrackId> moveTargets_; // what buildMenu() last listed under "Move to track"

    juce::Rectangle<int> nameArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationLaneHeaderComponent)
};

} // namespace synth::ui
