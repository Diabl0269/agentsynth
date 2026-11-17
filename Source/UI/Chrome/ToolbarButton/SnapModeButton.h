#pragma once

#include "SnapMode.h"
#include "ToolbarButton.h"
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// The picker beside a curve editor's Snap button: a toolbar button whose glyph names the mode, with a corner notch
// marking the menu; a click opens a menu of the three modes. Like the clip sizing control it is never lit.
class SnapModeButton : public ToolbarButton {
public:
    /** `componentId` names the picker for tests; `propertyKey` is the user-settings key the mode is saved under. */
    SnapModeButton(const juce::String& componentId, const juce::String& propertyKey);

    SnapMode getMode() const noexcept { return mode_; }
    // Shows, saves (once settings are attached) and announces `mode` through onModeChanged.
    void choose(SnapMode mode);
    // Restores the saved mode and announces it; nullptr keeps the mode in memory only.
    void setApplicationProperties(juce::ApplicationProperties* props);
    // Names the Snap key in the tooltip: `decorate` returns the tooltip text with the key after it
    // (WindowSnapKey::tooltip).
    void setTooltipDecorator(std::function<juce::String(const juce::String&)> decorate);
    // The width the control needs: the same in every mode.
    int getFixedWidth() const;

    std::function<void(SnapMode)> onModeChanged;
    // A test replaces the real popup with a capture of the menu.
    std::function<void(juce::PopupMenu&)> showMenuHook;

    juce::PopupMenu buildMenu() const;
    void showMenu();
    void clicked() override;
    // The toolbar button plus the menu mark in the chip's lower-right corner.
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    static synth::theme::Icon iconFor(SnapMode mode) noexcept;

    static constexpr int kFirstModeItemId = 1; // + the mode's index

private:
    void show(SnapMode mode);
    void refreshTooltip();

    SnapMode mode_ = SnapMode::TimeAndLevel;
    juce::String propertyKey_;
    juce::ApplicationProperties* props_ = nullptr;
    std::function<juce::String(const juce::String&)> decorate_;
    std::unique_ptr<juce::Component> stateProbe_; // test mode's "<id>State"
};

} // namespace synth::ui
