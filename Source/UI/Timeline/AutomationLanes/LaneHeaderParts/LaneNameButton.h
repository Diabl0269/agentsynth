#pragma once

#include "UI/Layout/FadeAmount.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The name on a timeline lane header (an automation lane's parameter, a mod lane's source) as a real button: a Tab stop
// with an accent ring, Return and Space press it at once, and its screen-reader name and tooltip say what a press
// does. It takes no mouse events of its own: the header that holds it handles the press so a drag from the name still
// reorders the lane, and tells the button when the pointer is over it (setHovered, a border in the accent colour) and
// when a click landed on it (triggerClick).
class LaneNameButton : public juce::Button {
public:
    LaneNameButton();

    void setName(const juce::String& text);
    /** The screen-reader name and the tooltip: what a press does ("Change source of LFO 1 on Cutoff"). */
    void setActionText(const juce::String& accessibleName, const juce::String& tooltip);
    void setBold(bool bold);
    /** The width the name needs at the button's font, padding included. */
    int getPreferredWidth() const;
    void setHovered(bool hovered);
    bool isHovered() const noexcept { return hovered_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    bool keyPressed(const juce::KeyPress& key) override;

    static constexpr int kPadding = 6; // px of space each side of the name inside the button
    static constexpr float kFontSize = 11.0f;

private:
    juce::Font font() const;

    juce::String text_;
    FadeAmount hoverFade_{*this}; // the border: fades in and out with the hover
    bool hovered_ = false;
    bool bold_ = true;
};

} // namespace synth::ui
