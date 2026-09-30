#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// TimelineRoutingPaneControls.h (docs/timeline/tracks.md#routing-from-the-side-pane): the two small controls the
// Timeline routing pane's rows are built from -- a combo-style button (a value with a chevron) and a text link.
namespace synth::ui {

/** A combo-style button: its text at the left, a chevron at the right; the text is drawn in the warning colour while
 *  setWarning(true). It only reports the click -- the pane decides what opens. */
class RoutingComboButton : public juce::Button {
public:
    explicit RoutingComboButton(const juce::String& name);

    void setWarning(bool warning);
    bool isWarning() const noexcept { return warning_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    bool warning_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingComboButton)
};

/** A text link ("Show on canvas ›"): accent text at the left, underlined while hovered. */
class RoutingTextLink : public juce::Button {
public:
    explicit RoutingTextLink(const juce::String& text);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingTextLink)
};

} // namespace synth::ui
