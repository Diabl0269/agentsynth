#pragma once

// "Convert to melody" on an audio clip's menu: the off-thread analysis and the one-undo-step result. A friend of
// MainComponent, so each takes the component first. Message thread only, except the analysis it launches.

#include "Timeline/Melody/MelodyQuantize.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

class MainComponent;

struct MainComponentConvertToMelody {
    /** Analyses audio clip `id` in the background, then adds an instrument track holding the melody as one undo step.
     */
    static void request(MainComponent& mc, synth::ClipId id, bool snapToKey);
    /** The message-thread half: adds the track and clip for `notes` as one undo step and selects the clip. Says so
     *  in the status bar, and changes nothing, when `notes` is empty or the clip is gone. `clipKey` goes on the new
     *  clip when set (the song has no key of its own). Returns the new clip, or an invalid id. */
    static synth::ClipId apply(MainComponent& mc, synth::ClipId audioClip,
                               const std::vector<synth::melody::MelodyNote>& notes,
                               const std::optional<synth::MusicalScale>& clipKey);
};
