// CardBodyViews.cpp -- the view registry a card body builds its view items from. Each entry wraps an
// existing component from Source/UI/ModuleViews unchanged. Only the Threshold view is registered: it is
// the one view the automatic layout places (between the toggles and the knobs). The scope, response,
// spectrum, envelope and LFO curve views are still built by ModuleComponent's own chrome and join this
// registry when a layout first places them.
#include "CardBodyViews.h"
#include "Modules/ModuleBase.h"
#include "Modules/ThresholdMeterSource.h"
#include "UI/ModuleViews/ThresholdControlComponent.h"

namespace synth {

namespace {

// Sample & Hold keeps its rotary Threshold knob, so its view is the meter alone. ADSR and Comparator
// embed the Threshold slider in the view so the slice sits on the live level bar, and get no knob.
juce::AudioParameterFloat* thresholdSliderParam(juce::AudioProcessor& module) {
    auto* src = dynamic_cast<ThresholdMeterSource*>(&module);
    auto* mb = dynamic_cast<ModuleBase*>(&module);
    if (src == nullptr || (mb != nullptr && mb->getModuleType() == ModuleType::SampleHold))
        return nullptr;
    return dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&module, src->getThresholdParamID()));
}

std::unique_ptr<juce::Component> createThreshold(juce::AudioProcessor& module) {
    auto* src = dynamic_cast<ThresholdMeterSource*>(&module);
    if (src == nullptr)
        return nullptr;
    return std::make_unique<ThresholdControlComponent>(*src, thresholdSliderParam(module));
}

int thresholdHeight(juce::AudioProcessor& module) {
    return thresholdSliderParam(module) != nullptr ? ThresholdControlComponent::getSliderModeHeight()
                                                   : ThresholdControlComponent::getMeterOnlyHeight();
}

juce::String thresholdOwnedParam(juce::AudioProcessor& module) {
    auto* param = thresholdSliderParam(module);
    return param != nullptr ? param->paramID : juce::String();
}

const CardViewFactory kThreshold{createThreshold, thresholdHeight, thresholdOwnedParam};

} // namespace

const CardViewFactory* findCardViewFactory(CardView view) {
    switch (view) {
    case CardView::Threshold:
        return &kThreshold;
    default:
        return nullptr;
    }
}

bool cardViewAvailableFor(CardView view, juce::AudioProcessor& module) {
    if (findCardViewFactory(view) == nullptr)
        return false;
    switch (view) {
    case CardView::Threshold:
        return dynamic_cast<ThresholdMeterSource*>(&module) != nullptr;
    default:
        return false;
    }
}

} // namespace synth
