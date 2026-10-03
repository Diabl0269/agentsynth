// CardLayoutEditBar.cpp -- the Time and tempo switch (ADSR cards), Preset, Apply to, Cancel and Done over the
// card's own header controls.
#include "CardLayoutEditBar.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

CardLayoutEditBar::CardLayoutEditBar() {
    setTitle("Layout editing");
    setFocusContainerType(FocusContainerType::focusContainer);
    timeTempo_.setTooltip("Show each stage once (its time or tempo control follows the card's Time/Tempo switch), "
                          "or as separate Time and Tempo groups");
    timeTempo_.onChange = [this](int index) {
        if (onTimeTempo)
            onTimeTempo(index);
    };
    timeTempo_.setVisible(false);
    // Tab starts at the switch even when it sits on the second row, under the buttons.
    timeTempo_.setExplicitFocusOrder(1);
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
    addChildComponent(timeTempo_);
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

void CardLayoutEditBar::setTimeTempo(std::optional<int> index) {
    const bool shown = index.has_value();
    if (shown)
        timeTempo_.setSelectedIndex(*index, juce::dontSendNotification);
    if (shown != timeTempo_.isVisible()) {
        timeTempo_.setVisible(shown);
        resized();
    }
}

void CardLayoutEditBar::setTwoRows(bool twoRows) {
    if (twoRows == twoRows_)
        return;
    twoRows_ = twoRows;
    resized();
}

// Right to left: Done, Cancel, Apply to, Preset, then the switch with what is left; on a narrow card the
// switch is the second row, under the buttons and right-aligned with them.
void CardLayoutEditBar::resized() {
    auto area = getLocalBounds().reduced(6, 0);
    const bool withSwitch = timeTempo_.isVisible();
    if (withSwitch && twoRows_)
        timeTempo_.setBounds(
            area.removeFromBottom(kHeight).removeFromRight(juce::jmin(kTimeTempoWidth, area.getWidth())));
    if (twoRows_)
        area = area.removeFromTop(kHeight);
    const auto take = [&](int width) {
        auto piece = area.removeFromRight(width);
        area.removeFromRight(kGap);
        return piece;
    };
    done_.setBounds(take(kButtonWidth));
    cancel_.setBounds(take(kButtonWidth));
    applyTo_.setBounds(take(kApplyToWidth));
    preset_.setBounds(take(kPresetWidth));
    if (withSwitch && !twoRows_)
        timeTempo_.setBounds(area.removeFromRight(juce::jmin(kTimeTempoWidth, area.getWidth())));
}

} // namespace synth::ui
