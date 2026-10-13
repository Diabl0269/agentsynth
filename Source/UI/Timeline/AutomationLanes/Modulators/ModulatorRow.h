#pragma once

#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlStrip.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneMarkerIcon.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneNameButton.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorInfo.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorShapeIcon.h"
#include "UI/Timeline/AutomationLanes/TrackFoldArrow.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

struct TrackHeaderHost;

// The header-column half of one modulator row under an automation lane (a mod lane): a marker icon, the source card's
// title as a button that opens the Change source picker, and -- for an LFO -- a picture of its shape, the shape, rate
// and sync controls, a strip of control chips (the set every mod lane shows is the user's, see LaneControlStrip) and a
// menu (right-click, Shift+F10 or Return: show on canvas, remove); any other source is read-only. A click anywhere else
// on the row, not on a control, gives it keyboard focus. A routing through an Attenuverter also shows its amount
// ("Amount  +72%"), which the band beside it edits (ModulatorBand).
//
// A fold arrow at the left of the title line collapses the row to that line (the title and the amount readout; the
// shape, rate and sync controls are hidden) and expands it again; Left on the row collapses it and Right expands it,
// as in a tree. The owner decides the fold state and the height; the row only shows it.
//
// Holds no parameter state: every control mirrors the live processor through the host
// (refreshValues), and every edit goes back through TrackHeaderHost::setNodeParameter, the same undo
// path the canvas knobs use. Message thread only. "Remove modulator" destroys this row before the
// call returns (the owner prunes it on the graph-changed refresh).
class ModulatorRow
    : public juce::Component
    , public juce::SettableTooltipClient
    , public juce::KeyListener
    , public KeyboardContextMenuProvider {
public:
    // Three lines (title; shape and rate; sync and amount) at 100% row zoom, scaled like a lane row. The
    // header column is ~200 px, too narrow for the app's combos and bars side by side on two lines.
    static constexpr int kBaseHeight = 54;
    static constexpr int kIndent = 16;     // the same inset as a lane header, so the marker and name line up with it
    static constexpr int kStripeWidth = 4; // px of track colour at the row's left edge, as on a lane header
    static constexpr int kShowOnCanvasMenuId = 1;
    static constexpr int kRemoveMenuId = 2;
    static constexpr int kChangeSourceMenuId = 3; // only when the host can re-point the routing

    /** `host` may be null (every control then stays inert). `parameterName` words the controls' names. `controls` may
     *  be null (the chips then show the default set and the "+" row has nowhere to float). */
    ModulatorRow(const ModulatorInfo& info, TrackHeaderHost* host, const juce::String& parameterName,
                 LaneControlSet* controls = nullptr);

    const ModulatorInfo& getInfo() const noexcept { return info_; }
    /** Same routing (same key()), possibly re-titled or re-coloured. */
    void setInfo(const ModulatorInfo& info, const juce::String& parameterName);
    /** Re-reads every control from the live processor; a control mid-drag is left alone. */
    void refreshValues();
    /** The owning track's colour (tag, stripe, amount text), drawn pushed to a readable contrast. */
    void setTrackColour(juce::Colour colour);
    /** The amount to show (-1..1), or nullopt for a direct cable, which has none. */
    void setAmount(std::optional<double> amount);
    /** The amount readout as drawn ("+72%"); empty for a direct cable. */
    juce::String getAmountText() const;

    /** Folded to the title line: the arrow, its names and the Left/Right keys follow. The owner sets the height. */
    void setCollapsed(bool collapsed);
    bool isCollapsed() const noexcept { return collapsed_; }
    /** True once the row is at its one-line height (or on its way out of it): hides the controls and moves the amount
     *  readout up onto the title line. Kept apart from setCollapsed so a folding row keeps its controls until it
     *  lands. */
    void setCompact(bool compact);
    bool isCompact() const noexcept { return compact_; }
    /** The height of one line, px; 0 = a third of the row. The owner sets it so lines keep their place while the row's
     *  height tweens. */
    void setTitleLineHeight(int height);
    TrackFoldArrow& getFoldArrow() noexcept { return foldArrow_; }
    /** How many plain clicks asked this row for keyboard focus (a headless test has no native focus to read). */
    int getClickFocusRequestsForTest() const noexcept { return clickFocusRequests_; }
    LaneNameButton& getNameButton() noexcept { return nameButton_; }
    /** The control chips, the "+" button and edit mode. */
    LaneControlStrip& getControlStrip() noexcept { return strip_; }
    LaneMarkerIcon& getMarkerIcon() noexcept { return marker_; }
    /** The source name button's bounds, in this row's coordinates. */
    juce::Rectangle<int> getNameAreaForTest() const noexcept { return nameButton_.getBounds(); }
    /** The arrow, Return/Space on it, or Left/Right on the row asked to collapse (true) or expand (false); may be
     *  null. */
    std::function<void(bool collapse)> onFoldRequested;
    /** Offered every key that reaches the row or one of its controls first (a combo or bar would take Up/Down
     *  itself); true when the owner used it (Move Lane Up/Down moves the modulator). May be null. */
    std::function<bool(const juce::KeyPress&)> onMoveKey;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void moved() override;

    juce::ComboBox& getShapeCombo() noexcept { return shape_; }
    juce::ComboBox& getSyncRateCombo() noexcept { return syncRate_; }
    juce::Slider& getRateSlider() noexcept { return rateHz_; }
    juce::ToggleButton& getSyncToggle() noexcept { return sync_; }
    ModulatorShapeIcon& getShapeIcon() noexcept { return shapeIcon_; }

    /** The row menu (right-click, Shift+F10 or Return on the row); ids are the k*MenuId constants. */
    juce::PopupMenu buildMenu() const;
    /** Applies a menu choice; may destroy this row (see the class comment). */
    void applyMenuChoice(int menuId);
    /** Shows the menu with `options` placing it: the pointer for a right-click anywhere on the row, beside the row for
     *  the keyboard. */
    void showMenuAt(const juce::PopupMenu::Options& options);
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    /** The name button's tip while the pointer is over the name, else the row's own. */
    juce::String getTooltip() override;
    /** The drag that reorders the row or moves it to another lane, reported by screen y: the press, each drag event
     *  (true once the pointer is really dragging, which gives the grabbing cursor) and the release. Any may be null. */
    std::function<void(int screenY)> onDragPress;
    std::function<bool(int screenY)> onDragMove;
    std::function<void()> onDragRelease;
    /** 0..1 strength of the lifted look while this row is dragged; repaints only on a change. */
    void setLift(float lift);
    /** Up (-1) / Down (+1) on the focused row itself (not on its controls, which keep their own arrows); may be null.
     *  Return on the row opens its menu. */
    std::function<void(int direction)> onFocusMoveRequested;
    /** Fired when this row takes keyboard focus; may be null. */
    std::function<void()> onKeyboardFocused;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;
    bool showContextMenuForKeyboardFocus() override;

private:
    void initLfoControls();
    void applyNames();
    void openChangeSourcePicker(); // the name button's press and the menu's "Change source..."
    void edit(const juce::String& uuid, const juce::String& paramId, float value, ParameterEditPhase phase);
    void wireDragEdits(juce::Slider& slider, const juce::String& uuid, const juce::String& paramId,
                       std::function<float(double)> toParameter);
    void applyCompact();
    LaneControlStrip::Actions controlActions();
    bool controlEnabled(LaneControl control) const;
    void layoutLfoControls(juce::Rectangle<int> shapeLine, juce::Rectangle<int> amountLine);
    void layoutAmount(juce::Rectangle<int> line);
    juce::Colour readableTrackColour() const;

    ModulatorInfo info_;
    TrackHeaderHost* host_ = nullptr;
    juce::String parameterName_;

    juce::ComboBox shape_;
    juce::ComboBox syncRate_;
    juce::Slider rateHz_;
    juce::ToggleButton sync_;
    ModulatorShapeIcon shapeIcon_;
    TrackFoldArrow foldArrow_;
    bool collapsed_ = false;
    bool compact_ = false;
    bool pressed_ = false;
    bool pressedOnName_ = false; // a left press landed on the name button
    bool dragged_ = false;       // ...and the pointer has really dragged the row since
    int clickFocusRequests_ = 0;
    float lift_ = 0.0f;
    int titleLineHeight_ = 0;
    juce::Slider* dragging_ = nullptr; // the slider mid-gesture, which refreshValues() leaves alone
    LaneMarkerIcon marker_{LaneMarkerIcon::Kind::Mod};
    LaneNameButton nameButton_;
    LaneControlStrip strip_;
    juce::Rectangle<int> rateTextArea_;      // the Hz value, beside its bar (never over it: the bar has a cap)
    juce::Rectangle<int> amountArea_;        // "Amount" and its readout
    juce::Rectangle<int> compactAmountArea_; // the readout alone, on the title line of a folded row
    std::optional<juce::Colour> trackColour_;
    std::optional<double> amount_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorRow)
};

} // namespace synth::ui
