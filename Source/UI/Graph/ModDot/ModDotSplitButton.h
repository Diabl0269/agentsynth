#pragma once

// The panel's "Add source" split button: the left half opens the source list under the rows, the right half starts
// "Pick on canvas". Each half is a Tab stop with the accent focus ring, a screen-reader name and a tooltip, and is
// drawn lit while its mode is on. Left/Right move between the halves; Up/Down are left to the page.

#include "ModDotGlyphButton.h"
#include "ModDotMotion.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class ModDotSplitButton final : public juce::Component {
public:
    static constexpr int kHeight = 26;

    ModDotSplitButton();
    ~ModDotSplitButton() override;

    juce::Button& listHalf() noexcept;
    juce::Button& pickHalf() noexcept;
    /** Lights the list half while the source list is open (and renames it for a screen reader). */
    void setListOpen(bool open);
    /** Lights the pick half while "Pick on canvas" is on. */
    void setPicking(bool on);
    bool isListOpen() const noexcept { return listOpen_; }
    bool isPicking() const noexcept { return picking_; }

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    class Half;
    std::unique_ptr<Half> list_;
    std::unique_ptr<Half> pick_;
    bool listOpen_ = false;
    bool picking_ = false;
};

} // namespace synth::ui
