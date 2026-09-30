// Concern: TimelineRoutingPane's clicks -- the re-bind menu, the MIDI-destination picker and the two "show" links.
// Every edit goes out through the TrackHeaderHost the track header uses, so it is the same undoable step there.
#include "TimelineRoutingPane.h"

#include "UI/Timeline/TrackRoutingMenus.h"

namespace synth::ui {

void TimelineRoutingPane::openBindingMenu() {
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    if (host_ == nullptr || track == nullptr)
        return;

    // The same menu the header's binding chip opens; the "MIDI destinations..." entry is left to the "MIDI
    // destinations" row.
    auto menu = buildTrackBindingMenu(host_->getAvailableTrackInNodes(track_), track->bindingUuid,
                                      /*includeMidiDestinations=*/false);
    if (showBindingMenuHook_) {
        showBindingMenuHook_(menu);
        return;
    }
    juce::Component::SafePointer<TimelineRoutingPane> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&canvasNode_), [safeThis](int result) {
        if (auto* self = safeThis.getComponent())
            self->applyBindingMenuChoice(result);
    });
}

void TimelineRoutingPane::applyBindingMenuChoice(int menuId) {
    if (host_ == nullptr)
        return;
    // The options are re-collected at apply time, like the header does: ids are positions in that list.
    applyTrackBindingChoice(*host_, track_, host_->getAvailableTrackInNodes(track_), menuId);
    refresh();
}

std::unique_ptr<MidiDestinationPicker> TimelineRoutingPane::buildPicker() {
    juce::Component::SafePointer<TimelineRoutingPane> safeThis(this);
    return buildTrackMidiDestinationPicker(
        [safeThis]() -> TrackHeaderHost* { return safeThis != nullptr ? safeThis->host_ : nullptr; }, track_);
}

void TimelineRoutingPane::openMidiDestinationsPicker() {
    if (openDestinationsHook_) {
        openDestinationsHook_();
        return;
    }
    auto picker = buildPicker();
    if (picker == nullptr)
        return; // no host: nothing to build a picker against
    juce::CallOutBox::launchAsynchronously(std::move(picker), midiDestinations_.getScreenBounds(), nullptr);
}

// The existing select path (a highlight of the node in the graph editor's selection, no canvas scroll), the same one
// the header's binding chip uses.
void TimelineRoutingPane::showOnCanvas() {
    const auto* track = doc_ != nullptr ? doc_->getTrack(track_) : nullptr;
    if (host_ != nullptr && track != nullptr && track->bindingUuid.isNotEmpty() && !track->orphaned)
        host_->selectNodeInGraph(track->bindingUuid);
}

void TimelineRoutingPane::showInMixer() {
    if (host_ == nullptr)
        return;
    if (auto* link = host_->getChannelLinkSurface())
        link->revealChannelForTrack(track_);
}

} // namespace synth::ui
