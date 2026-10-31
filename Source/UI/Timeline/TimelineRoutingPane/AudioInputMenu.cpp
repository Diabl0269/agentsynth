// Concern: the audio track Input row's words and menu (pure; see AudioInputMenu.h).
#include "UI/Timeline/TimelineRoutingPane/AudioInputMenu.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"

namespace synth::ui {

namespace {
using Ids = AudioInputMenuIds;
using Kind = synth::AudioInputChoice::Kind;

juce::String dot() { return juce::String::fromUTF8(" \xC2\xB7 "); }

juce::String inputName(int first, int count) {
    return count > 1 ? "In " + juce::String(first + 1) + " + " + juce::String(first + 2)
                     : "In " + juce::String(first + 1);
}

// "Vocal mic (In 1)" for a named input, plain "In 1" otherwise.
juce::String namedInputLabel(const juce::String& name, int first, int count) {
    return name.isNotEmpty() ? name + " (" + inputName(first, count) + ")" : inputName(first, count);
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
    return !status.hosted && choice.inputName.isEmpty() && choice.deviceName.isNotEmpty() &&
           status.deviceName.isNotEmpty() && choice.deviceName != status.deviceName;
}

bool isTicked(const synth::AudioInputChoice& choice, const synth::AudioInputStatus& status, int first, int count) {
    if (choice.kind != Kind::Device)
        return false;
    const auto resolved = synth::resolveAudioInput(choice, status);
    return resolved.connected && resolved.firstChannel == first && resolved.numChannels == count;
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
        menu.addItem(Ids::kFirstMono + channel, namedInputLabel(status.nameOfInput(channel, 1), channel, 1),
                     status.isChannelActive(channel), isTicked(choice, status, channel, 1));
    }
    if (inputs >= 2) {
        menu.addSectionHeader("Stereo" + dot() + deviceLabel(status));
        for (int first = 0; first + 1 < inputs; first += 2)
            menu.addItem(Ids::kFirstStereo + first, namedInputLabel(status.nameOfInput(first, 2), first, 2),
                         pairActive(status, first), isTicked(choice, status, first, 2));
    }
    if (anyOff && !status.hosted)
        menu.addItem(Ids::kTurnOnInputs, "Turn on all its inputs");
}

const AudioInputTrackSource* findSource(const std::vector<AudioInputTrackSource>& sources, synth::TrackId id) {
    for (const auto& source : sources)
        if (source.id == id)
            return &source;
    return nullptr;
}

AudioInputRowText describeTrackInput(const synth::AudioInputChoice& choice,
                                     const std::vector<AudioInputTrackSource>& sources) {
    AudioInputRowText row;
    const auto* source = findSource(sources, choice.sourceTrack);
    row.warning = !synth::isTrackInputConnected(choice, {source != nullptr, source != nullptr && source->hasChannel});
    if (source == nullptr) {
        row.text = "(track removed)";
        row.note = "The track this one recorded is gone. Pick another input to record again.";
    } else if (!source->hasChannel) {
        row.text = source->name + " (no mixer channel)";
        row.note = source->name + " does not reach a mixer channel, so there is nothing to record from it.";
    } else {
        row.text = source->name;
    }
    return row;
}

void addTrackSources(juce::PopupMenu& menu, const synth::AudioInputChoice& choice,
                     const std::vector<AudioInputTrackSource>& sources) {
    juce::PopupMenu sub;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& source = sources[i];
        sub.addItem(Ids::kFirstTrack + (int)i, source.hasChannel ? source.name : source.name + " (no mixer channel)",
                    source.hasChannel, choice.kind == Kind::Track && choice.sourceTrack == source.id);
    }
    menu.addSubMenu("Another track", sub, !sources.empty(), nullptr, choice.kind == Kind::Track);
}
} // namespace

std::vector<AudioInputTrackSource> listAudioInputTrackSources(const synth::TimelineDoc& doc, synth::TrackId target,
                                                              const std::function<bool(synth::TrackId)>& hasChannel) {
    std::vector<AudioInputTrackSource> sources;
    for (const auto& track : doc.getTracks())
        if (track.id != target && track.kind != synth::TrackKind::Automation)
            sources.push_back({track.id, track.name, hasChannel && hasChannel(track.id)});
    return sources;
}

