// MixerTrackSelectionHost.h: what the mixer strips need from the timeline's track selection (one shared model, ids
// are TrackIds, UI/Timeline/TrackSelectionModel.h) and from the delete and move that act on it. MainComponent fills it;
// a panel with no host (tests, the bare panel) has no selection and its strips act one at a time.
#pragma once

#include "Timeline/TimelineDoc/TimelineTypes.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

struct MixerTrackSelectionHost {
    std::function<bool(synth::TrackId)> isSelected;
    /** A click or key on a strip's track: plain resets the selection to it, Cmd/Ctrl toggles, Shift extends. */
    std::function<void(synth::TrackId, const juce::ModifierKeys&)> select;
    /** Cmd+Backspace and the menu's Delete: the selected set when `track` is one of several, else `track`. The host
     *  asks first (the timeline's question and its "ask before deleting" setting) and deletes in one undo step. */
    std::function<void(synth::TrackId track, bool fromMenu)> deleteTracks;
    /** The whole new track order after a keyboard move; one undo step. */
    std::function<void(const std::vector<synth::TrackId>&)> reorderTracks;
    /** Every selected track, in timeline order. */
    std::function<std::vector<synth::TrackId>()> selected;
};

} // namespace synth::ui
