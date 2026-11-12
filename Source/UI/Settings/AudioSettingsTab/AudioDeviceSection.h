#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Settings/AudioSettingsTab/AudioAdvancedToggle.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSection.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

// The Device section: the output device (with a Test button) and, where inputs and outputs are separate, the input
// device, all from the juce::AudioDeviceManager. A collapsed Advanced row holds the device type (where there is more
// than one), the sample rate, the buffer size and the interface's control panel. A Bluetooth output gets a one-line
// note, and a Bluetooth input a warning. The header summary reads "48000 Hz . 256 samples (5.3 ms) . CoreAudio".
class AudioDeviceSection : public AudioSettingsSection {
public:
    AudioDeviceSection(juce::AudioDeviceManager& deviceManager, AudioSettingsSource& source);

    int preferredHeight(int width) const override;
    juce::String summary() const override;
    void refresh() override;
    void resized() override;
    void lookAndFeelChanged() override;

    /** Opens or closes the Advanced rows (they fade and the section's height follows); false lands at once. */
    void setAdvancedOpen(bool open, bool animate = true);
    bool isAdvancedOpen() const noexcept { return advancedToggle_.isOpen(); }
    /** Called after the user opens or closes Advanced, so the owner can remember it. */
    std::function<void(bool open)> onAdvancedToggled;
    /** Called whenever preferredHeight changes without a refresh (the Advanced fade); the owner re-lays out. */
    std::function<void()> onLayoutChanged;
    /** Whether the named device is a Bluetooth one; a test replaces the CoreAudio lookup. */
    std::function<bool(const juce::String& deviceName)> isBluetooth;

    /** Called with the picked device's name and JUCE's error when it will not open; unset, an alert shows them. */
    std::function<void(const juce::String& deviceName, const juce::String& error)> onOpenFailed;

    juce::ComboBox& getOutputBoxForTest() { return output_; }
    juce::ComboBox& getSampleRateBoxForTest() { return rate_; }
    juce::TextButton& getTestButtonForTest() { return test_; }
    juce::ComboBox& getInputBoxForTest() { return input_; }
    juce::ComboBox& getBufferBoxForTest() { return buffer_; }
    juce::ComboBox& getTypeBoxForTest() { return type_; }
    AudioAdvancedToggle& getAdvancedToggleForTest() { return advancedToggle_; }
    juce::Label& getBluetoothNoteForTest() { return bluetoothNote_; }
    juce::Label& getMicWarningForTest() { return micWarning_; }
    juce::Label& getAdvancedHintForTest() { return advancedHint_; }
    float advancedProgressForTest() const { return advancedFade_.progress(); }

private:
    void populate();
    void applyDeviceChoice(bool isInput);
    juce::String openPicked(juce::AudioDeviceManager::AudioDeviceSetup setup,
                            const synth::AudioInputLayoutStore::DeviceSettings& remembered, bool isInput);
    void updateNotes(const juce::AudioDeviceManager::AudioDeviceSetup& setup, bool separate);
    int noteHeight(const juce::Label& note, int width) const;
    int advancedRows() const;

    juce::AudioDeviceManager& deviceManager_;
    AudioSettingsSource& source_;
    juce::Label typeLabel_{{}, "Audio device type"}, outputLabel_, inputLabel_{{}, "Input"},
        rateLabel_{{}, "Sample rate"}, bufferLabel_{{}, "Buffer size"};
    juce::ComboBox type_, output_, input_, rate_, buffer_;
    juce::TextButton test_{"Test"};
    juce::TextButton controlPanel_{"Control panel"};
    juce::Label bluetoothNote_, micWarning_;
    juce::Label advancedHint_; // under Advanced while no device is open, so the open row is never empty
    AudioAdvancedToggle advancedToggle_;
    juce::Component advanced_; // holds the device type, sample rate, buffer size and control panel
    synth::ui::FadeVisibility advancedFade_{&advanced_};
    bool populating_ = false;
};
