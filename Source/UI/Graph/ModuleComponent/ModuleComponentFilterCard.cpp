// ModuleComponentFilterCard.cpp -- builds the Filter card's Custom-type section (the morph map, the Edit shapes
// button and the Show Morph Map pill), which lives in FilterCustomCard. ModuleComponent is declared in
// ModuleComponent.h; the rest of its implementation lives in the sibling ModuleComponent*.cpp units.
#include "AudioEngine/AudioEngine.h"
#include "FilterCustomCard/FilterCustomCard.h"
#include "ModuleComponent.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

void ModuleComponent::createFilterCustomCard() {
    auto* filter = dynamic_cast<FilterModule*>(module);
    if (filter == nullptr)
        return;
    synth::ui::FilterCustomCard::Host host;
    host.card = this;
    host.relayout = [this] { updateLayout(); };
    host.makeRoom = [this] { owner.handleModuleResized(this); };
    host.settling = &settlingFootprint_;
    host.showMenu = [this](juce::PopupMenu& menu) { showContextMenuHook_(menu); };
    host.rememberView = [this](const std::function<void(synth::CardViewState&)>& edit) { rememberCardView(edit); };
    host.hideMorphKnobs = [this](bool hidden) {
        if (cardBody_ != nullptr)
            cardBody_->setSectionHidden("custom", hidden);
    };
    host.title = [this] { return cardTitle(); };
    auto adapter = std::make_shared<synth::ui::FilterShapesAdapter>(
        *filter, undoManager, owner.getAudioEngine().getGraph(), nodeId, cardTitle());
    filterCustom_ = std::make_unique<synth::ui::FilterCustomCard>(std::move(host), *filter, std::move(adapter));
    freqResponseComponent->onCustomDoubleClick = [this] {
        if (filterCustom_ != nullptr)
            filterCustom_->openEditor(filterCustom_->nearestShapeToCursor());
    };
}
