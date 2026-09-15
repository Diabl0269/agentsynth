// Concern: FRO11 (P9-5) -- MixerMeter's paint only (the ballistics live inline in the header).
#include "MixerMeter.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {

// FRO18: a read-only value interface reporting the meter's displayed level (0..1) as a percentage
// string -- isReadOnly() true means VoiceOver announces it but offers no adjust gesture (there is
// nothing to set: the meter only ever reflects the engine's own peak).
class MeterValueInterface : public juce::AccessibilityValueInterface {
public:
    explicit MeterValueInterface(const MixerMeter& meter)
        : meter_(meter) {}

    bool isReadOnly() const override { return true; }
    double getCurrentValue() const override { return (double)meter_.getDisplayedLevelForTest(); }
    juce::String getCurrentValueAsString() const override {
        return juce::String((int)std::round(meter_.getDisplayedLevelForTest() * 100.0f)) + "%";
    }
    void setValue(double) override {}
    void setValueAsString(const juce::String&) override {}
    AccessibleValueRange getRange() const override { return {{0.0, 1.0}, 0.01}; }

private:
    const MixerMeter& meter_;
};

} // namespace

std::unique_ptr<juce::AccessibilityHandler> MixerMeter::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::staticText, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<MeterValueInterface>(*this)});
}

void MixerMeter::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto track = laf != nullptr ? laf->getTheme().colors.border : juce::Colour(0xff2A2F38);
    const auto fill = laf != nullptr ? laf->getTheme().colors.meterFill : juce::Colour(0xff00D1FF);

    auto bounds = getLocalBounds().toFloat();
    g.setColour(track);
    g.fillRoundedRectangle(bounds, 2.0f);

    if (displayedLevel_ <= 0.0f)
        return;

    auto filled = bounds;
    filled = filled.removeFromBottom(bounds.getHeight() * displayedLevel_);
    juce::ColourGradient gradient(fill.brighter(0.3f), filled.getX(), filled.getY(), fill, filled.getX(),
                                  filled.getBottom(), false);
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(filled, 2.0f);
}

} // namespace synth::ui
