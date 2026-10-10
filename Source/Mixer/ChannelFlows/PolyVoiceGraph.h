#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth {

class MacroSet;

/** One timeline track as the voice-graph planner needs it: the uuid of its source node (Track In / Track Audio)
 *  and the name to show the user. */
struct PolyTrackRef {
    juce::String sourceUuid;
    juce::String name;
};

/** What a Poly click on `clicked` would switch. Pure query: no graph mutation, no undo. */
struct PolyVoiceGraphPlan {
    /** Poly-capable modules of the voice graph this click owns: the clicked module, and every connected one that is
     *  owned by the clicked module's track(s) or by no track at all. */
    std::vector<juce::AudioProcessorGraph::NodeID> thisTrackNodes;
    /** Poly-capable modules of the voice graph that are owned ONLY by other tracks. */
    std::vector<juce::AudioProcessorGraph::NodeID> otherTrackNodes;
    /** The distinct names of the tracks owning `otherTrackNodes`, in timeline order. */
    std::vector<juce::String> otherTrackNames;
};

/** True when `processor` declares a "poly" AudioParameterBool (Oscillator, Wavetable, Noise, Filter, ADSR, VCA). */
bool hasPolyParameter(const juce::AudioProcessor* processor);

/**
 * The voice graph of `clicked`: an undirected walk over every cable (audio, CV and MIDI), starting at `clicked`.
 * The walk is reached-but-not-expanded at a Channel Strip, Master, a track source node, a Gate/EQ/Compressor, a
 * Record Tap or an Audio Output (a different channel starts there), and never crosses a sidechain key cable. Macro
 * port nodes and hidden attenuverters are ordinary nodes to it, so a module inside a macro and one wired into the
 * macro from outside are the same voice graph. Parallel branches are included by construction.
 *
 * A node belongs to track T when it is forward-reachable from T's source node (stopping at a strip). The clicked
 * module's own tracks are the ones that reach it; a node no track reaches counts as this track's.
 *
 * @param tracks the timeline's tracks in order (those without a live source node own nothing).
 */
PolyVoiceGraphPlan planPolyVoiceGraph(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID clicked,
                                      const std::vector<PolyTrackRef>& tracks);

/** How applyPolyVoiceGraph reaches the parts that differ between a headless caller and the app. */
struct PolyVoiceGraphOptions {
    /** Optional macro membership: a Poly MIDI node this call removes is dropped from its macro. */
    MacroSet* macros = nullptr;
    /** True: a new Poly MIDI node gets no x/y, for the caller to place (placeNewModulesBesideConnections). False: it
     *  is put on the 8 px grid left of the first module it feeds. */
    bool leavePolyMidiUnplaced = false;
    /** Removes Poly MIDI nodes left with no outgoing cable. Unset: graph.removeNode directly. The app passes its
     *  card-aware removal. */
    std::function<void(const std::vector<juce::AudioProcessorGraph::NodeID>&)> removeNodes;
};

struct PolyVoiceGraphResult {
    std::vector<juce::AudioProcessorGraph::NodeID> flipped;         ///< Modules whose poly value changed.
    std::vector<juce::AudioProcessorGraph::NodeID> addedPolyMidi;   ///< Poly MIDI nodes this call inserted.
    std::vector<juce::AudioProcessorGraph::NodeID> removedPolyMidi; ///< Poly MIDI nodes this call removed.
};

/**
 * Sets "poly" to `poly` on every poly-capable module of `nodes` and keeps the voices playable. NO UNDO: the caller
 * owns the surrounding transaction. Message thread only.
 *
 * Turning poly ON: for each MIDI source node whose MIDI output directly feeds a module that just went poly
 * (Oscillator, Wavetable, ADSR), one Poly MIDI node is inserted (an existing one fed by the same source is reused),
 * wired source MIDI -> Poly MIDI, Poly MIDI pitch ch0-7 -> each Oscillator/Wavetable pitch, Poly MIDI gate ch8-15 ->
 * each ADSR gate. The raw MIDI cables stay (harmless in poly mode).
 *
 * Turning poly OFF: Poly MIDI -> module pitch/gate cables to the modules going mono are removed first, each of
 * those modules gets a raw MIDI cable from the source that fed that Poly MIDI (when missing), and a Poly MIDI node
 * left with no outgoing cable is removed.
 *
 * The cables of a flipped module are re-anchored to its new channel layout by the module's card listening to the
 * parameter (ModuleComponent::applyPolyStateChange); with no card, only the parameter changes.
 */
PolyVoiceGraphResult applyPolyVoiceGraph(juce::AudioProcessorGraph& graph,
                                         const std::vector<juce::AudioProcessorGraph::NodeID>& nodes, bool poly,
                                         const PolyVoiceGraphOptions& options = {});

/** "Bass", "Bass and Lead", "Bass, Lead and Pad" -- the dialog's track list. */
juce::String describeTrackNames(const std::vector<juce::String>& names);

} // namespace synth
