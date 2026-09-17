// Concern: FRO146 -- MixerMeterReadout's paint, click-to-reset and accessibility only.
#include "MixerMeterReadout.h"

#include "MixerMeterScale.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {

// FRO146: a read-only value interface reporting the readout's own text -- isReadOnly() true means
// VoiceOver announces it but offers no adjust gesture (there is nothing to set; the readout only
// ever reflects the engine's own peaks, same convention as MixerMeter's own accessibility
// handler).
class ReadoutValueInterface : public juce::AccessibilityValueInterface {
public:
    explicit ReadoutValueInterface(const MixerMeterReadout& readout)
        : readout_(readout) {}

    bool isReadOnly() const override { return true; }
    double getCurrentValue() const override { return 0.0; }
    juce::String getCurrentValueAsString() const override { return readout_.getDisplayTextForTest() + " dBFS"; }
    void setValue(double) override {}
    void setValueAsString(const juce::String&) override {}
    AccessibleValueRange getRange() const override { return {{0.0, 1.0}, 0.1}; }

private:
    const MixerMeterReadout& readout_;
};

} // namespace

MixerMeterReadout::MixerMeterReadout() { setWantsKeyboardFocus(false); }

void MixerMeterReadout::updatePeak(float peakDb) {
    if (peakDb <= kMeterMinDb) // the floor -- nothing has rendered this tick, leave the max alone
        return;
    if (peakDb > maxPeakDb_)
        maxPeakDb_ = peakDb;
    if (maxPeakDb_ > 0.0f)
        clipped_ = true; // sticky: only reset() below ever clears it
    refreshDisplayText();
}

void MixerMeterReadout::reset() {
    maxPeakDb_ = -std::numeric_limits<float>::infinity();
    clipped_ = false;
    refreshDisplayText();
}

void MixerMeterReadout::refreshDisplayText() {
    const juce::String next = std::isfinite(maxPeakDb_) ? (maxPeakDb_ >= 0.0f ? "+" : "") + juce::String(maxPeakDb_, 1)
                                                        : juce::String("-inf");
    if (next == displayText_)
        return;
    displayText_ = next;
    repaint();
}

void MixerMeterReadout::mouseUp(const juce::MouseEvent& event) {
    if (event.mods.isAltDown()) {
        if (onResetAllRequested)
            onResetAllRequested();
        return;
    }
    reset();
}

void MixerMeterReadout::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto normalColour = laf != nullptr ? laf->getTheme().colors.textMuted : juce::Colour(0xff8A93A0);
    const auto clipColour = laf != nullptr ? laf->getTheme().colors.meterClip : juce::Colour(0xffFF4D4F);
    g.setColour(clipped_ ? clipColour : normalColour);
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.drawText(displayText_, getLocalBounds(), juce::Justification::centred, false);
}

std::unique_ptr<juce::AccessibilityHandler> MixerMeterReadout::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::staticText, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<ReadoutValueInterface>(*this)});
}

} // namespace synth::ui
