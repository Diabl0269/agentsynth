// ModuleComponentAutomationMarker.cpp
//
// The "this knob has an automation lane" marker on a card (docs/layout/module-card.md#automated-marker). Mirrors the
// MIDI-mapped badge: the card asks the host ONCE per tick (owner.onQueryAutomatedParamsForNode) which of its
// parameters are automated, every registered control moves its marker fade towards the answer, and the card repaints
// only while a fade is running. The control's tooltip gains an "Automated: <name>" line and its screen-reader
// description says "Automated" for as long as the lane exists.

#include "ModuleComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {

// The name the tooltip gives the automated parameter.
juce::String automatedNameFor(juce::AudioProcessorParameter* param, const juce::String& baseTooltip,
                              const juce::String& paramId) {
    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
        return ranged->getName(100);
    return baseTooltip.isNotEmpty() ? baseTooltip : paramId;
}

// A control inside a hidden section (or a hidden card body view) paints no marker.
bool isChainVisible(const juce::Component& c, const juce::Component& card) {
    for (const auto* p = &c; p != nullptr && p != &card; p = p->getParentComponent())
        if (!p->isVisible())
            return false;
    return true;
}

} // namespace

void ModuleComponent::MidiLearnableRegistry::applyTooltip(Entry& e) {
    auto* client = dynamic_cast<juce::SettableTooltipClient*>(e.component);
    if (client == nullptr)
        return;
    juce::String text = e.baseTooltip;
    if (e.mapped)
        text = text.isNotEmpty() ? text + " - " + e.tooltip : e.tooltip;
    if (e.marker.isAutomated()) {
        const juce::String line = "Automated: " + e.automatedName;
        text = text.isNotEmpty() ? text + "\n" + line : line;
    }
    client->setTooltip(text);
}

bool ModuleComponent::MidiLearnableRegistry::refreshAutomated(const std::set<juce::String>& automatedParamIds,
                                                              double nowMs, bool reducedMotion) {
    bool changed = false;
    for (auto& e : entries_) {
        const bool automated = automatedParamIds.count(e.paramId) > 0;
        if (automated == e.marker.isAutomated())
            continue;
        e.marker.setAutomated(automated, nowMs, reducedMotion);
        e.automatedName = automated ? automatedNameFor(e.param, e.baseTooltip, e.paramId) : juce::String();
        if (automated) {
            e.descriptionBeforeAutomated = e.component->getDescription();
            e.component->setDescription(e.descriptionBeforeAutomated.isNotEmpty()
                                            ? e.descriptionBeforeAutomated + ". Automated"
                                            : juce::String("Automated"));
        } else {
            e.component->setDescription(e.descriptionBeforeAutomated);
            e.descriptionBeforeAutomated = {};
        }
        applyTooltip(e);
        changed = true;
    }
    return changed;
}

void ModuleComponent::refreshAutomatedMarkers() {
    if (module == nullptr || !owner.onQueryAutomatedParamsForNode)
        return;
    const double now = synth::ui::AutomatedMarkerFade::nowMs();
    const auto automated = owner.onQueryAutomatedParamsForNode(nodeId);
    const bool changed = midiLearnableRegistry_.refreshAutomated(automated, now, synth::ui::prefersReducedMotion());
    if (!changed)
        return;
    // Repaint the markers' own corners on every frame of the fade (one frame source per card, made on first use);
    // the last frame repaints once more as the fade lands.
    if (automatedTicker_ == nullptr)
        automatedTicker_ = std::make_unique<synth::ui::AutomatedMarkerTicker>(*this);
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    const auto repaintMarkers = [safeThis] {
        if (safeThis == nullptr)
            return;
        for (const auto& e : safeThis->midiLearnableRegistry_.entries())
            if (!e.marker.isSettled(synth::ui::AutomatedMarkerFade::nowMs()) || e.marker.isAutomated())
                safeThis->repaint(
                    synth::ui::automatedMarkerRect(safeThis->getLocalArea(e.component, e.component->getLocalBounds()))
                        .expanded(2));
    };
    repaintMarkers();
    automatedTicker_->run(repaintMarkers);
}

void ModuleComponent::paintAutomatedMarkers(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour textPrimary = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colour(0xffE8E8F0);
    const double now = synth::ui::AutomatedMarkerFade::nowMs();
    for (const auto& e : midiLearnableRegistry_.entries()) {
        const float level = e.marker.level(now);
        if (level <= 0.0f || !isChainVisible(*e.component, *this))
            continue;
        synth::ui::paintAutomatedMarker(g, getLocalArea(e.component, e.component->getLocalBounds()),
                                        synth::ui::automatedMarkerColour(textPrimary, level));
    }
}
