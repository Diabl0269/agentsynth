#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// The Settings "Audio" tab: JUCE's stock device selector, plus (FRO136,
// docs/control/midi-remote-ui.md#settings) one caption under it naming the MIDI controllers that have
// a MIDI Remote profile. The stock selector's MIDI-input checklist can't be decorated per device, and
// its ticks mean "feeds the patch" -- a profiled controller is opened for MIDI Remote regardless -- so
// the caption is what lets the two lists explain each other.
class AudioSettingsTab : public juce::Component {
public:
    /** `midiRemoteDeviceNames`: the input-device names of the profiled controllers on this machine
     *  (duplicates and empties are dropped). Empty: no caption. */
    AudioSettingsTab(juce::AudioDeviceManager& deviceManager, const std::vector<juce::String>& midiRemoteDeviceNames);

    void resized() override;
    void lookAndFeelChanged() override;

    juce::AudioDeviceSelectorComponent& getDeviceSelector() { return *deviceSelector_; }
    /** Empty when there is no caption. */
    juce::String getCaptionTextForTest() const { return caption_.isVisible() ? caption_.getText() : juce::String(); }

private:
    static constexpr int kCaptionHeight = 44;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector_;
    juce::Label caption_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsTab)
};
