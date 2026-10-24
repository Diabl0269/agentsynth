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

private:
    void setChannel(int channel, bool on);

    juce::AudioDeviceManager& deviceManager_;
    std::vector<std::unique_ptr<juce::ToggleButton>> ticks_;
    juce::Label none_;
};
