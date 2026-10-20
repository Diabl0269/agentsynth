// Concern: TimelineRoutingPane's Input row (MIDI tracks) -- which keyboard or controller, and which MIDI channel,
// records into the track: what the two buttons say, their menus, and the one undoable edit a choice makes.
// docs/timeline/tracks.md#the-input-row
#include "TimelineRoutingPane.h"

namespace synth::ui {

namespace {
juce::String channelText(int channel) {
    return channel == 0 ? juce::String("Any channel") : "Channel " + juce::String(channel);
}

bool isConnected(const std::vector<TrackHeaderHost::MidiInputDevice>& devices, const juce::String& identifier) {
    return std::any_of(devices.begin(), devices.end(), [&](const auto& d) { return d.identifier == identifier; });
}
} // namespace

// A stored device that is not connected now is named, in the warning colour, rather than shown as "All MIDI inputs":
// the take records nothing from it until it is back, and the row must say so.
void TimelineRoutingPane::computeMidiInput(const synth::Track& track) {
    if (!view_.isMidi)
        return;
    view_.inputChannelText = channelText(track.midiInputChannel);
    if (track.midiInputId.isEmpty()) {
        view_.inputDeviceText = "All MIDI inputs";
        return;
    }
    const auto devices =
        host_ != nullptr ? host_->getMidiInputDevices() : std::vector<TrackHeaderHost::MidiInputDevice>{};
    view_.inputDeviceName = track.midiInputName.isNotEmpty() ? track.midiInputName : track.midiInputId;
    view_.inputDeviceMissing = !isConnected(devices, track.midiInputId);
    view_.inputDeviceText =
        view_.inputDeviceMissing ? view_.inputDeviceName + " (not connected)" : view_.inputDeviceName;
}

// The screen-reader name carries the value: a button's text is not read once it has a title.
void TimelineRoutingPane::applyMidiInputToChildren() {
    if (!view_.isMidi)
        return; // a row fading out keeps what it showed
    midiInputDevice_.setButtonText(view_.inputDeviceText);
    midiInputDevice_.setWarning(view_.inputDeviceMissing);
    midiInputDevice_.setTitle("MIDI input, " + view_.inputDeviceText);
    midiInputDevice_.setTooltip(view_.inputDeviceMissing
                                    ? "'" + view_.inputDeviceName +
                                          "' is not connected, so this track records nothing. Click to choose an input."
                                    : "Records from " + view_.inputDeviceText + ". Click to choose a MIDI input.");
    midiInputChannel_.setButtonText(view_.inputChannelText);
    midiInputChannel_.setTitle("Input channel, " + view_.inputChannelText);
    midiInputChannel_.setTooltip("Records " + view_.inputChannelText.toLowerCase() +
                                 ". Click to choose a MIDI channel.");
}

void TimelineRoutingPane::openMidiInputDeviceMenu() {
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    if (host_ == nullptr || track == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addItem(kAllInputsMenuId, "All MIDI inputs", true, track->midiInputId.isEmpty());
    menu.addSeparator();
    const auto devices = host_->getMidiInputDevices();
    for (size_t i = 0; i < devices.size(); ++i)
        menu.addItem(kFirstInputDeviceMenuId + static_cast<int>(i), devices[i].name, true,
                     devices[i].identifier == track->midiInputId);
    if (track->midiInputId.isNotEmpty() && !isConnected(devices, track->midiInputId))
        menu.addItem(kMissingInputDeviceMenuId, view_.inputDeviceText, true, true);
    if (devices.empty() && track->midiInputId.isEmpty())
        menu.addItem(-1, "No MIDI inputs connected", false, false);
    showInputMenu(menu, midiInputDevice_, &TimelineRoutingPane::applyMidiInputDeviceChoice);
}

void TimelineRoutingPane::openMidiInputChannelMenu() {
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    if (host_ == nullptr || track == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addItem(kAnyChannelMenuId, "Any channel", true, track->midiInputChannel == 0);
    menu.addSeparator();
    for (int channel = 1; channel <= synth::Track::kMaxMidiInputChannel; ++channel)
        menu.addItem(kAnyChannelMenuId + channel, channelText(channel), true, track->midiInputChannel == channel);
    showInputMenu(menu, midiInputChannel_, &TimelineRoutingPane::applyMidiInputChannelChoice);
}

void TimelineRoutingPane::showInputMenu(juce::PopupMenu& menu, juce::Component& anchor,
                                        void (TimelineRoutingPane::*apply)(int)) {
    if (showInputMenuHook_) {
        showInputMenuHook_(menu);
        return;
    }
    juce::Component::SafePointer<TimelineRoutingPane> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor), [safeThis, apply](int result) {
        if (auto* self = safeThis.getComponent())
            (self->*apply)(result);
    });
}

// Devices are re-collected at apply time, as the binding menu does: an id is a position in that list. Picking the
// ticked not-connected device, or dismissing the menu, changes nothing.
void TimelineRoutingPane::applyMidiInputDeviceChoice(int menuId) {
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    if (host_ == nullptr || track == nullptr || track->kind != synth::TrackKind::Midi)
        return;
    juce::String identifier, name;
    if (menuId != kAllInputsMenuId) {
        const auto devices = host_->getMidiInputDevices();
        const int index = menuId - kFirstInputDeviceMenuId;
        if (menuId == kMissingInputDeviceMenuId || index < 0 || index >= static_cast<int>(devices.size()))
            return;
        identifier = devices[static_cast<size_t>(index)].identifier;
        name = devices[static_cast<size_t>(index)].name;
    }
    auto* doc = doc_;
    const auto id = track_;
    const int channel = track->midiInputChannel;
    host_->performTrackEdit(
        [doc, id, identifier, name, channel] { doc->setTrackMidiInput(id, identifier, name, channel); });
    refresh();
}

void TimelineRoutingPane::applyMidiInputChannelChoice(int menuId) {
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    const int channel = menuId - kAnyChannelMenuId;
    if (host_ == nullptr || track == nullptr || channel < 0 || channel > synth::Track::kMaxMidiInputChannel)
        return;
    auto* doc = doc_;
    const auto id = track_;
    const auto identifier = track->midiInputId;
    const auto name = track->midiInputName;
    host_->performTrackEdit(
        [doc, id, identifier, name, channel] { doc->setTrackMidiInput(id, identifier, name, channel); });
    refresh();
}

} // namespace synth::ui
