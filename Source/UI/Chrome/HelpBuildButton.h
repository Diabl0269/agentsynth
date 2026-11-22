#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// The Welcome screen's "Help build Agent Synth" button (docs/layout/chrome.md): a custom-drawn control, not a
// TextButton. It is the one loud thing on the strip: a morphing accent-to-rose fill, a light that runs around its
// border, a shimmer that sweeps across every few seconds and a beating heart. Every effect reads one `phase` (0 to 1
// over HelpBuildStrip::kCycleMs) that the strip pushes in, so the button owns no clock of its own.
namespace synth::theme {
struct Colors;
}

namespace synth::ui {

class HelpBuildButton : public juce::Button {
public:
    static constexpr int kHeight = 40;

    HelpBuildButton();

    /** The width that holds the text, the heart and their padding. */
    static int preferredWidth(const juce::String& text);

    /** The label and heart colour: the dark or light ink that reads on the fill's accent and rose together. */
    static juce::Colour inkColour(const synth::theme::Colors& colours);

    /** Where the motion is in the strip's cycle (0 to 1). Repaints only this button. */
    void setPhase(float phase);

    /** Whether the effects move; false draws the same button in its resting pose (phase 0, no shimmer, heart at rest).
     */
    void setMotion(bool moving);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void resized() override;

private:
    float phase_ = 0.0f;
    bool moving_ = false;
    std::vector<juce::Point<float>> borderPoints_; // the border sampled evenly, rebuilt on resize
};

} // namespace synth::ui
