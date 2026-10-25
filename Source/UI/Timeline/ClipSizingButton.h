#pragma once

#include "Timeline/TimelineDoc/ClipEdgeSizing.h"
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The edit-tool strip's Sizing field: shows what dragging a clip edge does and opens a menu of the three modes.
// Lit while the mode is not Trim. The mode is a user setting, the same in every project.
class ClipSizingButton : public juce::TextButton {
public:
    ClipSizingButton();

    synth::ClipSizingMode getMode() const noexcept { return mode_; }
    // Shows, saves (once settings are attached) and announces `mode` through onModeChanged.
    void choose(synth::ClipSizingMode mode);
    // Restores the saved mode and announces it; nullptr keeps the mode in memory only.
    void setApplicationProperties(juce::ApplicationProperties* props);

    std::function<void(synth::ClipSizingMode)> onModeChanged;
    // A test replaces the real popup with a capture of the menu.
    std::function<void(juce::PopupMenu&)> showMenuHook;

    juce::PopupMenu buildMenu() const;
    void showMenu();
    void clicked() override;

    static constexpr int kFirstModeItemId = 1; // + the mode's index
    static constexpr const char* kPropertyKey = "timelineClipSizingMode";

private:
    void show(synth::ClipSizingMode mode);

    synth::ClipSizingMode mode_ = synth::ClipSizingMode::Trim;
    juce::ApplicationProperties* props_ = nullptr;
};

} // namespace synth::ui
