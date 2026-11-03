#pragma once

#include "Timeline/TimelineDoc/ClipEdgeSizing.h"
#include "UI/Theme/IconGlyphs.h"
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// The edit-tool strip's Sizing field: one fixed-width button showing the mode's glyph and a menu chevron; a click
// opens a menu of the three modes. The mode is a user setting, the same in every project.
class ClipSizingButton : public juce::TextButton {
public:
    ClipSizingButton();

    synth::ClipSizingMode getMode() const noexcept { return mode_; }
    // Shows, saves (once settings are attached) and announces `mode` through onModeChanged.
    void choose(synth::ClipSizingMode mode);
    // Restores the saved mode and announces it; nullptr keeps the mode in memory only.
    void setApplicationProperties(juce::ApplicationProperties* props);
    // The key that steps the mode while Select is the tool, named in the tooltip; empty when it has none.
    void setStepKeyHint(const juce::String& key);

    std::function<void(synth::ClipSizingMode)> onModeChanged;
    // A test replaces the real popup with a capture of the menu.
    std::function<void(juce::PopupMenu&)> showMenuHook;

    juce::PopupMenu buildMenu() const;
    void showMenu();
    void clicked() override;

    static synth::theme::Glyph glyphFor(synth::ClipSizingMode mode) noexcept;
    // Where the glyph is drawn, in the button's own coordinates.
    juce::Rectangle<float> glyphBounds() const noexcept;

    static constexpr int kWidth = 36;          // the field's width in every mode
    static constexpr int kFirstModeItemId = 1; // + the mode's index
    static constexpr const char* kPropertyKey = "timelineClipSizingMode";

protected:
    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    void show(synth::ClipSizingMode mode);
    void refreshTooltip();

    synth::ClipSizingMode mode_ = synth::ClipSizingMode::Trim;
    juce::ApplicationProperties* props_ = nullptr;
    juce::String stepKey_;
    std::unique_ptr<juce::Component> stateProbe_; // test mode's "timelineClipSizingState"
};

} // namespace synth::ui
