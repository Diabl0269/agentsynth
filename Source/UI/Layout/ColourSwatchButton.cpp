#include "ColourSwatchButton.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

ColourSwatchButton::ColourSwatchButton(const juce::String& name)
    : juce::Button(name) {}

void ColourSwatchButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        lf->drawColourSwatch(g, *this, highlighted, down);
    else
        synth::theme::paintColourSwatch(g, *this, synth::theme::themeOf(*this), highlighted, down);
}

void ColourSwatchButton::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() && onRightClick) {
        onRightClick();
        return; // a right-click is not a click: never forwarded to Button::mouseDown
    }
    juce::Button::mouseDown(e);
}

} // namespace synth::ui
