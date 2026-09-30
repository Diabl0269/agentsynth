// Concern: the shared re-bind menu and MIDI-destination picker builders (see TrackRoutingMenus.h).
#include "TrackRoutingMenus.h"

namespace synth::ui {

juce::PopupMenu buildTrackBindingMenu(const std::vector<TrackHeaderHost::BindingOption>& options,
                                      const juce::String& currentUuid, bool includeMidiDestinations) {
    juce::PopupMenu menu;
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        const auto& option = options[static_cast<size_t>(i)];
        menu.addItem(i + 1, option.displayName, true, option.uuid == currentUuid);
    }
    if (!options.empty())
        menu.addSeparator();
    menu.addItem(TimelineTrackHeaderComponent::kNewTrackInNodeMenuId, "New Track In node");

    // MIDI destinations only make sense for a MIDI-kind track -- an Audio or Automation track's binding feeds no
    // MIDI-consuming node, so offering the entry there would open a picker with nothing it could ever wire.
    if (includeMidiDestinations) {
        menu.addSeparator();
        menu.addItem(TimelineTrackHeaderComponent::kMidiDestinationsMenuId, "MIDI destinations...");
    }
    return menu;
}

bool applyTrackBindingChoice(TrackHeaderHost& host, synth::TrackId track,
                             const std::vector<TrackHeaderHost::BindingOption>& options, int menuId) {
    if (menuId == TimelineTrackHeaderComponent::kNewTrackInNodeMenuId) {
        host.createAndBindTrackInNode(track);
        return true;
    }
    if (menuId < 1 || menuId > static_cast<int>(options.size()))
        return false;
    // An explicit user choice -- the ONLY way a binding ever changes. Never matched by name.
    host.bindTrackTo(track, options[static_cast<size_t>(menuId) - 1].uuid);
    return true;
}

std::unique_ptr<MidiDestinationPicker> buildTrackMidiDestinationPicker(std::function<TrackHeaderHost*()> hostProvider,
                                                                       synth::TrackId track) {
    if (!hostProvider || hostProvider() == nullptr)
        return nullptr;

    // TrackHeaderHost::MidiDestinationOption and MidiDestinationPicker::Option carry the same fields by design (the
    // header stays graph-free, so it can't hand the picker anything richer) -- converted here rather than sharing one
    // type, so the picker's header has no dependency on TimelineDoc/TrackHeaderHost at all.
    return std::make_unique<MidiDestinationPicker>(
        [hostProvider, track]() -> std::vector<MidiDestinationPicker::Option> {
            auto* host = hostProvider();
            if (host == nullptr)
                return {};
            using PickerGroup = MidiDestinationPicker::Option::Group;
            std::vector<MidiDestinationPicker::Option> options;
            for (const auto& option : host->getMidiDestinationOptions(track))
                options.push_back({option.displayName, option.nodeUid, option.connected,
                                   option.isInstrument ? PickerGroup::Instruments : PickerGroup::Other});
            return options;
        },
        [hostProvider, track](juce::uint32 nodeUid, bool connect) {
            if (auto* host = hostProvider())
                host->setMidiDestinationConnected(track, nodeUid, connect);
        });
}

} // namespace synth::ui
