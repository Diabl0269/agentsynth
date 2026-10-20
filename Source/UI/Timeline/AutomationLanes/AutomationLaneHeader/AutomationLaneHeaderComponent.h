#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/LaneValueReadout.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlStrip.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneMarkerIcon.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneNameButton.h"
#include "UI/Timeline/AutomationLanes/LaneMenuHook.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <utility>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth::ui {

struct TrackHeaderHost;

// The header-column half of one automation lane row, under the track that owns the lane: a stripe
// in the track's colour, the parameter and module names, the curve's value at the playhead (or the
// selected point's value, see setSelectedPointValue), a strip of control chips (the record mode, add a modulator... --
// the set every automation lane shows is the user's, see LaneControlStrip) and a menu (add a modulator, change the
// parameter, duplicate, move to another track, delete).
// The same menu opens at the pointer on a right-click anywhere on the header and from the keyboard (Shift+F10).
// A click anywhere on the header that is not a control gives it keyboard focus (Up/Down walk the rows from there); the
// parameter name is a button of its own that opens the change-parameter picker, and a marker icon before it says
// this is an automation lane. Holds no lane state of its own;
// every value is re-read from the doc by refreshFromDoc() / setReadoutBeat().
// Message thread only. A Delete or Move from the menu destroys this component before the call
// returns (the owning panel prunes it on the doc notification).
class AutomationLaneHeaderComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public KeyboardContextMenuProvider {
public:
    static constexpr int kIndent = 16;       // px the row is inset from the track header's left edge
    static constexpr int kStripeWidth = 4;   // px of track colour at the row's left edge
    static constexpr int kReadoutWidth = 44; // px for the value at the playhead
    static constexpr int kDeleteLaneMenuId = 1;
    static constexpr int kAddModulatorMenuId = 2;
    static constexpr int kChangeParameterMenuId = 3;
    static constexpr int kDuplicateMenuId = 4;
    static constexpr int kCreateCustomLfoMenuId = 5;
    static constexpr int kRecordModeMenuIdBase = 50;   // + record mode (the Read / Write chip's menu)
    static constexpr int kMoveToTrackMenuIdBase = 100; // + index into the menu's move targets

    /** `doc` must outlive this component; `host`, `undo` and `controls` may be null (no `controls`: the chips show the
     *  default set and the "+" row has nowhere to float). */
    AutomationLaneHeaderComponent(synth::TimelineDoc& doc, synth::LaneId lane, TrackHeaderHost* host,
                                  AppUndoManager* undo, LaneControlSet* controls = nullptr);

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
    void moved() override;

    /** The control chips, the "+" button and edit mode. */
    LaneControlStrip& getControlStrip() noexcept { return strip_; }

    /** The lane menu (right-click, Shift+F10 or Return on the row); item ids are the k*MenuId constants above. */
    juce::PopupMenu buildMenu();
    /** Adds the Create custom LFO item (text and enabled state follow the lane's range and parameter). With `direct`
     *  the item runs itself when picked, for a menu that does not route its choice through applyMenuChoice. */
    void addCreateCustomLfoItem(juce::PopupMenu& menu, bool direct = false) const;
    /** Applies a menu choice; may destroy this component (see the class comment). */
    void applyMenuChoice(int menuId);

    /** A combo id (LaneRecordMode + 1) as the record-mode selector applies it. */
    void applyRecordModeChoice(int comboId);

    /** Shows the menu with `options` placing it (the pointer for a right-click, the header's anchor from the keyboard).
     */
    void showMenuAt(const juce::PopupMenu::Options& options);
    /** Opens the picker that points this lane at another parameter; a no-op without a host or a free parameter. */
    void openChangeParameterPicker();
    /** Opens the picker for a copy of this lane directly below it; the copy exists only once a parameter is picked. */
    void openDuplicatePicker();
    /** The parameter name button's area, in this component's coordinates. */
    juce::Rectangle<int> getNameAreaForTest() const noexcept { return nameArea_; }
    /** How many plain clicks asked this row for keyboard focus (a headless test has no native focus to read). */
    int getClickFocusRequestsForTest() const noexcept { return clickFocusRequests_; }
    LaneNameButton& getNameButton() noexcept { return nameButton_; }
    LaneMarkerIcon& getMarkerIcon() noexcept { return marker_; }
    /** Where the module note sits under the name, in this component's coordinates. */
    juce::Rectangle<int> getNoteAreaForTest() const noexcept { return noteArea_; }

