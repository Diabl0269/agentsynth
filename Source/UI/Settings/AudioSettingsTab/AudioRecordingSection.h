#pragma once

#include "UI/Settings/AudioSettingsTab/AudioSettingsSection.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"

// The Recording section (read-only): the sample format the take files are written in and the latency the app removes
// from every take by itself. Header summary: "WAV 32-bit float . latency fixed automatically (-4.1 ms)".
class AudioRecordingSection : public AudioSettingsSection {
public:
    explicit AudioRecordingSection(AudioSettingsSource& source);

    int preferredHeight(int width) const override;
    juce::String summary() const override;
    void refresh() override;
    void resized() override;

    juce::String getLatencyTextForTest() const { return latency_.getText(); }
    juce::String getFormatTextForTest() const { return format_.getText(); }

private:
    juce::String latencyText() const;

    AudioSettingsSource& source_;
    juce::Label formatLabel_{{}, "Take files"}, latencyLabel_{{}, "Latency"};
    juce::Label format_, latency_;
    juce::Label caption_;
};
