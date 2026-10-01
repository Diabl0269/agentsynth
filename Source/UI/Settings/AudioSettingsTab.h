#pragma once

#include "UI/Layout/ListBoxFocusRing.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// The Settings "Audio" tab: JUCE's stock device selector, plus (
// docs/control/midi-remote-ui.md#settings) one caption under it naming the MIDI controllers that have
// a MIDI Remote profile. The stock selector's MIDI-input checklist can't be decorated per device, and
// its ticks mean "feeds the patch" -- a profiled controller is opened for MIDI Remote regardless -- so
// the caption is what lets the two lists explain each other.
//
// The stock selector names none of its controls for a screen reader and gives its drop-downs no
// tooltip: this tab reads each control's attached caption label ("Output:", "Sample rate:", ...) and
// applies it as the control's title and tooltip, again whenever the device manager changes (the
// selector rebuilds its controls then).
class AudioSettingsTab
    : public juce::Component
    , private juce::ChangeListener {
public:
    /** `midiRemoteDeviceNames`: the input-device names of the profiled controllers on this machine
     *  (duplicates and empties are dropped). Empty: no caption. */
    AudioSettingsTab(juce::AudioDeviceManager& deviceManager, const std::vector<juce::String>& midiRemoteDeviceNames);
    ~AudioSettingsTab() override;

    void resized() override;
    void lookAndFeelChanged() override;

    juce::AudioDeviceSelectorComponent& getDeviceSelector() { return *deviceSelector_; }
    synth::ui::ListBoxFocusRing& getListFocusRingForTest() { return *listFocusRing_; }
    /** Empty when there is no caption. */
    juce::String getCaptionTextForTest() const { return caption_.isVisible() ? caption_.getText() : juce::String(); }

private:
    static constexpr int kCaptionHeight = 44;

    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        nameSelectorControls();
        listFocusRing_->rescan();
    }
    void nameSelectorControls();

    juce::AudioDeviceManager& deviceManager_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector_;
    // After the selector, so it is destroyed first; the stock channel and MIDI input lists paint no
    // selection, so it draws the keyboard focus ring on their selected row.
    std::unique_ptr<synth::ui::ListBoxFocusRing> listFocusRing_;
    juce::Label caption_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsTab)
};
