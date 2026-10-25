// DrumMachineComponent: what a screen reader says. The grid is one group whose value is the focused cell, so arrowing
// speaks "Kick, step 5, hit, loudness 100, accent" or "Kick, step 5, empty".

#include "DrumMachineComponent.h"

#include "UI/ModuleViews/ModuleViewAccessibility.h"

namespace synth::ui {

// The cell's per-hit settings that are not their default, as ", chance 50%, repeat 3, nudge -10%, condition 2/4".
juce::String DrumMachineComponent::describeHitSettings(int pitch, int step) const {
    const auto it = cells_.find(drumgrid::cellKey(pitch, step));
    if (it == cells_.end())
        return {};
    juce::String text;
    for (int i = 1; i < kDrumLaneKindCount; ++i) {
        const auto& spec = drumLaneSpec(static_cast<DrumLaneKind>(i));
        const int value = drumLaneValueOf(spec.kind, it->second);
        if (value != drumLaneValueOf(spec.kind, drumgrid::CellHits{}))
            text += ", " + spec.word + " " + spec.textOf(value);
    }
    return text;
}

juce::String DrumMachineComponent::getAccessibilityValueText() const {
    const int pitch = getLanePitch(focus_.row);
    if (!clipId_.isValid() || pitch < 0)
        return "No drum rows";
    auto text = slots_[(size_t)focus_.row].lane.name + ", step " + juce::String(focus_.step + 1);
    // A cell past the drum's end repeats an earlier one: it reads that cell's hit, after saying so.
    const int source = ghostSourceStep(focus_.row, focus_.step);
    if (source >= 0)
        text += ", repeats step " + juce::String(source + 1);
    const int step = source >= 0 ? source : focus_.step;
    const auto stored = cells_.find(drumgrid::cellKey(pitch, step));
    const int velocity =
        source >= 0 ? (stored == cells_.end() ? 0 : stored->second.velocity) : getCellVelocity(focus_.row, focus_.step);
    if (velocity == 0)
        return text + ", empty";
    auto described = text + ", hit, loudness " + juce::String(velocity);
    if (drumgrid::isAccent(velocity))
        described += ", accent";
    described += describeHitSettings(pitch, step);
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
