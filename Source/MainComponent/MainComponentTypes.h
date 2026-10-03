#pragma once

// Nested value types of MainComponent, declared at namespace scope so MainComponent.h stays a
// declaration list. MainComponent re-exports each as a member alias, so every `MainComponent::X`
// spelling still compiles.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>

namespace synth::maincomponent {

/** Which surface Copy/Paste/Duplicate/Cut/Repeat act on. Enumerators are only ever appended, so no
 *  existing value moves (same convention as AppCommands::CommandIDs). */
enum class EditSurface { Graph, TimelineClips, PianoRoll, Mixer, AutomationLane };

/** The three sliding panels MainComponent docks. */
enum class SlidingPanel { Library, AiChat, Timeline };

/** What the user picked in the unsaved-changes dialog: Save runs performSaveProject; Discard
 *  continues immediately; Cancel abandons the action that asked. */
enum class UnsavedChangesChoice { Save, Discard, Cancel };

/** Pick for a pending autosave sidecar: Restore loads autosave.json (stays dirty), Discard loads
 *  project.json; either deletes the sidecar. */
enum class AutosaveRecoveryChoice { Restore, Discard };

/** The choice when opening a `.json` patch: replace the current one, add on top, or cancel. */
enum class PatchLoadMode { Replace, Append, Cancel };

/** Step state threaded through buildInstrumentTrackAndChain's helpers (MainComponentTrackCreation.cpp). */
struct InstrumentChainBuild {
    synth::TrackId trackId;
    juce::AudioProcessorGraph::Node* trackInNode = nullptr;
    juce::String trackInUuid;
    juce::Point<int> trackInPosition, trackInSize;
    juce::AudioProcessorGraph::Node* instrumentNode = nullptr;
    juce::String instrumentModuleType, instrumentUuid;
    juce::AudioProcessorGraph::Node* chainSource = nullptr;
    int sourceRightChannel = 1;
    juce::String chainSourceType;
    juce::Point<int> chainSourcePosition;
    juce::String voiceMixerUuid, polyMidiUuid, adsrUuid, vcaUuid;
};

/** Message-thread state of one armed-Audio-track take, from the Record-on click to the commit.
 *  Capture starts at the click, so a take is either rolling or not. The punch is the earliest beat
 *  the COMMITTED CLIP may start at; pre-roll frames are recorded, then trimmed out of the clip
 *  window. The capture* fields are FROZEN at capture start and never re-read at commit. */
struct AudioTake {
    bool capturing = false;   // the tap is writing
    synth::TrackId track;     // the armed Audio track the clip lands on
    double punchInBeat = 0.0; // earliest beat the committed clip may start at
    juce::File wavFile;       // absolute path being written
    juce::File peaksFile;     // its .agpk sidecar
    juce::String assetRef;    // what the committed clip stores (see synth::Clip::assetRef)
    juce::AudioProcessorGraph::NodeID tapNode;

    double captureSampleRate = 44100.0;
    double captureBpm = 120.0;
    int captureRecordingLatencySamples = 0;
};

} // namespace synth::maincomponent
