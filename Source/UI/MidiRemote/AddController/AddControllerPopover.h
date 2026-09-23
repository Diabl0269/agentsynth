#pragma once

#include "MidiRemote/ControllerTemplates.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// AddControllerPopover.h -- FRO134 (docs/control/midi-remote-ui.md#add-controller): the "+ Add
// controller" popover -- MIDI input device, Name (prefilled from the device), Start with: Detect
// controls now (default) / a template / Empty. Pure view: the device and template lists are handed
// in (so it is deterministic in tests, no juce::MidiInput), and OK reports a Choice; creating the
// profile, opening the device and entering Detect is MidiRemotePanelComponent's job.
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
    };

    AddControllerPopover(std::vector<DeviceRow> devices, std::vector<synth::midi::TemplateInfo> templates);
    ~AddControllerPopover() override;

    std::function<void(const Choice&)> onConfirmed;
    std::function<void()> onCancelled;

    /** Whether OK is allowed: a free (profile-less) device is selected and the name is non-empty. */
    bool canConfirm() const;
    Choice getChoice() const;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int kWidth = 340;
    static constexpr int kHeight = 190;

private:
    void selectDevice(int comboId);
    void confirm();

    std::vector<DeviceRow> devices_;
    std::vector<synth::midi::TemplateInfo> templates_;
    bool nameEditedByUser_ = false;

    juce::Label deviceLabel_{{}, "MIDI input device"};
    juce::ComboBox deviceCombo_;
    juce::Label nameLabel_{{}, "Name"};
    juce::TextEditor nameEditor_;
    juce::Label startLabel_{{}, "Start with"};
    juce::ComboBox startCombo_;
    juce::TextButton okButton_{"OK"};
    juce::TextButton cancelButton_{"Cancel"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AddControllerPopover)
};

} // namespace synth::ui
