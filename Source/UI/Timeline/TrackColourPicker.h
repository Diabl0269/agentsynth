#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Timeline/TrackChannelLinkSurface.h"
#include <functional>
#include <memory>

// TrackColourPicker.h (docs/timeline/tracks.md#colour-swatch): the colour picker of one track, built the same way for
// every surface that recolours a track -- the Timeline's track-header swatch and the mixer column's colour dot -- so a
// pick means the same thing wherever it is made.
namespace synth::ui {

/** What the picker needs from its host. `doc` must outlive the popup. */
struct TrackColourPickerContext {
    synth::TimelineDoc* doc = nullptr;
    /** The favourites shelf's store; null keeps the favourites in memory. */
    juce::PropertiesFile* favourites = nullptr;
    /** The track/macro colour link; null (or a track with no owned macro) picks the track alone. */
    TrackChannelLinkSurface* link = nullptr;
    /** Runs `mutation` as one undo step (MainComponent::performTrackEdit); null runs it directly, unrecorded. */
    std::function<void(const std::function<void()>& mutation)> performEdit;
    /** False once the surface that opened the popup is gone, so a late commit does nothing; null means always alive. */
    std::function<bool()> isAlive;
};

/** The popup for `track`'s colour, or null when the track is gone. Live preview writes the colour with no undo step,
 *  closing with no net change restores it, and the final pick is one undo step whose undo restores the original.
 *  A track that owns its channel macro fans every preview and the commit out to the macro too. */
std::unique_ptr<ColourPickerPopup> buildTrackColourPicker(const TrackColourPickerContext& context,
                                                          synth::TrackId track);

} // namespace synth::ui
