#pragma once

#include "UI/Settings/AudioSettingsTab/AudioSettingsSection.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

// The Device section: the interface (device type where there is more than one, the output device, the input device
// where inputs and outputs are separate), a Test button, the sample rate and the buffer size, all from the
// juce::AudioDeviceManager. The header summary reads "48000 Hz . 256 samples (5.3 ms) . CoreAudio".
class AudioDeviceSection : public AudioSettingsSection {
public:
    AudioDeviceSection(juce::AudioDeviceManager& deviceManager, AudioSettingsSource& source);

    int preferredHeight(int width) const override;
    juce::String summary() const override;
    void refresh() override;
    void resized() override;

    juce::ComboBox& getSampleRateBoxForTest() { return rate_; }
    juce::TextButton& getTestButtonForTest() { return test_; }

private:
    void populate();
    void applyDeviceChoice(bool isInput);
    int visibleRows() const;

    juce::AudioDeviceManager& deviceManager_;
    AudioSettingsSource& source_;
    juce::Label typeLabel_{{}, "Audio device type"}, outputLabel_, inputLabel_{{}, "Input"},
        rateLabel_{{}, "Sample rate"}, bufferLabel_{{}, "Buffer size"};
    juce::ComboBox type_, output_, input_, rate_, buffer_;
    juce::TextButton test_{"Test"};
    juce::TextButton controlPanel_{"Control panel"};
    bool populating_ = false;
};
