#pragma once

#include "EQCurveComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** The headphones toggle in the EQ window's footer: solos the selected band, like the S key. It is lit while that band
 *  is the one being auditioned, and unavailable while no enabled band is selected. */
class EQSoloButton
    : public juce::Button
    , private EQCurveComponent::Listener {
public:
    explicit EQSoloButton(EQCurveComponent& curve);
    ~EQSoloButton() override;

    void paintButton(juce::Graphics& g, bool over, bool down) override;
    /** Names the live key ("Solo the selected band (S)"), so a rebind shows at the next hover. */
    juce::String getTooltip() override;

private:
    void eqCurveChanged() override;

    EQCurveComponent& curve_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQSoloButton)
};

} // namespace synth::ui
