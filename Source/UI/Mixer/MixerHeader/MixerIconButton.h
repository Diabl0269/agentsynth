#pragma once

#include "UI/Theme/IconLibrary.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// MixerIconButton.h (docs/mixer/panel.md#header-controls-and-names): the small icon-only button the mixer
// puts inside a column -- the header's sources badge and every insert and send row's bypass toggle. A
// juce::DrawableButton, so AppLookAndFeel::drawDrawableButton paints its hover pill, its toggled-on wash
// and the keyboard focus ring; this class only supplies the glyph (the same muted, hover and accent
// ladder the toolbar's icons use) and keeps it current when the look and feel changes.
namespace synth::ui {

class MixerIconButton : public juce::DrawableButton {
public:
    /** `name` is the component name only; give the button its screen-reader title with setTitle(). */
    explicit MixerIconButton(const juce::String& name);

    /** Picks the glyph; applied now when a themed look and feel is installed, and again whenever one is. */
    void setIcon(synth::theme::Icon icon);

    /** Message thread only; null uses the plain tooltip set with setTooltip(). */
    std::function<juce::String()> tooltipProvider;

    juce::String getTooltip() override;
    void lookAndFeelChanged() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void applyImages();

    synth::theme::Icon icon_ = synth::theme::Icon::MixerSources;
    bool hasIcon_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerIconButton)
};

} // namespace synth::ui
