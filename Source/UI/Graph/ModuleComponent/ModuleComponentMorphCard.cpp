// ModuleComponentMorphCard.cpp -- builds the Custom section of the cards whose Custom type is a list of shapes on a
// morph map (the Filter's Custom type, the LFO's Custom shape): the morph map, the Edit shapes button and the Show
// Morph Map pill, which live in MorphShapesCard. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units.
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ConnectionIndex.h"
#include "Mixer/ChannelFlows/UpstreamPitchSource.h"
#include "ModuleComponent.h"
#include "ModuleComponentModBand.h"
#include "Modules/LFOModule.h"
#include "MorphShapesCard/MorphShapesCard.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/ModuleViews/ShapeEditor/FilterShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"

namespace {
bool lfoShapeIsCustom(juce::AudioProcessor* module) {
    auto* shape = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module, "shape"));
    return shape != nullptr && shape->getIndex() == LFOModule::kCustomShapeIndex;
}

/** The Filter's Pitch input for the per-note modes: whether it is cabled, and a way to cable it from the Pitch source
    of the voice chain feeding the card (by the same path as a hand-drawn cable, so a poly fan is all 8 edges). */
synth::ui::FilterShapesAdapter::PitchHooks filterPitchHooks(GraphEditor& owner, juce::AudioProcessorGraph& graph,
                                                            juce::AudioProcessorGraph::NodeID nodeId,
                                                            FilterModule& filter) {
    synth::ui::FilterShapesAdapter::PitchHooks hooks;
    hooks.state = [&graph, nodeId] {
        const synth::ConnectionIndex cables(graph);
        if (synth::hasPitchCable(cables, graph, nodeId))
            return synth::ui::PitchInput::Connected;
        return synth::upstreamPitchSource(cables, graph, nodeId) ? synth::ui::PitchInput::Missing
                                                                 : synth::ui::PitchInput::MissingNoSource;
    };
    hooks.connect = [&owner, &graph, &filter, nodeId] {
        const synth::ConnectionIndex cables(graph);
        const auto source = synth::upstreamPitchSource(cables, graph, nodeId);
        if (!source)
            return;
        const int pitchJack = filter.mapInputChannel(FilterModule::kPitchBase).visibleJackIndex;
        owner.connectPorts(source->node, source->visibleJack, nodeId, pitchJack, false, false);
        owner.notifyModuleContentChanged();
    };
    return hooks;
}

/** The parameter the module's modulation target on `channel` drives (the hidden Morph X / Y knob's). */
const juce::RangedAudioParameter* morphStripParam(ModuleBase* module, int channel) {
    if (module == nullptr)
        return nullptr;
    for (const auto& target : module->getModulationTargets())
        if (target.channelIndex == channel)
            return module->parameterForModTarget(target);
    return nullptr;
}

/** What the map's bar shows for each routing on `channel`: the same range and live reading a knob's ring or a
    fader's bar is given (ModuleComponent::paintModulationRings), so the three always agree. */
std::vector<synth::ui::MorphMap::StripModulation> morphStripModulation(GraphEditor& owner,
                                                                       juce::AudioProcessorGraph::NodeID nodeId,
                                                                       const juce::RangedAudioParameter* param,
                                                                       int channel) {
    std::vector<synth::ui::MorphMap::StripModulation> out;
    if (param == nullptr)
        return out;
    const float base = param->getValue();
    const auto& hovered = owner.getHoveredModTarget();
    for (const auto& info : owner.getCachedModDisplayInfo()) {
        if (info.destNodeID != nodeId || info.destChannelIndex != channel || info.isBypassed)
            continue;
        const auto band = synth::ui::modDepthBandRange(base, info.amount, info.sourceBipolar);
        synth::ui::MorphMap::StripModulation m;
        m.baseNorm = base;
        m.modNorm = juce::jlimit(0.0f, 1.0f, base + info.modSignalValue);
        m.bandStart = band.startNorm;
        m.bandEnd = band.endNorm;
        m.positive = info.modSignalValue >= 0.0f;
        m.bandNegative = synth::ui::modDepthBandUsesNegativeColour(info.amount, info.sourceBipolar);
        m.hovered = hovered.has_value() && hovered->nodeId == nodeId && hovered->channel == channel;
        out.push_back(m);
    }
    return out;
}
} // namespace

/** Makes each map slider carry a fader's modulation gestures. Everything but the hit zones is the knob wiring
    (wireCardControlGestures, on the hidden Morph knob); the zones are the slider's modulation bar and landing dot,
    and the mod-dot press names the slider (not the hidden knob) so the amount readout appears beside it. */
