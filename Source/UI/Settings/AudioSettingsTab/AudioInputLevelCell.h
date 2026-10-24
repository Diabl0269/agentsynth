#pragma once

#include "UI/Timeline/TimelineRoutingPane/InputLevelMeter.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The Level cell of the Inputs table: the routing pane's input meter (one bar, two for a stereo input) made a Tab stop
// that a screen reader reads as "<input> level" with the level in dB as its value.
class AudioInputLevelCell
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    explicit AudioInputLevelCell(const juce::String& inputName);

    void setChannelCount(int count);
    /** One tick with LINEAR peaks. */
    void setLevels(float left, float right);
    float getLevelFractionForTest(int index) const { return meter_.getLevelFraction(index); }
    int getChannelCount() const { return meter_.getChannelCount(); }
    /** "-18 dB" or "silent": what a screen reader hears as the value. */
    juce::String describeLevel() const;

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    synth::ui::InputLevelMeter meter_;
    float left_ = 0.0f, right_ = 0.0f;
};
