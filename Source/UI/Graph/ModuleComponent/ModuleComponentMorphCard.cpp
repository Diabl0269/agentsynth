// ModuleComponentMorphCard.cpp -- builds the Custom section of the cards whose Custom type is a list of shapes on a
// morph map (the Filter's Custom type, the LFO's Custom shape): the morph map, the Edit shapes button and the Show
// Morph Map pill, which live in MorphShapesCard. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units.
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ConnectionIndex.h"
#include "Mixer/ChannelFlows/UpstreamPitchSource.h"
#include "ModuleComponent.h"
#include "Modules/LFOModule.h"
#include "MorphShapesCard/MorphShapesCard.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
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
} // namespace

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
        return;
    }
    host.customActive = [this] { return lfoShapeIsCustom(module); };
    morphCard_ = std::make_unique<synth::ui::MorphShapesCard>(
        std::move(host), *lfo,
        std::make_shared<synth::ui::LfoShapesAdapter>(*lfo, undoManager, graph, nodeId, cardTitle()),
        LFOModule::kMorphXChannel, LFOModule::kMorphYChannel);
    stylePill();
}

/** Opens (or raises) the shape list editor of this card's module on `shapeIndex`. */
juce::DialogWindow* ModuleComponent::openShapesWindow(int shapeIndex) {
    return morphCard_ != nullptr ? morphCard_->openEditor(shapeIndex) : nullptr;
}
