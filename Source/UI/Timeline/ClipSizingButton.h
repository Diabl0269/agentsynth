#pragma once

#include "Timeline/TimelineDoc/ClipEdgeSizing.h"
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// The Timeline bar's clip sizing control: a toolbar button whose glyph (and caption, when labels are shown) names the
// mode, at one width in every mode, with a small corner notch marking the menu; a click opens a menu of the three
// modes. The mode is a user setting, the same in every project.
class ClipSizingButton : public ToolbarButton {
public:
    ClipSizingButton();

    synth::ClipSizingMode getMode() const noexcept { return mode_; }
    // Shows, saves (once settings are attached) and announces `mode` through onModeChanged.
    void choose(synth::ClipSizingMode mode);
    // Restores the saved mode and announces it; nullptr keeps the mode in memory only.
    void setApplicationProperties(juce::ApplicationProperties* props);
    // The key that steps the mode while Select is the tool, named in the tooltip; empty when it has none.
    void setStepKeyHint(const juce::String& key);
    // Shows the mode's short name beside the glyph (true) or the glyph alone; the width follows.
    void setCaptionShown(bool shown);
    bool isCaptionShown() const noexcept { return captionShown_; }
    // The width the control needs: the same in every mode, so choosing one never moves its neighbours.
    int getFixedWidth() const;

    std::function<void(synth::ClipSizingMode)> onModeChanged;
    // A test replaces the real popup with a capture of the menu.
    std::function<void(juce::PopupMenu&)> showMenuHook;

    juce::PopupMenu buildMenu() const;
    void showMenu();
    void clicked() override;
    // The toolbar button plus the menu mark in the chip's lower-right corner.
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    static synth::theme::Icon iconFor(synth::ClipSizingMode mode) noexcept;
    // The caption shown beside the glyph: "Trim", "Carry" or "Stretch".
    static juce::String captionFor(synth::ClipSizingMode mode);

    static constexpr int kFirstModeItemId = 1; // + the mode's index
    static constexpr const char* kPropertyKey = "timelineClipSizingMode";

private:
    void show(synth::ClipSizingMode mode);
    void refreshTooltip();

    synth::ClipSizingMode mode_ = synth::ClipSizingMode::Trim;
    bool captionShown_ = true;
    juce::ApplicationProperties* props_ = nullptr;
    juce::String stepKey_;
    std::unique_ptr<juce::Component> stateProbe_; // test mode's "timelineClipSizingState"
};

} // namespace synth::ui
