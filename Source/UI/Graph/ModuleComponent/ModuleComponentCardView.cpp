// ModuleComponentCardView.cpp -- the optional panels a card can open (Show Scope, Show Response, Show
// Spectrum): what a click on each toggle does, remembering the choice on the module, and reopening the
// panels when a card is built. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units.
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

using namespace detail;

// The panel choices live on the module, not the card: a card is rebuilt on every graph reconcile, and
// the module is what the project saves (as the node's "cardView" key, see CardViewState).
void ModuleComponent::rememberCardView(const std::function<void(synth::CardViewState&)>& edit) {
    if (auto* modBase = dynamic_cast<ModuleBase*>(module)) {
        auto state = modBase->getCardViewState();
        edit(state);
        modBase->setCardViewState(state);
    }
}

// The body of a click on "Show Scope", and of restoring it: one path, so a restored scope opens exactly as
// a clicked one does.
void ModuleComponent::setScopeShown(bool show) {
    scopeComponent->setVisible(show);
    rememberCardView([show](synth::CardViewState& state) { state.showScope = show; });
    updateLayout();
    owner.handleModuleResized(this); // the card's height changed: neighbours make room (a restore is ignored)
}

// Hiding the response view also hides and clears its spectrum backdrop, which is back to the card default.
void ModuleComponent::setResponseShown(bool show) {
    freqResponseComponent->setVisible(show);
    if (spectrumToggle != nullptr) {
        spectrumToggle->setVisible(show);
        if (!show) {
            spectrumToggle->setToggleState(false, juce::dontSendNotification);
            freqResponseComponent->setShowSpectrum(false);
        }
    }
    rememberCardView([show](synth::CardViewState& state) {
        state.showResponse = show;
        if (!show)
            state.showSpectrum.reset();
    });
    updateLayout();
    owner.handleModuleResized(this);
}

void ModuleComponent::setSpectrumShown(bool show) {
    if (freqResponseComponent != nullptr)
        freqResponseComponent->setShowSpectrum(show);
    else if (eqCurveComponent != nullptr)
        eqCurveComponent->setShowSpectrum(show);
    rememberCardView([show](synth::CardViewState& state) { state.showSpectrum = show; });
}

// Reopens whatever the module remembers, through the same setters the toggles use. Called once at the end of
// the constructor, after every panel exists and has had its first layout. Absent means the defaults the
// constructor already set up (everything closed; the EQ's spectrum on), so a project that never saved a view
// changes nothing.
void ModuleComponent::restoreCardView() {
    auto* modBase = dynamic_cast<ModuleBase*>(module);
    if (modBase == nullptr)
        return;
    const auto state = modBase->getCardViewState();

    if (state.showScope && scopeToggle != nullptr) {
        scopeToggle->setToggleState(true, juce::dontSendNotification);
        setScopeShown(true);
    }
    if (state.showResponse && freqResponseToggle != nullptr) {
        freqResponseToggle->setToggleState(true, juce::dontSendNotification);
        setResponseShown(true);
    }
    if (state.showSpectrum.has_value() && spectrumToggle != nullptr) {
        const bool spectrumOn = *state.showSpectrum && (eqCurveComponent != nullptr || state.showResponse);
        spectrumToggle->setToggleState(spectrumOn, juce::dontSendNotification);
        setSpectrumShown(spectrumOn);
    }
}
