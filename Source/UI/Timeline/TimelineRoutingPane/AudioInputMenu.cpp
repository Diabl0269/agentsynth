// Concern: the audio track Input row's words and menu (pure; see AudioInputMenu.h).
#include "UI/Timeline/TimelineRoutingPane/AudioInputMenu.h"

namespace synth::ui {

namespace {
using Ids = AudioInputMenuIds;
using Kind = synth::AudioInputChoice::Kind;

juce::String dot() { return juce::String::fromUTF8(" \xC2\xB7 "); }

juce::String inputName(int first, int count) {
    return count > 1 ? "In " + juce::String(first + 1) + " + " + juce::String(first + 2)
                     : "In " + juce::String(first + 1);
}

// The inputs the menu lists: the device's own (capped at the model's range), or what the host passes in.
int listedInputs(const synth::AudioInputStatus& status) {
    const int count = status.hosted ? status.activeInputs : status.deviceInputs;
    return juce::jlimit(0, synth::AudioInputChoice::kMaxChannel, count);
}

juce::String deviceLabel(const synth::AudioInputStatus& status) {
    if (status.hosted)
        return "Host input";
    return status.deviceName.isNotEmpty() ? status.deviceName : juce::String("Audio input");
}

bool pickedOnOtherDevice(const synth::AudioInputChoice& choice, const synth::AudioInputStatus& status) {
    return !status.hosted && choice.deviceName.isNotEmpty() && status.deviceName.isNotEmpty() &&
           choice.deviceName != status.deviceName;
}

bool isTicked(const synth::AudioInputChoice& choice, const synth::AudioInputStatus& status, int first, int count) {
    return choice.kind == Kind::Device && choice.firstChannel == first && choice.numChannels == count &&
           synth::isAudioInputConnected(choice, status);
}

bool pairActive(const synth::AudioInputStatus& status, int first) {
    return status.isChannelActive(first) && status.isChannelActive(first + 1);
}

void addDeviceInputs(juce::PopupMenu& menu, const synth::AudioInputChoice& choice,
                     const synth::AudioInputStatus& status) {
    const int inputs = listedInputs(status);
    if (inputs == 0) {
        menu.addItem(-1, status.hosted ? "The host passes no inputs to this plugin" : "No audio input device found",
                     false, false);
        return;
    }
    if (!status.hosted && status.activeInputs == 0) {
        menu.addItem(-1, "The inputs of " + deviceLabel(status) + " are turned off.", false, false);
        menu.addItem(Ids::kTurnOnInputs, "Turn on its inputs");
        return;
    }
    menu.addSectionHeader("Mono" + dot() + deviceLabel(status));
    bool anyOff = false;
    for (int channel = 0; channel < inputs; ++channel) {
        anyOff = anyOff || !status.isChannelActive(channel);
        menu.addItem(Ids::kFirstMono + channel, inputName(channel, 1), status.isChannelActive(channel),
                     isTicked(choice, status, channel, 1));
    }
    if (inputs >= 2) {
        menu.addSectionHeader("Stereo" + dot() + deviceLabel(status));
        for (int first = 0; first + 1 < inputs; first += 2)
            menu.addItem(Ids::kFirstStereo + first, inputName(first, 2), pairActive(status, first),
                         isTicked(choice, status, first, 2));
    }
    if (anyOff && !status.hosted)
        menu.addItem(Ids::kTurnOnInputs, "Turn on all its inputs");
}
} // namespace

AudioInputRowText describeAudioInput(const synth::AudioInputChoice& choice, const synth::AudioInputStatus& status) {
    AudioInputRowText row;
    if (choice.kind == Kind::None) {
        row.text = "No input";
        row.muted = true;
        return row;
    }
    if (choice.kind == Kind::WholeMix) {
        row.text = "Whole mix";
        return row;
    }
    const auto name = inputName(choice.firstChannel, choice.numChannels);
    if (pickedOnOtherDevice(choice, status)) {
        row.text = name + " (" + choice.deviceName + " not connected)";
        row.warning = true;
        row.note = "This track records and plays nothing until " + choice.deviceName + " is connected.";
        return row;
    }
    if (!synth::isAudioInputConnected(choice, status)) {
        row.text = name + " (turned off)";
        row.warning = true;
        row.note = status.hosted ? juce::String("The host does not pass this input to the plugin.")
                                 : juce::String("This input is turned off. Turn it on from this menu or in Audio "
                                                "settings.");
        return row;
    }
    row.text = choice.numChannels > 1 ? name : name + " (mono)";
    return row;
}

juce::PopupMenu buildAudioInputMenu(const synth::AudioInputChoice& choice, const synth::AudioInputStatus& status) {
    juce::PopupMenu menu;
    menu.addItem(Ids::kNoInput, "No input", true, choice.kind == Kind::None);
    menu.addSeparator();
    addDeviceInputs(menu, choice, status);
    // The track's own choice stays on the menu, ticked, while it cannot be picked from the list above.
    if (choice.kind == Kind::Device && !synth::isAudioInputConnected(choice, status))
        menu.addItem(Ids::kNotConnected, describeAudioInput(choice, status).text, true, true);
    menu.addSeparator();
    menu.addItem(Ids::kWholeMix, "Whole mix", true, choice.kind == Kind::WholeMix);
    if (!status.hosted) {
        menu.addSeparator();
        menu.addItem(Ids::kAudioSettings, "Audio settings...");
    }
    return menu;
}

AudioInputMenuAction resolveAudioInputMenuChoice(int menuId, const synth::AudioInputStatus& status) {
    AudioInputMenuAction action;
    const int inputs = listedInputs(status);
    auto pick = [&](int first, int count) {
        action.kind = AudioInputMenuAction::Kind::Pick;
        action.choice.kind = Kind::Device;
        action.choice.firstChannel = first;
        action.choice.numChannels = count;
        action.choice.deviceName = status.hosted ? juce::String() : status.deviceName;
    };
    if (menuId == Ids::kNoInput || menuId == Ids::kWholeMix) {
        action.kind = AudioInputMenuAction::Kind::Pick;
        action.choice.kind = menuId == Ids::kNoInput ? Kind::None : Kind::WholeMix;
    } else if (menuId == Ids::kTurnOnInputs && !status.hosted) {
        action.kind = AudioInputMenuAction::Kind::TurnOnInputs;
    } else if (menuId == Ids::kAudioSettings && !status.hosted) {
        action.kind = AudioInputMenuAction::Kind::OpenAudioSettings;
    } else if (menuId >= Ids::kFirstMono && menuId < Ids::kFirstMono + inputs) {
        pick(menuId - Ids::kFirstMono, 1);
    } else if (menuId >= Ids::kFirstStereo && menuId + 1 < Ids::kFirstStereo + inputs &&
               (menuId - Ids::kFirstStereo) % 2 == 0) {
        pick(menuId - Ids::kFirstStereo, 2);
    }
    return action;
}

} // namespace synth::ui
