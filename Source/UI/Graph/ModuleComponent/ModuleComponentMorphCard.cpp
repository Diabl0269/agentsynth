// ModuleComponentMorphCard.cpp -- builds the Custom section of the cards whose Custom type is a list of shapes on a
// morph map (the Filter's Custom type, the LFO's Custom shape): the morph map, the Edit shapes button and the Show
// Morph Map pill, which live in MorphShapesCard. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units.
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "Modules/LFOModule.h"
#include "MorphShapesCard/MorphShapesCard.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/ModuleViews/ShapeEditor/FilterShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"

namespace {
bool lfoShapeIsCustom(juce::AudioProcessor* module) {
    auto* shape = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module, "shape"));
    return shape != nullptr && shape->getIndex() == LFOModule::kCustomShapeIndex;
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
    host.mapPlace = filter != nullptr ? "under the response view" : "under the curve editor";
    if (filter != nullptr) {
        host.customActive = [filter] { return filter->getCurrentFilterType() == FilterModule::kCustomTypeIndex; };
        morphCard_ = std::make_unique<synth::ui::MorphShapesCard>(
            std::move(host), *filter,
            std::make_shared<synth::ui::FilterShapesAdapter>(*filter, undoManager, graph, nodeId, cardTitle()),
            FilterModule::kMorphXCVChannel, FilterModule::kMorphYCVChannel);
        freqResponseComponent->onCustomDoubleClick = [this] {
            if (morphCard_ != nullptr)
                morphCard_->openEditor(morphCard_->nearestShapeToCursor());
        };
        return;
    }
    host.customActive = [this] { return lfoShapeIsCustom(module); };
    morphCard_ = std::make_unique<synth::ui::MorphShapesCard>(
        std::move(host), *lfo,
        std::make_shared<synth::ui::LfoShapesAdapter>(*lfo, undoManager, graph, nodeId, cardTitle()),
        LFOModule::kMorphXChannel, LFOModule::kMorphYChannel);
}

/** Opens (or raises) the shape list editor of this card's module on `shapeIndex`. */
juce::DialogWindow* ModuleComponent::openShapesWindow(int shapeIndex) {
    return morphCard_ != nullptr ? morphCard_->openEditor(shapeIndex) : nullptr;
}
