// ModuleComponentModRings.cpp -- the card's modulation display: the pending-drop-target outline and the
// live modulation on every continuous control, a ring with its depth band on a knob and a bar beside
// the slot on a fader. Both read the same routing snapshot (GraphEditor::getCachedModDisplayInfo) and
// are painted from the card's paint(). docs/modules/modulation.md#modulation-rings-on-knobs.
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "ModuleComponentModBand.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

namespace {

const synth::theme::Colors& themeColours(juce::Component& card) {
    static const synth::theme::Colors fallback{};
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&card.getLookAndFeel());
    return lf != nullptr ? lf->getTheme().colors : fallback;
}

// The fader's slider proportion for a parameter's normalised value: through the real value, so a fader
// whose slider range differs from the parameter's (the ADSR times' display skew) still lines up.
double faderProportionFor(synth::ui::CardFader& fader, const juce::RangedAudioParameter* param, float norm) {
    if (param == nullptr)
        return norm;
    return fader.valueToProportionOfLength(param->convertFrom0to1(juce::jlimit(0.0f, 1.0f, norm)));
}

// A fader's live modulation: a bar beside the slot from the base value to base + CV, positive or
// negative colour by the CV's sign. The bar sits inside the fader's own bounds.
void paintFaderModulation(juce::Graphics& g, synth::ui::CardFader& fader, const juce::RangedAudioParameter* param,
                          float baseNorm, float modNorm, const synth::theme::Colors& colours, bool positive,
                          bool hovered) {
    const auto bar =
        fader.modBarBetween(faderProportionFor(fader, param, baseNorm), faderProportionFor(fader, param, modNorm));
    synth::ui::paintCardFaderModBar(g, bar.translated((float)fader.getX(), (float)fader.getY()),
                                    positive ? colours.modRingPositive : colours.modRingNegative, hovered);
}

} // namespace

// The pending-drop-target ring (a released cable would land on this knob, or an outline round a
// fader's slot and bar) and the live modulation, both driven by ModuleBase::getModulationTargets().
// `mod`/`jackAccentColour` are paint()'s own locals, computed once there.
void ModuleComponent::paintModulationRings(juce::Graphics& g, ModuleBase* mod, juce::Colour jackAccentColour) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    if (mod != nullptr && modDropTargetChannel >= 0) {
        for (const auto& t : mod->getModulationTargets()) {
            if (t.channelIndex != modDropTargetChannel)
                continue;
            const int si = sliderIndexForModTarget(t);
            // si now resolves even when the knob is hidden behind a swapped-in BPM *Div
            // combo (so a jack still lands there) -- but there is no ring to draw on a combo, so
            // skip painting one rather than drawing a circle over it.
            if (si < 0 || !sliders[si]->isVisible())
                break;
            const auto b = sliders[si]->getBounds().toFloat();
            g.setColour(jackAccentColour);
            if (auto* fader = dynamic_cast<synth::ui::CardFader*>(sliders[si])) {
                const auto area = fader->travelBounds().getUnion(fader->modBarTrack()).expanded(4.0f);
                g.drawRoundedRectangle(area.translated(b.getX(), b.getY()), 4.0f, 2.0f);
                break;
            }
            const float radius = std::min(b.getWidth(), b.getHeight()) / 2.0f - 6.0f;
            const auto c = modRingCentreFor(*sliders[si], b);
            g.drawEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);
            break;
        }
    }

    if (mod == nullptr)
        return;

    auto targets = mod->getModulationTargets();
    const auto& modInfo = owner.getCachedModDisplayInfo();

    for (const auto& info : modInfo) {
        if (info.destNodeID != nodeId || info.isBypassed)
            continue;

        // The ring belongs on the knob of the parameter the routed jack DRIVES — resolved through
        // the target's bound parameter, not its jack label. "Rate" is the Flanger's jack label
        // and "Rate (Hz)" its knob; matching the label against the knob's name silently drew no
        // ring on every module whose labels carry no unit.
        const ModulationTarget* target = nullptr;
        for (const auto& t : targets) {
            if (t.channelIndex == info.destChannelIndex) {
                target = &t;
                break;
            }
        }
        if (target == nullptr)
            continue;

        const int si = sliderIndexForModTarget(*target);
        // Same reasoning as the drop-target ring above -- a swapped-in BPM combo still
        // resolves an si (real jack anchor), but has no ring to paint over it.
        if (si < 0 || !sliders[si]->isVisible())
            continue;

        const auto* param = mod->parameterForModTarget(*target);
        const float baseNorm = param != nullptr ? param->getValue() : 0.5f;
        const float modNorm = juce::jlimit(0.0f, 1.0f, baseNorm + info.modSignalValue);

        // This routing is correlated with a hover (either a cable hovered on the canvas that lands
        // here, or this very control being hovered) -- widen/brighten it.
        const auto& hovered = owner.getHoveredModTarget();
        const bool isHovered =
            hovered.has_value() && hovered->nodeId == nodeId && hovered->channel == info.destChannelIndex;

        if (auto* fader = dynamic_cast<synth::ui::CardFader*>(sliders[si])) {
            paintFaderModulation(g, *fader, param, baseNorm, modNorm, themeColours(*this), info.modSignalValue >= 0.0f,
                                 isHovered);
            continue;
        }

        // Serum-style mod ring drawn by the themed LnF (270 degree sweep + theme tokens). Guarded:
        // headless tests without our LnF simply skip the ring.
        if (lf == nullptr)
            continue;

        auto sliderBounds = sliders[si]->getBounds().toFloat();
        const auto centre = modRingCentreFor(*sliders[si], sliderBounds);
        const float radius = modRingRadiusFor(sliderBounds);

        // The reachable-range band goes UNDER the live ring, one per routing (two
        // routings on one knob -> two bands, never summed) -- visible even at rest, since it
        // answers "how far could this move", not "where is it now".
        const auto band = synth::ui::modDepthBandRange(baseNorm, info.amount, info.sourceBipolar);
        const bool bandNegative = synth::ui::modDepthBandUsesNegativeColour(info.amount, info.sourceBipolar);
        const auto bandColour =
            bandNegative ? lf->getTheme().colors.modRingNegative : lf->getTheme().colors.modRingPositive;
        lf->drawModulationDepthBand(g, centre, radius, band.startNorm, band.endNorm, bandColour);

        lf->drawModulationRing(g, centre, radius, baseNorm, modNorm, info.modSignalValue >= 0.0f, isHovered);
    }
}
