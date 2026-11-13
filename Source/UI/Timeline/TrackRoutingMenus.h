#pragma once

#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <functional>
#include <memory>
#include <vector>

// TrackRoutingMenus.h (docs/timeline/tracks.md#routing-from-the-side-pane): the re-bind menu and the MIDI-destination
// picker of a timeline track, built once for both places that offer them -- the track header's binding chip and the
// Timeline side pane's routing rows. Free functions over TrackHeaderHost: neither caller keeps a second copy of the
// menu ids, the kind-aware candidate list or the picker's provider/apply wiring.
namespace synth::ui {

/** The re-bind menu: candidate `i` has id `i + 1` (`options` is TrackHeaderHost::getAvailableTrackInNodes, the current
 *  binding ticked), then "New Track In node" (kNewTrackInNodeMenuId). `includeMidiDestinations` (MIDI tracks) adds the
 *  header chip's "MIDI destinations..." entry (kMidiDestinationsMenuId). */
juce::PopupMenu buildTrackBindingMenu(const std::vector<TrackHeaderHost::BindingOption>& options,
                                      const juce::String& currentUuid, bool includeMidiDestinations);

/** Applies a re-bind menu choice through `host` (one undo step there): 1..N binds to `options[id - 1]`,
 *  kNewTrackInNodeMenuId creates and binds a node. Returns false, doing nothing, for anything else -- including
 *  kMidiDestinationsMenuId, which the caller owns because only it knows where the picker anchors. */
bool applyTrackBindingChoice(TrackHeaderHost& host, synth::TrackId track,
                             const std::vector<TrackHeaderHost::BindingOption>& options, int menuId);

/** A MidiDestinationPicker whose provider and apply callbacks route through `hostProvider()` for `track`. The provider
 *  is asked on every call, so a picker that outlives its caller (a CallOutBox) goes inert instead of dangling: return
 *  null once the owner is gone. Null when `hostProvider()` is null now. */
std::unique_ptr<MidiDestinationPicker> buildTrackMidiDestinationPicker(std::function<TrackHeaderHost*()> hostProvider,
                                                                       synth::TrackId track);

} // namespace synth::ui
