#pragma once

#include "UI/Settings/AudioSettingsTab/AudioSettingsSection.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"
#include <memory>
#include <vector>

// The MIDI section: one tick per MIDI input (ticked = it plays the patch), the MIDI output choice, and the caption
// naming the controllers that have a MIDI Remote profile and are opened whether or not they are ticked.
class AudioMidiSection : public AudioSettingsSection {
public:
    AudioMidiSection(AudioSettingsSource& source, const juce::StringArray& profiledControllers);

    int preferredHeight(int width) const override;
    juce::String summary() const override;
    void refresh() override;
    void resized() override;
    void lookAndFeelChanged() override;

    /** Empty when there is no caption. */
    juce::String getCaptionText() const { return caption_.isVisible() ? caption_.getText() : juce::String(); }
    int getNumInputTicksForTest() const { return (int)ticks_.size(); }

private:
    int captionHeight(int width) const;

    AudioSettingsSource& source_;
    std::vector<std::unique_ptr<juce::ToggleButton>> ticks_;
    std::vector<AudioMidiDevice> outputs_;
    juce::Label inputsNone_;
    juce::Label outputLabel_{{}, "MIDI output"};
    juce::ComboBox output_;
    juce::Label caption_;
};
