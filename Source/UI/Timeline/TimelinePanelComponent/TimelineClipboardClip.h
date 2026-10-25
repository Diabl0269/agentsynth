#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

// One captured clip, relative to the earliest selected clip's start at copy time (see copySelectedClips()).
// Notes are already clip-relative in the doc; addNote() reassigns their ids on paste.
struct TimelineClipboardClip {
    synth::TrackId originalTrack;
    // The track kind this clip needs on paste, derived at COPY time from the payload
    // (non-empty assetRef -> Audio, else Midi) rather than from the track it sat on: the
    // payload is what decides where it can be played, and it is also what
    // TimelineDoc::moveClipToTrack checks.
    synth::TrackKind requiredKind = synth::TrackKind::Midi;
    double relativeStartBeat = 0.0;
    double lengthBeats = 4.0;
    juce::String name;
    std::vector<synth::MidiNote> notes; // each note's own muted flag travels with it
    bool muted = false;
    // Audio fields and the drum machine's settings: captured and restored so a paste yields the same clip.
    juce::String assetRef;
    double gainDb = 0.0;
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;
    double sourceStartSeconds = 0.0;
    int stepsPerBar = synth::Clip::kDefaultStepsPerBar;
    double swing = synth::Clip::kStraightSwing;
    double humanise = 0.0;
    std::vector<synth::Clip::DrumRow> drumRows;
    std::vector<int> mutedPitches, soloPitches;
};
} // namespace synth::ui
