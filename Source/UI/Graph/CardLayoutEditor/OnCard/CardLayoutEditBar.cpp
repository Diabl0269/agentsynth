// CardLayoutEditBar.cpp -- Preset, Apply to, Cancel and Done over the card's own header controls.
#include "CardLayoutEditBar.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

CardLayoutEditBar::CardLayoutEditBar() {
    setTitle("Layout editing");
    setFocusContainerType(FocusContainerType::focusContainer);
    preset_.setTitle("Preset");
    preset_.setTooltip("Save, load or reset this card's layout");
    preset_.onClick = [this] {
        if (onPreset)
            onPreset();
    };
    applyTo_.setTitle("Apply to");
    applyTo_.setTooltip("Choose which cards this layout changes");
    applyTo_.onClick = [this] {
        if (onApplyTo)
            onApplyTo();
    };
    cancel_.setTitle("Cancel");
    cancel_.setTooltip("Undo everything since Edit layout (Esc)");
    cancel_.onClick = [this] {
        if (onCancel)
            onCancel();
    };
    done_.setTitle("Done");
    done_.setTooltip("Keep the layout");
    done_.onClick = [this] {
        if (onDone)
            onDone();
    };
    addAndMakeVisible(preset_);
    addAndMakeVisible(applyTo_);
    addAndMakeVisible(cancel_);
    addAndMakeVisible(done_);
}

// Done is the primary action: the accent fill, under theme text.
void CardLayoutEditBar::lookAndFeelChanged() {
    const auto& colours = synth::theme::themeOf(*this).colors;
    done_.setColour(juce::TextButton::buttonColourId, colours.accent);
    done_.setColour(juce::TextButton::textColourOffId, colours.surface);
}

// Opaque in the card's own colour, so the header's mute and bypass buttons under it do not show through.
void CardLayoutEditBar::paint(juce::Graphics& g) {
    g.setColour(synth::theme::themeOf(*this).colors.surface);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
}

void CardLayoutEditBar::resized() {
    auto area = getLocalBounds().reduced(6, 0);
    done_.setBounds(area.removeFromRight(kButtonWidth));
    area.removeFromRight(kGap);
    cancel_.setBounds(area.removeFromRight(kButtonWidth));
    area.removeFromRight(kGap);
    applyTo_.setBounds(area.removeFromRight(kApplyToWidth));
    area.removeFromRight(kGap);
    preset_.setBounds(area.removeFromRight(kPresetWidth));
}

} // namespace synth::ui
