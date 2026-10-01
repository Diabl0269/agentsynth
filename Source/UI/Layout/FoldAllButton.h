#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The one "fold every section" control of a list of collapsible sections (the Keyboard Shortcuts tab
// and the Preferences All view; the module library paints the same label itself). One small,
// right-aligned text button: it reads "Collapse all" while any section is open and "Expand all" once
// every section is folded, and the owner clicks it to do the other thing. Size it to the whole strip
// it sits in; it draws its focus ring around that strip.
class FoldAllButton : public juce::Button {
public:
    FoldAllButton();

    // Flips the label and tooltip for the current state; a no-op when nothing changes.
    void setAllFolded(bool allFolded);
    bool isAllFolded() const noexcept { return allFolded_; }

    // Height of the strip the owner gives it.
    static constexpr int kStripHeight = 20;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    bool allFolded_ = false;
};

} // namespace synth::ui
