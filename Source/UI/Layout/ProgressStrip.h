#pragma once

// A thin themed progress bar that only shows a fraction. Not a Tab stop; its name and value are read by screen readers.

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class ProgressStrip : public juce::Component {
public:
    ProgressStrip();

    /** The filled share, clamped to 0..1. */
    void setFraction(float fraction);
    float getFraction() const noexcept { return fraction_; }

    /** What the bar measures ("Downloading Core instruments"); the screen reader adds the percentage. */
    void setSubject(const juce::String& subject);

    void paint(juce::Graphics& g) override;

private:
    void updateAccessibleName();

    float fraction_ = 0.0f;
    juce::String subject_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProgressStrip)
};

} // namespace synth::ui
