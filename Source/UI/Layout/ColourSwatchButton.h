#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The one colour swatch of the app: a juce::Button filled with `colour`. Border, hover ring and focus
// ring are defined once, in AppLookAndFeel::drawColourSwatch. A left-click fires onClick; a right-click
// fires onRightClick instead (and never onClick) when that is set, otherwise it behaves like a left-click.
// Call sites keep their own title, tooltip, description and focus policy: the button sets none of them.
class ColourSwatchButton : public juce::Button {
public:
    explicit ColourSwatchButton(const juce::String& name);

    // What the swatch paints; set it, then repaint().
    juce::Colour colour{juce::Colours::grey};
    std::function<void()> onRightClick;

    // Test seam: draws the focus ring without real keyboard focus.
    bool forceFocusRingForTest = false;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
};

} // namespace synth::ui