void ModuleComponent::wireMorphStripGestures() {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    for (const int channel : {morphCard_->axisChannel(synth::ui::MorphMap::Axis::X),
                              morphCard_->axisChannel(synth::ui::MorphMap::Axis::Y)}) {
        auto* param = const_cast<juce::RangedAudioParameter*>(morphStripParam(mb, channel));
        const int knob = sliderParams.indexOf(param);
        if (knob < 0)
            continue;
        auto& gestures = morphCard_->stripGestures(channel);
        wireCardControlGestures(*sliders[knob], gestures, param);
        const auto axis = morphCard_->axisForChannel(channel);
        auto* map = &morphCard_->getMap();
        const auto onDot = [this, map, axis](const juce::MouseEvent& e) {
            const float scale = hitToleranceScale();
            const float reach = kKnobLandingDotDiameter * 0.5f * scale + 4.0f * scale;
            return map->stripAnchor(axis).getDistanceFrom(e.position) <= reach;
        };
        gestures.wantsModAmountGesture = [this, param, map, axis](const juce::MouseEvent& e) {
            return firstAttenuverterForParam(param).uid != 0 &&
                   (map->modBarAt(e.position) == axis || (e.mods.isAltDown() && map->stripAt(e.position) == axis));
        };
        gestures.wantsModDotGesture = [this, param, channel, onDot](const juce::MouseEvent& e) {
            return !e.mods.isCommandDown() && firstAttenuverterForParam(param).uid != 0 &&
                   getModDotButton(channel) != nullptr && onDot(e);
        };
        gestures.wantsCablePickupGesture = [this, channel, onDot](const juce::MouseEvent& e) {
            return getModDotButton(channel) != nullptr && onDot(e);
        };
        gestures.onModDotGesture = [this, channel](const juce::MouseEvent& e, int phase) {
            if (auto* strip = morphCard_->stripControlFor(channel))
                owner.getModDot().routeGesture(nodeId, channel, *strip, getModDotButton(channel), e, phase);
        };
    }
}

void ModuleComponent::createMorphShapesCard() {
    auto* filter = dynamic_cast<FilterModule*>(module);
    auto* lfo = dynamic_cast<LFOModule*>(module);
    if (filter == nullptr && lfo == nullptr)
        return;
    synth::ui::MorphShapesCard::Host host;
    host.card = this;
    host.relayout = [this] { updateLayout(); };
    host.makeRoom = [this] { owner.handleModuleResized(this); };
    host.settling = &settlingFootprint_;
    host.showMenu = [this](juce::PopupMenu& menu) { showContextMenuHook_(menu); };
    host.rememberView = [this](const std::function<void(synth::CardViewState&)>& edit) { rememberCardView(edit); };
    // The Morph X / Y knobs are the card body's "custom" (Filter) or "morph" (LFO) section.
    const char* knobSection = filter != nullptr ? "custom" : "morph";
    host.hideMorphKnobs = [this, knobSection](bool hidden) {
        if (cardBody_ != nullptr)
            cardBody_->setSectionHidden(knobSection, hidden);
    };
    host.title = [this] { return cardTitle(); };
    host.landingDotDiameter = kKnobLandingDotDiameter;
    host.stripModulation = [this](int channel) {
        auto* mb = dynamic_cast<ModuleBase*>(module);
        return morphStripModulation(owner, nodeId, morphStripParam(mb, channel), channel);
    };
    host.modDotTooltip = [this](int channel) {
        auto* button = getModDotButton(channel);
        return button != nullptr ? button->getTooltip() : juce::String();
    };
    auto& graph = owner.getAudioEngine().getGraph();
    // The constructor styles the footer toggles before this card exists, so its pill is styled here.
    const auto stylePill = [this] {
        if (cardBody_ != nullptr && cardBody_->hasFooter())
            synth::ui::setTogglePillStyle(morphCard_->getToggle(), true);
    };
    host.mapPlace = filter != nullptr ? "under the response view" : "under the curve editor";
    if (filter != nullptr) {
        host.customActive = [filter] { return filter->getCurrentFilterType() == FilterModule::kCustomTypeIndex; };
        auto adapter =
            std::make_shared<synth::ui::FilterShapesAdapter>(*filter, undoManager, graph, nodeId, cardTitle());
        adapter->setPitchHooks(filterPitchHooks(owner, graph, nodeId, *filter));
        morphCard_ = std::make_unique<synth::ui::MorphShapesCard>(std::move(host), *filter, std::move(adapter),
                                                                  FilterModule::kMorphXCVChannel,
                                                                  FilterModule::kMorphYCVChannel);
        freqResponseComponent->onCustomDoubleClick = [this] {
            if (morphCard_ != nullptr)
                morphCard_->openEditor(morphCard_->nearestShapeToCursor());
        };
        stylePill();
        wireMorphStripGestures();
        return;
    }
    host.customActive = [this] { return lfoShapeIsCustom(module); };
    morphCard_ = std::make_unique<synth::ui::MorphShapesCard>(
        std::move(host), *lfo,
        std::make_shared<synth::ui::LfoShapesAdapter>(*lfo, undoManager, graph, nodeId, cardTitle()),
        LFOModule::kMorphXChannel, LFOModule::kMorphYChannel);
    stylePill();
    wireMorphStripGestures();
}

/** Opens (or raises) the shape list editor of this card's module on `shapeIndex`. */
juce::DialogWindow* ModuleComponent::openShapesWindow(int shapeIndex) {
    return morphCard_ != nullptr ? morphCard_->openEditor(shapeIndex) : nullptr;
}
