// Concern: TimelineRoutingPane's audio Input section (audio tracks) -- which input the track records from, its level,
// and whether the track plays it live (Monitor): what the controls say, the menu, and the one undoable edit a choice
// makes. The menu and the words are AudioInputMenu's pure functions. docs/timeline/tracks.md#the-input-row
#include "TimelineRoutingPane.h"

namespace synth::ui {

// The inputs on offer are asked for here, on a refresh, never from the meter tick (asking may probe the driver).
void TimelineRoutingPane::computeAudioInput(const synth::Track& track) {
    if (!view_.isAudio)
        return;
    auto* inputs = host_ != nullptr ? host_->getAudioInputSurface() : nullptr;
    const auto status = inputs != nullptr ? inputs->getAudioInputStatus() : synth::AudioInputStatus{};
    view_.audioInput = describeAudioInput(track.audioInput, status);
    view_.monitorIndex = static_cast<int>(track.monitorMode);
    const bool liveDevice = track.audioInput.kind == synth::AudioInputChoice::Kind::Device && !view_.audioInput.warning;
    view_.meterFirst = liveDevice ? track.audioInput.firstChannel : 0;
    view_.meterCount = liveDevice && inputs != nullptr ? track.audioInput.numChannels : 0;
}

// The screen-reader name carries the value, as on the MIDI row: a button's text is not read once it has a title.
void TimelineRoutingPane::applyAudioInputToChildren() {
    if (!view_.isAudio)
        return; // a section fading out keeps what it showed
    const auto& row = view_.audioInput;
    audioInput_.setButtonText(row.text);
    audioInput_.setWarning(row.warning);
    audioInput_.setMuted(row.muted);
    audioInput_.setTitle("Audio input, " + row.text);
    audioInput_.setTooltip(row.warning ? "Where this track records from. " + row.note
                                       : juce::String("Where this track records from. Click to choose an input."));
    if (view_.meterCount > 0)
        inputMeter_.setChannelCount(view_.meterCount);
    monitorSwitch_.setSelectedIndex(view_.monitorIndex, juce::dontSendNotification);
}

void TimelineRoutingPane::openAudioInputMenu() {
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    auto* inputs = host_ != nullptr ? host_->getAudioInputSurface() : nullptr;
    if (track == nullptr || inputs == nullptr)
        return;
    auto menu = buildAudioInputMenu(track->audioInput, inputs->getAudioInputStatus());
    showInputMenu(menu, audioInput_, &TimelineRoutingPane::applyAudioInputChoice);
}

// The inputs are re-read at apply time, as the MIDI device menu re-reads its devices: a pick names the device open
// now. Turning inputs on lands later as a device change, which refreshes the pane.
void TimelineRoutingPane::applyAudioInputChoice(int menuId) {
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    auto* inputs = host_ != nullptr ? host_->getAudioInputSurface() : nullptr;
    if (track == nullptr || inputs == nullptr || track->kind != synth::TrackKind::Audio)
        return;
    const auto action = resolveAudioInputMenuChoice(menuId, inputs->getAudioInputStatus());
    switch (action.kind) {
    case AudioInputMenuAction::Kind::Nothing:
        return;
    case AudioInputMenuAction::Kind::TurnOnInputs:
        inputs->enableDeviceInputs();
        return;
    case AudioInputMenuAction::Kind::OpenAudioSettings:
        inputs->openAudioSettings();
        return;
    case AudioInputMenuAction::Kind::Pick:
        break;
    }
    auto* doc = doc_;
    const auto id = track_;
    const auto choice = action.choice;
    host_->performTrackEdit([doc, id, choice] { doc->setTrackAudioInput(id, choice); });
    refresh();
}

void TimelineRoutingPane::applyMonitorChoice(int index) {
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    if (host_ == nullptr || track == nullptr || track->kind != synth::TrackKind::Audio || index < 0 || index > 2)
        return;
    auto* doc = doc_;
    const auto id = track_;
    const auto mode = static_cast<synth::MonitorMode>(index);
    host_->performTrackEdit([doc, id, mode] { doc->setTrackMonitorMode(id, mode); });
    refresh();
}

} // namespace synth::ui
