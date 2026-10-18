// SamplerChromeMap.cpp -- the Sampler card's instrument map: opening its window out of the card, and the undo step
// around each edit made in it.
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapWindow.h"

namespace synth::ui {

bool SamplerChrome::editRegionsUndoably(const std::vector<synth::sampler::RegionSpec>& specs) {
    return loadUndoably(
        [this, specs](synth::sampler::InstrumentHost&) { return sampler_.editInstrumentRegions(specs); });
}

void SamplerChrome::openMap() {
    if (mapWindow_ != nullptr) {
        mapWindow_->toFront(true);
        return;
    }
    if (mapView_ != nullptr)
        return;
    juce::WeakReference<SamplerChrome> weak(this);
    auto view =
        std::make_unique<SamplerMapView>(sampler_, [weak](const std::vector<synth::sampler::RegionSpec>& specs) {
            return weak != nullptr && weak->editRegionsUndoably(specs);
        });
    mapView_ = view.get();
    mapWindow_ = openSamplerMapWindow(std::move(view), host_.card);
}

// The window refers to the module, so it goes when the card does.
void SamplerChrome::closeMap() {
    if (mapView_ != nullptr)
        mapView_->detach();
    if (mapWindow_ != nullptr)
        mapWindow_->exitModalState(0);
    mapWindow_ = nullptr;
    mapView_ = nullptr;
}

} // namespace synth::ui
