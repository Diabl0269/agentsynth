// The modulation hover chip -- "<source> · <+NN%>" under a knob whose routing is hover-correlated
// (a hovered knob-landing cable, or the knob itself). Painted in paintOverChildren so it sits above
// the next row's knob labels instead of under them. docs/modules/modulation.md#modulation-rings-on-knobs.

#include "ModuleComponentModChip.h"
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

void ModuleComponent::paintOverChildren(juce::Graphics& g) { paintModHoverChip(g); }

void ModuleComponent::paintModHoverChip(juce::Graphics& g) {
    const auto& hovered = owner.getHoveredModTarget();
    if (!hovered.has_value() || hovered->nodeId != nodeId)
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    auto* mod = dynamic_cast<ModuleBase*>(module);
    if (lf == nullptr || mod == nullptr)
        return;

    int si = -1;
    for (const auto& t : mod->getModulationTargets())
        if (t.channelIndex == hovered->channel) {
            si = sliderIndexForModTarget(t);
            break;
        }
    if (si < 0)
        return;

    // The source the dot's drag adjusts (the last one chosen), named the way the Mod Matrix names it:
    // the real module behind any macro port, not the first routing's raw source.
    const auto chosen = owner.getModDot().chosenAttenuverter(nodeId, hovered->channel);
    for (const auto& source : synth::ui::knobModSources(owner, nodeId, hovered->channel)) {
        if (source.attenuverterId != chosen || source.bypassed || source.sourceName.isEmpty())
            continue;
        const auto chipText = synth::ui::formatModHoverChipText(source.sourceName, source.amount);

        const auto sliderBounds = sliders[si]->getBounds().toFloat();
        const juce::Font chipFont(juce::FontOptions(lf->getTheme().type.monoFamily, 11.0f, juce::Font::plain));
        const float chipWidth = chipFont.getStringWidthFloat(chipText) + 12.0f;
        const juce::Rectangle<float> chipBounds(sliderBounds.getCentreX() - chipWidth * 0.5f,
                                                sliderBounds.getBottom() + 2.0f, chipWidth, 16.0f);

        // Clipped to the card, never squashed or re-centred.
        juce::Graphics::ScopedSaveState clipScope(g);
        g.reduceClipRegion(getLocalBounds());
        // Opaque even when a theme's surfaceHi is translucent -- the chip sits over the next
        // row's knob label and must hide it, not show it through.
        const auto& colors = lf->getTheme().colors;
        g.setColour(colors.surface.overlaidWith(colors.surfaceHi).withAlpha(1.0f));
        g.fillRoundedRectangle(chipBounds, lf->getTheme().metrics.cornerRadiusSmall);
        g.setColour(lf->getTheme().colors.textPrimary);
        g.setFont(chipFont);
        g.drawText(chipText, chipBounds, juce::Justification::centred, true);
        return;
    }
}
