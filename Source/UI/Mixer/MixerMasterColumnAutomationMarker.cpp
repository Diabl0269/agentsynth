// MixerMasterColumnAutomationMarker.cpp
//
// The automation-lane marker on the master fader -- the same rules as MixerColumnAutomationMarker.cpp, for the one
// control the master column registers.

#include "MixerMasterColumn.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

void MixerMasterColumn::refreshAutomatedMarkers() {
    if (graphEditor_ == nullptr || !graphEditor_->onQueryAutomatedParamsForNode)
        return;
    const bool on = midiLearnableFaderParam_ != nullptr &&
                    graphEditor_->onQueryAutomatedParamsForNode(nodeId_).count(midiLearnableFaderParam_->paramID) > 0;
    const juce::String name =
        midiLearnableFaderParam_ != nullptr ? midiLearnableFaderParam_->getName(100) : juce::String();
    if (!faderAutomated_.update(fader_.getSlider(), on, name, synth::ui::AutomatedMarkerFade::nowMs(),
                                synth::ui::prefersReducedMotion()))
        return;
    if (automatedTicker_ == nullptr)
        automatedTicker_ = std::make_unique<synth::ui::AutomatedMarkerTicker>(*this);
    juce::Component::SafePointer<MixerMasterColumn> safeThis(this);
    const auto repaintMarker = [safeThis] {
        if (safeThis == nullptr)
            return;
        auto& slider = safeThis->fader_.getSlider();
        safeThis->repaint(
            synth::ui::automatedMarkerRect(safeThis->getLocalArea(&slider, slider.getLocalBounds())).expanded(2));
    };
    repaintMarker();
    automatedTicker_->run(repaintMarker);
}

void MixerMasterColumn::paintAutomatedMarkers(juce::Graphics& g) {
    const float level = faderAutomated_.fade.level(synth::ui::AutomatedMarkerFade::nowMs());
    if (level <= 0.0f)
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour textPrimary = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colour(0xffE8E8F0);
    // The slider sits two levels down (column -> fader_ -> slider), so walk up with getLocalArea.
    synth::ui::paintAutomatedMarker(g, getLocalArea(&fader_.getSlider(), fader_.getSlider().getLocalBounds()),
                                    synth::ui::automatedMarkerColour(textPrimary, level));
}

} // namespace synth::ui
