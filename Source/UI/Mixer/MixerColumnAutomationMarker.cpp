// MixerColumnAutomationMarker.cpp
//
// The "this fader or knob has an automation lane" marker on a mixer column (docs/mixer/panel.md#automation-markers),
// the same glyph and rules as on a module card (docs/layout/module-card.md#automated-marker): one query per column
// on the existing 10 Hz meter tick, a fade only on a change, a repaint only while a fade runs. The column never
// touches the timeline itself; the answer comes through GraphEditor::onQueryAutomatedParamsForNode, like the MIDI
// mappings.

#include "MixerColumnComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

void MixerColumnComponent::refreshAutomatedMarkers() {
    if (graphEditor_ == nullptr || !graphEditor_->onQueryAutomatedParamsForNode)
        return;
    const double now = synth::ui::AutomatedMarkerFade::nowMs();
    const bool reduced = synth::ui::prefersReducedMotion();
    const auto automated = graphEditor_->onQueryAutomatedParamsForNode(nodeId_);
    bool changed = false;
    for (auto& e : midiLearnableEntries_) {
        if (e.isSolo || e.param == nullptr || e.component == nullptr)
            continue;
        const bool on = automated.count(e.param->paramID) > 0;
        changed |= e.automated.update(*e.component, on, e.param->getName(100), now, reduced);
    }
    if (!changed)
        return;
    if (automatedTicker_ == nullptr)
        automatedTicker_ = std::make_unique<synth::ui::AutomatedMarkerTicker>(*this);
    juce::Component::SafePointer<MixerColumnComponent> safeThis(this);
    const auto repaintMarkers = [safeThis] {
        if (safeThis == nullptr)
            return;
        const double t = synth::ui::AutomatedMarkerFade::nowMs();
        for (const auto& e : safeThis->midiLearnableEntries_)
            if (e.component != nullptr && (e.automated.fade.isAutomated() || !e.automated.fade.isSettled(t)))
                safeThis->repaint(
                    synth::ui::automatedMarkerRect(safeThis->getLocalArea(e.component, e.component->getLocalBounds()))
                        .expanded(2));
    };
    repaintMarkers();
    automatedTicker_->run(repaintMarkers);
}

void MixerColumnComponent::paintAutomatedMarkers(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour textPrimary = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colour(0xffE8E8F0);
    const double now = synth::ui::AutomatedMarkerFade::nowMs();
    for (const auto& e : midiLearnableEntries_) {
        const float level = e.automated.fade.level(now);
        if (level <= 0.0f || e.component == nullptr)
            continue;
        // A knob in the sends list is two levels down (column -> sendList_ -> knob), hence getLocalArea.
        const auto bounds = getLocalArea(e.component, e.component->getLocalBounds());
        if (sendViewport_.isParentOf(e.component) &&
            (!sendViewport_.isVisible() || !sendViewport_.getBounds().contains(bounds.getCentre())))
            continue;
        synth::ui::paintAutomatedMarker(g, bounds, synth::ui::automatedMarkerColour(textPrimary, level));
    }
}

} // namespace synth::ui