    /** The Range tool's selection when it sits on this lane and has width, as {start, end} beats; the pool answers. May
     * be null (no range ever). */
    std::function<std::optional<std::pair<double, double>>()> laneRangeProvider;
    /** "Create custom LFO" was picked for the range {start, end}; the pool animates the lane and runs the host's
     * command. */
    std::function<void(double startBeat, double endBeat)> onCreateCustomLfo;

    /** The drag-to-reorder gesture, owned by the lane pool: a press, every drag step (raw screen y) and the release,
     *  which answers true when the press never became a drag (a plain click). All may be null. */
    std::function<void(int screenY)> onDragPress;
    std::function<void(int screenY)> onDragMove;
    std::function<bool()> onDragRelease;
    /** Offered every key that reaches the row (a chip's unused keys bubble here) first; true when the pool used it
     *  (Move Lane Up/Down). May be null. */
    std::function<bool(const juce::KeyPress&)> onLaneKey;
    /** 0..1 strength of the lifted look while this lane is reorder-dragged; repaints only on a change. */
    void setLift(float lift);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    /** The name button's tip while the pointer is over the name, else the row's own. */
    juce::String getTooltip() override;
    /** Keys for the header row itself: Return opens the lane menu, Up/Down ask the owner to move keyboard focus
     *  to the previous/next row. */
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;
    /** Up (-1) / Down (+1) on the focused header row; may be null. */
    std::function<void(int direction)> onFocusMoveRequested;
    /** Fired when this header row takes keyboard focus; may be null. */
    std::function<void()> onKeyboardFocused;
    juce::MouseCursor getMouseCursor() override;
    bool showContextMenuForKeyboardFocus() override;

private:
    void openParameterPicker(bool duplicate);
    bool canPickParameter() const;
    void addModulatorItem(juce::PopupMenu& menu) const;
    void openAddModulatorPicker();
    // The control chips (AutomationLaneHeaderControls.cpp).
    LaneControlStrip::Actions controlActions();
    void performControl(LaneControl control, juce::Component& anchor);
    bool controlEnabled(LaneControl control) const;
    juce::String controlDetail(LaneControl control) const;
    void showChipMenu(juce::PopupMenu menu, juce::Component& anchor);
    void showRecordModeMenu(juce::Component& anchor);
    void showMoveMenu(juce::Component& anchor);
    void toggleMute();
    juce::PopupMenu::Item createCustomLfoItem() const;

    synth::TimelineDoc& doc_;
    synth::LaneId laneId_;
    TrackHeaderHost* host_ = nullptr;
    AppUndoManager* undo_ = nullptr;

    juce::String parameterName_;
    juce::String moduleName_;
    juce::String valueText_;
    juce::Colour trackColour_{juce::Colours::grey};
    LaneValueReadout readout_;
    std::vector<synth::TrackId> moveTargets_; // what buildMenu() last listed under "Move to track"

    LaneMarkerIcon marker_{LaneMarkerIcon::Kind::Automation};
    LaneNameButton nameButton_;
    LaneControlStrip strip_;
    int modeBeforeMute_ = -1;       // the record mode Mute put back to Off, for Mute to restore
    juce::Rectangle<int> nameArea_; // the name button's bounds: the press that opens the parameter picker
    juce::Rectangle<int> noteArea_; // the module's name under the lane's
    float lift_ = 0.0f;
    bool pressed_ = false;       // a left press is down on the header
    bool pressedOnName_ = false; // ...and landed on the parameter name button
    int clickFocusRequests_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationLaneHeaderComponent)
};

} // namespace synth::ui
