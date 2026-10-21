// DrumMachineComponent: what a screen reader says. The grid is one group whose value is the focused cell, so arrowing
// speaks "Kick, step 5, hit, loudness 100, accent" or "Kick, step 5, empty".

#include "DrumMachineComponent.h"

#include "UI/ModuleViews/ModuleViewAccessibility.h"

namespace synth::ui {

juce::String DrumMachineComponent::getAccessibilityValueText() const {
    const int pitch = getLanePitch(focus_.row);
    if (!clipId_.isValid() || pitch < 0)
        return "No drum rows";
    const auto text = slots_[(size_t)focus_.row].lane.name + ", step " + juce::String(focus_.step + 1);
    const int velocity = getCellVelocity(focus_.row, focus_.step);
    if (velocity == 0)
        return text + ", empty";
    auto described = text + ", hit, loudness " + juce::String(velocity);
    if (drumgrid::isAccent(velocity))
        described += ", accent";
    if (rowSilenced(pitch))
        described += ", row silenced";
    return described;
}

std::unique_ptr<juce::AccessibilityHandler> DrumMachineComponent::createAccessibilityHandler() {
    return makeValueTextHandler(*this, [this] { return getAccessibilityValueText(); });
}

void DrumMachineComponent::focusGained(FocusChangeType) {
    repaint();
    announceFocus();
}

void DrumMachineComponent::focusLost(FocusChangeType) { repaint(); }

} // namespace synth::ui
