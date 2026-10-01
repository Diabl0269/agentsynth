// CardBodyViews.cpp -- the view registry a card body builds its view items from. Each entry wraps an
// existing component from Source/UI/ModuleViews unchanged. The Threshold view is the one the automatic
// layout places (between the toggles and the knobs); the Envelope view is the ADSR's curve editor, which
// the card wires to the parameters (ModuleComponent's createEnvelopeCardControls) and whose card toggle
// opens and closes it. The scope, response, spectrum and LFO curve views are still built by
// ModuleComponent's own chrome and join this registry when a layout first places them.
#include "CardBodyViews.h"
#include "Modules/ADSRModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/ThresholdMeterSource.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
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

// The ADSR's breakpoint editor: the same component, name and description the card's chrome built before.
std::unique_ptr<juce::Component> createEnvelope(juce::AudioProcessor& module) {
    if (dynamic_cast<ADSRModule*>(&module) == nullptr)
        return nullptr;
    auto editor = std::make_unique<synth::ui::CurveEditorComponent>();
    editor->setTitle("Envelope curve");
    editor->setDescription("Attack, hold, decay and release shape of the envelope");
    return editor;
}

constexpr int kEnvelopeViewHeight = 150;

int envelopeHeight(juce::AudioProcessor&) { return kEnvelopeViewHeight; }

juce::String noOwnedParam(juce::AudioProcessor&) { return {}; }

const CardViewFactory kEnvelope{createEnvelope, envelopeHeight, noOwnedParam};

} // namespace

const CardViewFactory* findCardViewFactory(CardView view) {
    switch (view) {
    case CardView::Threshold:
        return &kThreshold;
    case CardView::Envelope:
        return &kEnvelope;
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
    case CardView::Envelope:
        return dynamic_cast<ADSRModule*>(&module) != nullptr;
    default:
        return false;
    }
}

} // namespace synth
