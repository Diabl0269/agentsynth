#pragma once

#include "EQCurveComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** The selected band's type as a pill in its kind colour; pressing it opens the type menu, like a right-click on the
 *  dot. It follows the curve's selection and shows nothing while no band is selected. */
class EQBandTypePill
    : public juce::Button
    , private EQCurveComponent::Listener {
public:
    explicit EQBandTypePill(EQCurveComponent& curve);
    ~EQBandTypePill() override;

    void paintButton(juce::Graphics& g, bool over, bool down) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    /** Names the live key that opens the menu, so a rebind shows at the next hover. */
    juce::String getTooltip() override;

    /** The text on the pill now ("Bell"), empty with no band selected. */
    juce::String getPillText() const { return text_; }

private:
    void eqCurveChanged() override;

    EQCurveComponent& curve_;
    juce::String text_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQBandTypePill)
};

} // namespace synth::ui
