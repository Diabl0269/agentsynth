#pragma once

// The audio-import helpers and the Sounds view's load, split out of MainComponent to keep its header under the
// file-size cap. A friend of MainComponent, so each takes the component as its first parameter. Message thread only.

#include "Modules/Shapes/ShapeLibrary.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>

class MainComponent;

struct MainComponentAudioImport {
    /** `file`'s duration in beats at the transport's current bpm; 0.0 when it is not readable audio. */
    static double audioFileLengthInBeats(const MainComponent& mc, const juce::File& file);
    /** Copies `sourceFile` into the project and returns its asset ref; empty after a status-bar message when it fails.
     */
    static juce::String importAudioAsset(MainComponent& mc, const juce::File& sourceFile);
    /** Imports `sourceFile`, then adds a new audio track holding a clip of it at `startBeat`, as one undo step. */
    static void addAudioTrackWithClip(MainComponent& mc, double startBeat, const juce::File& sourceFile);
    /** The body of addAudioTrack's undo step (call inside a recorded change); an invalid id when no track was added. */
    static synth::TrackId buildAudioTrackChannel(MainComponent& mc, int index, juce::String& trackName);
    /** Loads `sound` into the selected Sampler, else the selected audio track at the playhead, else a new Sampler. */
    static void loadSoundFromBrowser(MainComponent& mc, const juce::File& sound);
    /** Return on a shape in the Sounds view: stamps it onto the selected Filter or LFO card (one undo step); false,
     * with nothing changed, when no selected card takes it. */
    static bool stampShapeFromBrowser(MainComponent& mc, const synth::shapes::LibraryShape& shape);
};
