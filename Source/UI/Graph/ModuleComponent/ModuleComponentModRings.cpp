// ModuleComponentModRings.cpp -- the card's modulation display: the pending-drop-target outline and the
// live modulation on every continuous control, a ring with its depth band on a knob and a bar beside
// the slot on a fader. Both read the same routing snapshot (GraphEditor::getCachedModDisplayInfo) and
// are drawn over the card's cached picture (its live layer), never into it. Also the ring's own hit test for the
// drag that adjusts a routing's amount.
// docs/modules/modulation.md#modulation-rings-on-knobs.
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "ModuleComponentModBand.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/CardOutline.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/InstrumentChrome.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
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

// Everything drawn between construction and destruction is drawn at `alpha` (a ring fading in or out).
class AlphaLayer {
public:
    AlphaLayer(juce::Graphics& g, float alpha)
        : g_(g)
        , on_(alpha < 1.0f) {
        if (on_)
            g_.beginTransparencyLayer(alpha);
    }
    ~AlphaLayer() {
        if (on_)
            g_.endTransparencyLayer();
    }
    AlphaLayer(const AlphaLayer&) = delete;
    AlphaLayer& operator=(const AlphaLayer&) = delete;

private:
    juce::Graphics& g_;
    bool on_;
};

// The card's picture leaves a ringed knob out, so the live layer draws it over its ring: the order the card's own paint
// gives (rings, then the knobs). It is drawn from its own picture, so a ring frame paints no knob. A knob not held yet
// (no ring step has met it) is still in the card's picture.
void paintKnobOverRing(juce::Graphics& g, juce::Component& card, juce::Component& knob) {
    auto* picture = dynamic_cast<synth::ui::RingedKnobPicture*>(knob.getCachedComponentImage());
    if (picture == nullptr || !knob.isVisible())
        return;
    const juce::Graphics::ScopedSaveState state(g);
    g.setOrigin(card.getLocalPoint(&knob, juce::Point<int>()));
    if (!g.reduceClipRegion(knob.getLocalBounds()))
        return;
    picture->paint(g);
}

// What a ring draws reaches this far past its circle: half the widest stroke (hovered) and a pixel or two of
// anti-aliasing. Nothing of it is drawn outside the box this gives.
juce::Rectangle<int> ringDrawnBounds(juce::Point<float> centre, float radius, const synth::theme::Metrics& metrics) {
    const float reach = radius + (metrics.knobRingWidth + metrics.modRingHoverWidthBoost) * 0.5f + 2.0f;
    return juce::Rectangle<float>(centre.x - reach, centre.y - reach, reach * 2.0f, reach * 2.0f)
        .getSmallestIntegerContainer();
}

// A fader's bar sits in its own track; the hovered bar is half a pixel wider each side.
juce::Rectangle<int> barDrawnBounds(const synth::ui::CardFader& fader) {
    return fader.modBarTrack()
        .translated((float)fader.getX(), (float)fader.getY())
        .expanded(2.0f)
        .getSmallestIntegerContainer();
}

} // namespace

// One step of this card's rings: follows the routings (a ring's fade in or out), and repaints the area of each ring
// whose drawing changed since it was last repainted: the ring and its band only. The rings are the card's live layer,
// so that repaint keeps the card's picture: a ring frame draws the picture and the ring, and re-rasters nothing.
// Returns true when anything repainted. A driven control this card cannot place repaints the whole card's live layer.
bool ModuleComponent::tickModulationRings() {
    const auto repaintRing = [this](juce::Rectangle<int> area) {
        if (rasterCache != nullptr)
            rasterCache->repaintLive(area);
        else
            repaint(area);
    };
    const auto& infos = owner.getCachedModDisplayInfo();
    ringFades_.sync(infos, nodeId);
    auto* mod = dynamic_cast<ModuleBase*>(module);
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto& hovered = owner.getHoveredModTarget();
    const int hoveredChannel = hovered.has_value() && hovered->nodeId == nodeId ? hovered->channel : -1;
    bool repainted = false;
    for (const auto& step : ringFades_.steps(infos, nodeId, hoveredChannel)) {
        int si = -1;
        PadKnob pad;
        if (mod != nullptr)
            for (const auto& target : mod->getModulationTargets())
                if (target.channelIndex == step.channel) {
                    si = sliderIndexForModTarget(target);
                    if (si < 0)
                        pad = padKnobForTarget(target);
                    break;
                }
        juce::Slider* knob = pad.slider;
        if (knob == nullptr && si >= 0 && sliders[si]->isVisible())
            knob = sliders[si];
        if (knob == nullptr) {
            // A control on an unselected tab or behind a swapped-in combo draws no ring. Only one this card cannot
            // place at all (a plugin knob) is unknown, so the whole card repaints.
            if (si < 0 && pad.slider == nullptr && step.repaint) {
                repaintRing(getLocalBounds());
                repainted = true;
            }
            continue;
        }
        ringFades_.holdKnob(step.channel, *knob);
        const auto* fader = dynamic_cast<synth::ui::CardFader*>(knob);
        if (!step.repaint)
            continue;
        repainted = true;
        const auto bounds = pad.slider != nullptr ? pad.bounds : knob->getBounds();
        if (fader != nullptr)
            repaintRing(barDrawnBounds(*fader));
        else if (lf != nullptr)
            repaintRing(ringDrawnBounds(modRingCentreFor(*knob, bounds.toFloat()), modRingRadiusFor(bounds.toFloat()),
                                        lf->getTheme().metrics));
        else
            repaintRing(bounds.expanded(8));
    }
    return repainted;
}

