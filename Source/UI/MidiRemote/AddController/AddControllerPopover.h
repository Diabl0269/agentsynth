#pragma once

#include "MidiRemote/ControllerProtocol.h"
#include "MidiRemote/ControllerTemplates.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// AddControllerPopover.h (docs/control/midi-remote-ui.md#add-controller): the "+ Add
// controller" popover -- Type (Generic, learned, or Mackie Control), MIDI input device, Name (prefilled from the
// device), Start with: Detect controls now (default) / a template / Empty; a Mackie Control asks for its MIDI output
// instead. Pure view: the device and template lists are handed in (so it is deterministic in tests, no
// juce::MidiInput), and OK reports a Choice; creating the profile, opening the device and entering Detect is
// MidiRemotePanelComponent's job.
namespace synth::ui {

class AddControllerPopover : public juce::Component {
public:
    struct DeviceRow {
        juce::String identifier;
        juce::String name;
        juce::String existingProfileName; // non-empty: this device already has a profile -> greyed out
    };

    enum class StartWith { detect, templateLayout, empty };

    struct Choice {
        juce::String deviceIdentifier;
        juce::String deviceName;
        juce::String profileName;
        StartWith startWith = StartWith::detect;
        juce::String templateId; // set only for StartWith::templateLayout
        synth::ControllerProtocol protocol = synth::ControllerProtocol::generic;
        juce::String outputIdentifier; // Mackie Control only: the port its motors and LEDs listen on
        juce::String outputName;
    };

    /** `outputs` (existingProfileName unused) are the MIDI outputs a Mackie Control can be given. */
    AddControllerPopover(std::vector<DeviceRow> devices, std::vector<synth::midi::TemplateInfo> templates,
                         std::vector<DeviceRow> outputs = {});
    ~AddControllerPopover() override;

    std::function<void(const Choice&)> onConfirmed;
    std::function<void()> onCancelled;

    /** Whether OK is allowed: a free (profile-less) device or "No device (virtual controller)" is
     *  selected and the name is non-empty. A virtual choice has an empty deviceIdentifier and never Detect. */
    bool canConfirm() const;
    /** Why OK is disabled, in words; empty while it is enabled. */
    juce::String getDisabledReason() const;
    Choice getChoice() const;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int kWidth = 340;
    static constexpr int kHeight = 274;

private:
    void selectDevice(int comboId);
    void confirm();
    /** "Start with" changed -- preselects a hinted-port device for a template that declares
     *  one (TemplateInfo::handshakePort), then re-derives whether OK is allowed. */
    void handleStartChanged();
    void handleTypeChanged();
    bool isMackie() const;
    void refreshOk();

    std::vector<DeviceRow> devices_;
    std::vector<synth::midi::TemplateInfo> templates_;
    std::vector<DeviceRow> outputs_;
    bool nameEditedByUser_ = false;

    juce::Label typeLabel_{{}, "Type"};
    juce::ComboBox typeCombo_;
    juce::Label deviceLabel_{{}, "MIDI input device"};
    juce::ComboBox deviceCombo_;
    juce::Label nameLabel_{{}, "Name"};
    juce::TextEditor nameEditor_;
    juce::Label startLabel_{{}, "Start with"};
    juce::ComboBox startCombo_;
    juce::Label outputLabel_{{}, "MIDI output device"};
    juce::ComboBox outputCombo_; // in Start with's place for a Mackie Control
    juce::Label reasonLabel_;    // why OK is disabled
    juce::TextButton okButton_{"OK"};
    juce::TextButton cancelButton_{"Cancel"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AddControllerPopover)
};

} // namespace synth::ui
