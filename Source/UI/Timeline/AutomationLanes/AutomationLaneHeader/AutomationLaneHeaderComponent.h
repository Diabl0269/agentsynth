#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/IconButton.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/LaneValueReadout.h"
#include "UI/Timeline/AutomationLanes/LaneMenuHook.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth::ui {

struct TrackHeaderHost;

// The header-column half of one automation lane row, under the track that owns the lane: a stripe
// in the track's colour, the parameter and module names, the curve's value at the playhead (or the
// selected point's value, see setSelectedPointValue), the
// lane's record mode and a menu (add a modulator, change the parameter, duplicate, move to another track, delete).
// The same menu opens at the pointer on a right-click anywhere on the header and from the keyboard (Shift+F10);
// a click on the parameter name opens the change-parameter picker. Holds no lane state of its own;
// every value is re-read from the doc by refreshFromDoc() / setReadoutBeat().
// Message thread only. A Delete or Move from the menu destroys this component before the call
// returns (the owning panel prunes it on the doc notification).
class AutomationLaneHeaderComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public juce::KeyListener
    , public KeyboardContextMenuProvider {
public:
    static constexpr int kIndent = 16;       // px the row is inset from the track header's left edge
    static constexpr int kStripeWidth = 4;   // px of track colour at the row's left edge
    static constexpr int kReadoutWidth = 44; // px for the value at the playhead
    static constexpr int kDeleteLaneMenuId = 1;
    static constexpr int kAddModulatorMenuId = 2;
    static constexpr int kChangeParameterMenuId = 3;
    static constexpr int kDuplicateMenuId = 4;
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

    /** Shows the menu with `options` placing it (the pointer for a right-click, the header's anchor from the keyboard).
     */
    void showMenuAt(const juce::PopupMenu::Options& options);
    /** Opens the picker that points this lane at another parameter; a no-op without a host or a free parameter. */
    void openChangeParameterPicker();
    /** Opens the picker for a copy of this lane directly below it; the copy exists only once a parameter is picked. */
    void openDuplicatePicker();
    /** The parameter name's hit area, in this component's coordinates. */
    juce::Rectangle<int> getNameAreaForTest() const noexcept { return nameArea_; }

    /** The drag-to-reorder gesture, owned by the lane pool: a press, every drag step (raw screen y) and the release,
     *  which answers true when the press never became a drag (a plain click). All may be null. */
    std::function<void(int screenY)> onDragPress;
    std::function<void(int screenY)> onDragMove;
    std::function<bool()> onDragRelease;
    /** Offered every key that reaches the record-mode combo or the "..." button first (the combo would take
     *  Up/Down itself); true when the pool used it (Move Lane Up/Down). May be null. */
    std::function<bool(const juce::KeyPress&)> onLaneKey;
    /** 0..1 strength of the lifted look while this lane is reorder-dragged; repaints only on a change. */
    void setLift(float lift);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    using juce::Component::keyPressed;
    juce::MouseCursor getMouseCursor() override;
    bool showContextMenuForKeyboardFocus() override;

private:
    class MenuButton : public IconButton {
    public:
        MenuButton();
    };

    void showMenu();
    void openParameterPicker(bool duplicate);
    bool canPickParameter() const;
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
    float lift_ = 0.0f;
    bool pressed_ = false;       // a left press is down on the header
    bool pressedOnName_ = false; // ...and landed on the parameter name

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationLaneHeaderComponent)
};

} // namespace synth::ui