// The pending-drop-target ring (a released cable would land on this knob, or an outline round a
// fader's slot and bar) and the live modulation, both driven by ModuleBase::getModulationTargets().
// `mod`/`jackAccentColour` are paint()'s own locals, computed once there.
void ModuleComponent::paintModulationRings(juce::Graphics& g, ModuleBase* mod, juce::Colour jackAccentColour,
                                           bool overKnobs) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    // A cable over a plugin card's body: a drop there searches all its parameters, so the whole card is the target.
    if (modDropTargetChannel == kHostedCardBodyDropChannel) {
        g.setColour(jackAccentColour);
        g.drawRoundedRectangle(synth::ui::cardBodyRect(getLocalBounds().toFloat()),
                               synth::theme::themeOf(*this).metrics.cornerRadius, 2.0f);
    }

    // A plugin knob with no slot yet (see getModTargetPortForPoint) is named by its slider index instead.
    if (const int si = kUnboundHostedKnobChannelBase - modDropTargetChannel;
        modDropTargetChannel <= kUnboundHostedKnobChannelBase && si < sliders.size() && sliders[si]->isVisible()) {
        const auto b = sliders[si]->getBounds().toFloat();
        const float radius = std::min(b.getWidth(), b.getHeight()) / 2.0f - 6.0f;
        const auto c = modRingCentreFor(*sliders[si], b);
        g.setColour(jackAccentColour);
        g.drawEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);
    }

    // A pad knob with no cable slot yet is named by its registry entry (padSlotPortForPoint).
    if (const auto entry = (size_t)(kUnboundPadKnobChannelBase - modDropTargetChannel);
        modDropTargetChannel <= kUnboundPadKnobChannelBase && entry < midiLearnableRegistry_.entries().size())
        if (auto* knob = dynamic_cast<juce::Slider*>(midiLearnableRegistry_.entries()[entry].component);
            knob != nullptr && knob->isShowing()) {
            const auto b = getLocalArea(knob, knob->getLocalBounds()).toFloat();
            const float radius = modRingRadiusFor(b);
            const auto c = modRingCentreFor(*knob, b);
            g.setColour(jackAccentColour);
            g.drawEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);
        }

    if (mod != nullptr && modDropTargetChannel >= 0) {
        for (const auto& t : mod->getModulationTargets()) {
            if (t.channelIndex != modDropTargetChannel)
                continue;
            if (const auto pad = padKnobForTarget(t); pad.slider != nullptr) {
                const auto b = pad.bounds.toFloat();
                const float radius = modRingRadiusFor(b);
                const auto c = modRingCentreFor(*pad.slider, b);
                g.setColour(jackAccentColour);
                g.drawEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);
                break;
            }
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

    if (mod == nullptr || ringFades_.drawsNothing())
        return;
    // The live routings, plus the last-seen ones of a ring that is fading out after its modulation was removed. This
    // runs on every paint that crosses the card (the live layer), so a card with no ring stops here.
    const auto rings = ringFades_.rings(owner.getCachedModDisplayInfo(), nodeId);
    if (rings.empty())
        return;
    auto targets = mod->getModulationTargets();
    std::vector<juce::Slider*> ringedKnobs;
    for (const auto& ring : rings) {
        const auto& info = ring.info;

        // The ring belongs on the knob of the parameter the routed jack DRIVES — resolved through
        // the target's bound parameter, not its jack label. "Rate" is the Flanger's jack label
        // and "Rate" its knob; matching the label against the knob's name silently drew no
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
        // resolves an si (real jack anchor), but has no ring to paint over it. A pad cable rings the pad knob only
        // while the card shows that pad.
        juce::Slider* ringKnob = si >= 0 && sliders[si]->isVisible() ? sliders[si] : nullptr;
        auto ringBounds = ringKnob != nullptr ? ringKnob->getBounds() : juce::Rectangle<int>();
        // A pad cable's ring follows what its knob draws: the knob's own fade (Reduce Motion), the pick's cross-fade
        // between the old pad's ring and the new one, and the value the knob is gliding through.
        float padAlpha = 1.0f;
        bool padShown = false;
        if (si < 0) {
            if (const auto pad = padKnobForTarget(*target); pad.slider != nullptr) {
                ringKnob = pad.slider;
                ringBounds = pad.bounds;
                padShown = true;
                padAlpha =
                    pad.slider->getAlpha() * (instrumentChrome_ != nullptr ? instrumentChrome_->padRingAlpha() : 1.0f);
            } else if (instrumentChrome_ != nullptr)
                if (const auto leaving = instrumentChrome_->leavingPadRing(target->paramId);
                    leaving && leaving->knob != nullptr) {
                    ringKnob = leaving->knob;
                    ringBounds = getLocalArea(ringKnob, ringKnob->getLocalBounds());
                    padAlpha = leaving->alpha;
                }
        }
        // A ring outside what this paint draws (the cable tick repaints most of the view) costs nothing.
        if (ringKnob == nullptr || !g.clipRegionIntersects(ringBounds.expanded(8)))
            continue;
        if (overKnobs && std::find(ringedKnobs.begin(), ringedKnobs.end(), ringKnob) == ringedKnobs.end())
            ringedKnobs.push_back(ringKnob);
        const AlphaLayer layer(g, ring.alpha);
        const AlphaLayer panelLayer(g, padAlpha);

        const auto* param = mod->parameterForModTarget(*target);
        float baseNorm = param != nullptr ? param->getValue() : 0.5f;
        if (padShown)
            baseNorm = (float)ringKnob->valueToProportionOfLength(ringKnob->getValue());
        // A plugin parameter's own value moves with the modulation; the ring starts from its resting value.
        if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(mod))
            baseNorm = hosted->modSlotBase(target->paramId).value_or(baseNorm);
        const float modNorm = juce::jlimit(0.0f, 1.0f, baseNorm + info.modSignalValue);

        // This routing is correlated with a hover (either a cable hovered on the canvas that lands
        // here, or this very control being hovered) -- widen/brighten it.
        const auto& hovered = owner.getHoveredModTarget();
        const bool isHovered =
            hovered.has_value() && hovered->nodeId == nodeId && hovered->channel == info.destChannelIndex;

        if (auto* fader = dynamic_cast<synth::ui::CardFader*>(ringKnob)) {
            paintFaderModulation(g, *fader, param, baseNorm, modNorm, themeColours(*this), info.modSignalValue >= 0.0f,
                                 isHovered);
            continue;
        }

        // Serum-style mod ring drawn by the themed LnF (270 degree sweep + theme tokens). Guarded:
        // headless tests without our LnF simply skip the ring.
        if (lf == nullptr)
            continue;

        auto sliderBounds = ringBounds.toFloat();
        const auto centre = modRingCentreFor(*ringKnob, sliderBounds);
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
    for (auto* knob : ringedKnobs)
        paintKnobOverRing(g, *this, *knob);
}

// CardControlGestures::wantsModAmountGesture: claim the gesture when this control drives an
// attenuverter AND the click is either Alt-modified or lands within +-5px of the ring's own radius (the
// same geometry paintModulationRings draws it at -- see modRingCentreFor/modRingRadiusFor), or, on a
// fader, on its modulation bar's strip. `knob` is the control itself (its local bounds and layout give
// the ring centre).
bool ModuleComponent::wantsModAmountGestureFor(juce::RangedAudioParameter* param, const juce::Slider& knob,
                                               const juce::MouseEvent& e) const {
    if (firstAttenuverterForChannel(destChannelForKnob(param, knob)).uid == 0)
        return false;
    if (e.mods.isAltDown())
        return true;
    if (const auto* fader = dynamic_cast<const synth::ui::CardFader*>(&knob))
        return fader->hitsModBar(e.position);

    const auto bounds = knob.getLocalBounds().toFloat();
    const float radius = modRingRadiusFor(bounds);
    if (radius <= 0.0f)
        return false;
    const auto centre = modRingCentreFor(knob, bounds);
    const float dist = centre.getDistanceFrom(e.position);
    return std::abs(dist - radius) <= 5.0f;
}
