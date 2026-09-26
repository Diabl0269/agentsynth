// The modulation hover chip -- "<source> · <+NN%>" under a knob whose routing is hover-correlated
// (a hovered knob-landing cable, or the knob itself). Painted in paintOverChildren so it sits above
// the next row's knob labels instead of under them. docs/modules/modulation.md#modulation-rings-on-knobs.

#include "ModuleComponentModChip.h"
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
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

    for (const auto& info : owner.getCachedModDisplayInfo()) {
        if (info.destNodeID != nodeId || info.destChannelIndex != hovered->channel || info.isBypassed)
            continue;
        // Source name the way the mod matrix labels it: find the live routing this display entry
        // came from (ModulationDisplayInfo does not carry the source node).
        for (const auto& routing : owner.getCachedModRoutings()) {
            if (routing.destNodeID != nodeId || routing.destChannelIndex != info.destChannelIndex ||
                routing.kind != AudioEngine::RoutingKind::AttenuverterChain)
                continue;
            auto* srcNode = owner.getAudioEngine().getGraph().getNodeForId(routing.sourceNodeID);
            if (srcNode == nullptr || srcNode->getProcessor() == nullptr)
                return;
            const auto chipText = synth::ui::formatModHoverChipText(srcNode->getProcessor()->getName(), info.amount);

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
}
