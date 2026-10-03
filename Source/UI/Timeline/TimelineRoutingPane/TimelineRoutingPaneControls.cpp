// Concern: RoutingComboButton -- painting and focus behaviour.
#include "TimelineRoutingPaneControls.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

RoutingComboButton::RoutingComboButton(const juce::String& name)
    : juce::Button(name) {
    // A keyboard stop of the routing pane, but a click must never move keyboard focus off the panel's focus root.
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void RoutingComboButton::setWarning(bool warning) {
    if (warning_ == warning)
        return;
    warning_ = warning;
    repaint();
}

void RoutingComboButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto& colors = theme.colors;
    synth::theme::ChipState state;
    state.hovered = highlighted;
    state.down = down;
    state.warning = warning_;
    synth::theme::paintChip(g, getLocalBounds().toFloat(), theme, state);

    auto area = getLocalBounds().reduced(7, 0);
    const auto chevron = area.removeFromRight(10).toFloat();
    synth::theme::paintComboChevron(g, chevron.getCentre(), colors.textMuted);

    area.removeFromRight(4);
    g.setColour(warning_ ? colors.warning : colors.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(getButtonText(), area, juce::Justification::centredLeft, true);
}

} // namespace synth::ui
