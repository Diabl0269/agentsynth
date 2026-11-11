// EQCurveTypeMenu.cpp -- how EQCurveComponent changes a band's type and slope, and opens the menu that asks for them.
// The curve is declared in EQCurveComponent.h; the menu content is EQBandTypeMenu.

#include "EQBandTypeMenu.h"
#include "EQCurveComponent.h"
#include "UI/Layout/PopupHost.h"

void EQCurveComponent::setBandTypeAndSlope(int band, synth::eq::BandType type, int slopeDbPerOct) {
    if (band < 0 || band >= ParametricEQModule::kMaxBands)
        return;
    const bool typeChanges = eqModule.getBandType(band) != type;
    const bool slopeChanges = slopeDbPerOct != 0 && eqModule.getBandSlope(band) != slopeDbPerOct;
    if (!typeChanges && !slopeChanges)
        return; // nothing to change: no undo step
    beginGesture();
    if (typeChanges)
        eqModule.setBandType(band, type);
    if (slopeChanges)
        eqModule.setBandSlope(band, slopeDbPerOct);
    endGesture();
    refreshFromModule();
}

void EQCurveComponent::showTypeMenu(juce::Rectangle<int> screenArea) {
    if (selectedBand < 0)
        return;
    const int band = selectedBand;
    auto menu = std::make_unique<synth::ui::EQBandTypeMenu>(eqModule.getBandType(band), eqModule.getBandSlope(band));
    juce::Component::SafePointer<EQCurveComponent> self(this);
    menu->onChoose = [self, band](synth::eq::BandType type, int slope) {
        if (self != nullptr)
            self->setBandTypeAndSlope(band, type, slope);
    };
    synth::ui::launchPopover(std::move(menu), screenArea, this);
}

void EQCurveComponent::showTypeMenuAtDot(int band) {
    if (band < 0 || band >= ParametricEQModule::kMaxBands)
        return;
    setSelectedBand(band);
    const auto centre = getDrawnBandPosition(band).roundToInt();
    const auto dot = juce::Rectangle<int>(2 * (int)kHandleRadius, 2 * (int)kHandleRadius).withCentre(centre);
    showTypeMenu(localAreaToGlobal(dot));
}
