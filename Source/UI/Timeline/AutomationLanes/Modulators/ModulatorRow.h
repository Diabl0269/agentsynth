#pragma once

#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorInfo.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorShapeIcon.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

struct TrackHeaderHost;

// The header-column half of one modulator row under an automation lane: a tag in the owning track's
// colour, the source card's title, and -- for an LFO -- a picture of its shape, the shape, rate and sync controls and
// a menu (right-click, Shift+F10 or Return: show on canvas, remove); any other source is read-only. A routing through
// an Attenuverter also shows its amount ("Amount  +72%"), which the band beside it edits (ModulatorBand).
//
// Holds no parameter state: every control mirrors the live processor through the host
// (refreshValues), and every edit goes back through TrackHeaderHost::setNodeParameter, the same undo
// path the canvas knobs use. Message thread only. "Remove modulator" destroys this row before the
// call returns (the owner prunes it on the graph-changed refresh).
class ModulatorRow
    : public juce::Component
    , public juce::SettableTooltipClient
    , public KeyboardContextMenuProvider {
public:
    // Three lines (title; shape and rate; sync and amount) at 100% row zoom, scaled like a lane row. The
    // header column is ~200 px, too narrow for the app's combos and bars side by side on two lines.
    static constexpr int kBaseHeight = 54;
    static constexpr int kIndent = 28; // one step further in than a lane header
    static constexpr int kShowOnCanvasMenuId = 1;
    static constexpr int kRemoveMenuId = 2;

    /** `host` may be null (every control then stays inert). `parameterName` words the controls' names. */
    ModulatorRow(const ModulatorInfo& info, TrackHeaderHost* host, const juce::String& parameterName);

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

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

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
    /** Up (-1) / Down (+1) on the focused row itself (not on its controls, which keep their own arrows); may be null.
     *  Return on the row opens its menu. */
    std::function<void(int direction)> onFocusMoveRequested;
    /** Fired when this row takes keyboard focus; may be null. */
    std::function<void()> onKeyboardFocused;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;
    bool showContextMenuForKeyboardFocus() override;

private:
    void initLfoControls();
    void applyNames();
    void edit(const juce::String& uuid, const juce::String& paramId, float value, ParameterEditPhase phase);
    void wireDragEdits(juce::Slider& slider, const juce::String& uuid, const juce::String& paramId,
                       std::function<float(double)> toParameter);
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
    juce::Slider* dragging_ = nullptr; // the slider mid-gesture, which refreshValues() leaves alone
    juce::Rectangle<int> tagArea_;
    juce::Rectangle<int> titleArea_;
    juce::Rectangle<int> rateTextArea_; // the Hz value, beside its bar (never over it: the bar has a cap)
    juce::Rectangle<int> amountArea_;   // "Amount" and its readout
    std::optional<juce::Colour> trackColour_;
    std::optional<double> amount_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorRow)
};

} // namespace synth::ui
