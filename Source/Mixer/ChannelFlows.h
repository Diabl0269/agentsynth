#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** The nodes buildDefaultAudioChannel() created (or, for `master`, spliced/reused) — every field
 *  empty/null when the build failed partway (out-of-memory-class failures only; see the function
 *  comment). `strip`/`master` are raw observing pointers, valid exactly as long as the graph node
 *  itself is (same lifetime rule every other `Node*` in this codebase follows). */
struct DefaultChannel {
    juce::String eqUuid;
    juce::String compressorUuid;
    juce::String stripUuid;
    juce::AudioProcessorGraph::Node* strip = nullptr;
    juce::AudioProcessorGraph::Node* master = nullptr;
};

/**
 * Builds the factory default mixer channel (docs/mixer.md §5.7/D3, T173a) that "+ Track ->
 * Audio Track" wires after a freshly-created Track Audio node's stereo output:
 *
 *     source 0/1 -> Parametric EQ 0/1 -> Compressor 0/1 -> Channel Strip (Stereo) -> Master (Mix)
 *
 * Both inserts are created BYPASSED (docs/mixer.md's factory-default chain: present but inert until
 * the user opts in). The Channel Strip is Stereo, shape set BEFORE the node is added to the live
 * graph (ChannelStripModule::setShape()'s own contract — adding to a live graph can prepareToPlay
 * and lock the shape).
 *
 * Channel numbering, verified against the module headers rather than assumed:
 *   - source 0/1 -> EQ 0/1 (Parametric EQ's audio pair sits on raw ch0/ch1);
 *   - EQ 0/1 -> Compressor 0/1 (same, both stereo pairs on raw ch0/ch1);
 *   - Compressor 0/1 -> Strip 0 / ChannelStripModule::kRightBase (=4) — the strip's right leg is
 *     NEVER ch1 (Source/Modules/CLAUDE.md), so this is the one place the raw channel number jumps;
 *   - Strip 0 / kRightBase -> Master MasterModule::kMixLeft / kMixRight.
 *
 * Master is spliced via spliceMasterNode() — reusing the existing singleton when this isn't the
 * first channel — AFTER the source->EQ->Compressor->Strip chain is wired but BEFORE the
 * Strip->Master edges are added: spliceMasterNode() re-routes whatever already feeds the audio
 * output, and nothing of this channel's own should be among that yet.
 *
 * The Strip->Master edge is a PLAIN graph edge, never boxed behind a macro port, on purpose:
 * spliceMasterNode()/ensureMasterNode() (Source/Mixer/MasterSplice.h) classify a re-routed feed as
 * Mix vs Direct by checking whether the connection's SOURCE NODE is itself a ChannelStripModule. A
 * MacroOutlet sitting between the strip and Master would make the source node a MacroOutlet instead,
 * defeating that check — which the later "Create channels" subtask depends on. (MainComponent::
 * addAudioTrack keeps Master OUTSIDE the macro it boxes this channel's other nodes into, for exactly
 * this reason — see its own comment.)
 *
 * NO UNDO — a plain graph mutation for a caller already inside its own undo transaction
 * (MainComponent::addAudioTrack, via AppUndoManager::recordGraphTimelineAndMacroChange). Core cannot
 * depend on AppUndoManager or GraphEditor (AppUI-only), so this function touches only the graph.
 *
 * Lays EQ/Compressor/Strip out left-to-right starting right of `source` (reading its "x"/"y"
 * properties). Core cannot depend on Source/UI/LayoutUtil::kSingleWidth (AppUI-only), so the
 * horizontal step is a plain constant kept in sync with it by convention, not a shared symbol.
 * `masterPosition` positions a newly-spliced Master; ignored when Master already exists.
 *
 * @param source must already be live in `graph`, with a stereo output on raw ch0/ch1 (Track Audio,
 *               today's only caller) and an assigned "uuid"/"x"/"y" set of properties.
 * @return the created chain's uuids/nodes. `stripUuid` (and every uuid before it, in order) is empty
 *         when a step failed to create its node — the caller should treat that as "nothing usable was
 *         built" the same way any other `graph.addNode()` failure is handled elsewhere.
 */
DefaultChannel buildDefaultAudioChannel(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node& source,
                                        juce::Point<int> masterPosition);

} // namespace synth
