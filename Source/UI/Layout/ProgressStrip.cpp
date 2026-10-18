#include "UI/Layout/ProgressStrip.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

ProgressStrip::ProgressStrip() {
    setInterceptsMouseClicks(false, false);
    setAccessible(true);
    updateAccessibleName();
}

void ProgressStrip::setFraction(float fraction) {
    const float clamped = juce::jlimit(0.0f, 1.0f, fraction);
    if (juce::approximatelyEqual(clamped, fraction_))
        return;
    fraction_ = clamped;
    updateAccessibleName();
    repaint();
}

void ProgressStrip::setSubject(const juce::String& subject) {
    subject_ = subject;
    updateAccessibleName();
}

void ProgressStrip::updateAccessibleName() {
    setTitle(subject_ + (subject_.isEmpty() ? "" : ", ") + juce::String(juce::roundToInt(fraction_ * 100.0f)) +
             " percent");
}

void ProgressStrip::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto bounds = getLocalBounds().toFloat();
    const float radius = bounds.getHeight() * 0.5f;
    g.setColour(theme.colors.border);
    g.fillRoundedRectangle(bounds, radius);
    if (fraction_ > 0.0f) {
        g.setColour(theme.colors.accent);
        g.fillRoundedRectangle(bounds.withWidth(juce::jmax(bounds.getHeight(), bounds.getWidth() * fraction_)), radius);
    }
}

} // namespace synth::ui
