#pragma once

#include "UI/Settings/AudioSettingsTab/AudioSettingsSection.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <vector>

// The Outputs section: which of the interface's output channels the app plays on, one tick per channel (at least
// one stays on).
class AudioOutputsSection : public AudioSettingsSection {
public:
    explicit AudioOutputsSection(juce::AudioDeviceManager& deviceManager);

    int preferredHeight(int width) const override;
    juce::String summary() const override;
    void refresh() override;
    void resized() override;

    int getNumChannelsForTest() const { return (int)ticks_.size(); }
    // Lays out the section for the given channels without a device, so a test can use 0, 1 and 2 outputs.
    void setChannelsForTest(const juce::StringArray& names, const juce::BigInteger& active) { rebuild(names, active); }
    juce::Rectangle<int> getTickBoundsForTest(int index) const { return ticks_[(size_t)index]->getBounds(); }
    juce::Rectangle<int> getNoneBoundsForTest() const { return none_.getBounds(); }
    bool isNoneVisibleForTest() const { return none_.isVisible(); }

private:
    void setChannel(int channel, bool on);
    void rebuild(const juce::StringArray& names, const juce::BigInteger& active);

    juce::AudioDeviceManager& deviceManager_;
    std::vector<std::unique_ptr<juce::ToggleButton>> ticks_;
    juce::Label none_;
};
