// Concern: the Inputs table's level meter cell.
#include "UI/Settings/AudioSettingsTab/AudioInputLevelCell.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Mixer/MixerMeterScale.h"
#include <algorithm>

namespace {
class LevelValue : public juce::AccessibilityTextValueInterface {
public:
    explicit LevelValue(const AudioInputLevelCell& cell)
        : cell_(cell) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return cell_.describeLevel(); }
    void setValueAsString(const juce::String&) override {}

private:
    const AudioInputLevelCell& cell_;
};
} // namespace

AudioInputLevelCell::AudioInputLevelCell(const juce::String& inputName) {
    setTitle(inputName + " level");
    setTooltip(inputName + " level: the loudest signal on this input");
    setWantsKeyboardFocus(true);
    addAndMakeVisible(meter_);
}

void AudioInputLevelCell::setChannelCount(int count) { meter_.setChannelCount(count); }

void AudioInputLevelCell::setLevels(float left, float right) {
    left_ = left;
    right_ = right;
    meter_.setLevels(left, right);
}

juce::String AudioInputLevelCell::describeLevel() const {
    const float peak = meter_.getChannelCount() > 1 ? std::max(left_, right_) : left_;
    const float db = synth::ui::meterLinearToDb(peak);
    return db <= synth::ui::kMeterMinDb ? juce::String("silent") : juce::String(juce::roundToInt(db)) + " dB";
}

void AudioInputLevelCell::resized() { meter_.setBounds(getLocalBounds().reduced(2, 5)); }

void AudioInputLevelCell::paintOverChildren(juce::Graphics& g) {
    synth::ui::paintFocusRing(g, getLocalBounds().toFloat().reduced(1.0f), *this, 2.0f);
}

std::unique_ptr<juce::AccessibilityHandler> AudioInputLevelCell::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::progressBar, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<LevelValue>(*this)});
}