AudioInputRowText describeAudioInput(const synth::AudioInputChoice& choice, const synth::AudioInputStatus& status,
                                     const std::vector<AudioInputTrackSource>& sources) {
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
    if (choice.kind == Kind::Track)
        return describeTrackInput(choice, sources);
    const auto resolved = synth::resolveAudioInput(choice, status);
    if (resolved.nameMissing) {
        row.text = choice.inputName + " (not on this interface)";
        row.warning = true;
        row.note = "This track records and plays nothing until you pick one of this interface's inputs here.";
        return row;
    }
    const auto name = choice.inputName.isNotEmpty()
                          ? namedInputLabel(choice.inputName, resolved.firstChannel, resolved.numChannels)
                          : inputName(resolved.firstChannel, resolved.numChannels);
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
    row.text = choice.inputName.isNotEmpty() || resolved.numChannels > 1 ? name : name + " (mono)";
    return row;
}

juce::PopupMenu buildAudioInputMenu(const synth::AudioInputChoice& choice, const synth::AudioInputStatus& status,
                                    const std::vector<AudioInputTrackSource>& sources) {
    juce::PopupMenu menu;
    menu.addItem(Ids::kNoInput, "No input", true, choice.kind == Kind::None);
    menu.addSeparator();
    addDeviceInputs(menu, choice, status);
    // The track's own choice stays on the menu, ticked, while it cannot be picked from the list above.
    if (choice.kind == Kind::Device && !synth::isAudioInputConnected(choice, status))
        menu.addItem(Ids::kNotConnected, describeAudioInput(choice, status).text, true, true);
    menu.addSeparator();
    addTrackSources(menu, choice, sources);
    // A source deleted since the pick stays on the menu, ticked, like a device that is not connected.
    if (choice.kind == Kind::Track && findSource(sources, choice.sourceTrack) == nullptr)
        menu.addItem(Ids::kTrackRemoved, describeAudioInput(choice, status, sources).text, true, true);
    menu.addSeparator();
    menu.addItem(Ids::kWholeMix, "Whole mix", true, choice.kind == Kind::WholeMix);
    if (!status.hosted) {
        menu.addSeparator();
        menu.addItem(Ids::kAudioSettings, "Audio settings...");
    }
    return menu;
}

AudioInputMenuAction resolveAudioInputMenuChoice(int menuId, const synth::AudioInputStatus& status,
                                                 const std::vector<AudioInputTrackSource>& sources) {
    AudioInputMenuAction action;
    const int inputs = listedInputs(status);
    auto pick = [&](int first, int count) {
        action.kind = AudioInputMenuAction::Kind::Pick;
        action.choice.kind = Kind::Device;
        action.choice.firstChannel = first;
        action.choice.numChannels = count;
        action.choice.deviceName = status.hosted ? juce::String() : status.deviceName;
        action.choice.inputName = status.hosted ? juce::String() : status.nameOfInput(first, count);
    };
    if (menuId == Ids::kNoInput || menuId == Ids::kWholeMix) {
        action.kind = AudioInputMenuAction::Kind::Pick;
        action.choice.kind = menuId == Ids::kNoInput ? Kind::None : Kind::WholeMix;
    } else if (menuId == Ids::kTurnOnInputs && !status.hosted) {
        action.kind = AudioInputMenuAction::Kind::TurnOnInputs;
    } else if (menuId == Ids::kAudioSettings && !status.hosted) {
        action.kind = AudioInputMenuAction::Kind::OpenAudioSettings;
    } else if (menuId >= Ids::kFirstTrack && menuId < Ids::kFirstTrack + (int)sources.size()) {
        const auto& source = sources[(std::size_t)(menuId - Ids::kFirstTrack)];
        if (source.hasChannel) {
            action.kind = AudioInputMenuAction::Kind::Pick;
            action.choice.kind = Kind::Track;
            action.choice.sourceTrack = source.id;
        }
    } else if (menuId >= Ids::kFirstMono && menuId < Ids::kFirstMono + inputs) {
        pick(menuId - Ids::kFirstMono, 1);
    } else if (menuId >= Ids::kFirstStereo && menuId + 1 < Ids::kFirstStereo + inputs &&
               (menuId - Ids::kFirstStereo) % 2 == 0) {
        pick(menuId - Ids::kFirstStereo, 2);
    }
    return action;
}

} // namespace synth::ui
