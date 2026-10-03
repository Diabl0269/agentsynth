#pragma once

#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

// The one value slot of an automation lane header: the lane's value at the playhead in the muted text
// colour, or, while the lane has a selected point, that point's value in the accent colour. The two
// are told apart by the accessible name and the tooltip as well as the colour.
// Message thread only. Draws nothing and repaints nothing unless its text or mode changed; the
// colour cross-fade is a time-bounded tween that lands at once under Reduce Motion or off screen.
class LaneValueReadout
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    LaneValueReadout();
    ~LaneValueReadout() override;

    /** The parameter's name, used in the accessible name and tooltip. */
    void setParameterName(const juce::String& name);
    /** The value at the playhead; what the slot shows when no point is selected. */
    void setPlayheadText(const juce::String& text);
    /** The selected point's value; an empty optional goes back to the playhead value. */
    void setSelectedText(const std::optional<juce::String>& text);

    /** What the slot is showing right now. */
    juce::String getDisplayedText() const { return selected_.value_or(playhead_); }
    bool isShowingSelectedPoint() const noexcept { return selected_.has_value(); }
    /** 0 = muted, 1 = accent: how far the colour has crossed. */
    float getAccentMixForTest() const noexcept { return mix_; }

    void paint(juce::Graphics& g) override;

private:
    void refreshAccessibility();
    void retargetColour();
    void applyMix(float mix);

    juce::String parameterName_;
    juce::String playhead_;
    std::optional<juce::String> selected_;
    float mix_ = 0.0f;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver driver_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaneValueReadout)
};

} // namespace synth::ui
