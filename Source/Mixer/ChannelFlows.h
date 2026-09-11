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

/** Canvas positions for the four cards buildDefaultAudioChannel() may place. Core cannot size UI
 *  cards itself (no dependency on Source/UI/LayoutUtil or GraphEditor::estimateModuleSize), so the
 *  caller — which CAN size them — works out non-overlapping positions and hands them in. `master`
 *  is used only when Master is newly spliced (ignored when it already exists — see the function
 *  comment). */
struct DefaultChannelLayout {
    juce::Point<int> eq;
    juce::Point<int> compressor;
    juce::Point<int> strip;
    juce::Point<int> master;
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
 * Positions the EQ/Compressor/Strip (and, when newly spliced, Master) cards exactly where
 * `layout` says. Core cannot size UI cards itself — no dependency on Source/UI/LayoutUtil or
 * GraphEditor::estimateModuleSize (both AppUI-only) — so the caller must have already worked out
 * non-overlapping positions from the real card widths (MainComponent::addAudioTrack is today's only
 * caller; see its own comment for how it derives `layout`).
 *
 * @param source must already be live in `graph`, with its stereo pair on raw ch0 and
 *               `sourceRightChannel` (Track Audio, today's only caller, is a contiguous ch0/ch1 pair
 *               — the default), and an assigned "uuid"/"x"/"y" set of properties.
 * @param layout canvas positions for EQ/Compressor/Strip, and for Master if this call is the one
 *               that splices it (ignored otherwise — see the DefaultChannelLayout comment).
 * @param sourceRightChannel the raw channel carrying `source`'s right leg. Defaults to 1 (a
 *               contiguous stereo pair); a split-block source (T183: Oscillator/Wavetable) passes
 *               `ModuleBase::rightAudioLegChannel()` instead — Source/Modules/CLAUDE.md: "pair legs
 *               via rightAudioLegChannel(), never by assuming ch1."
 * @return the created chain's uuids/nodes. `stripUuid` (and every uuid before it, in order) is empty
 *         when a step failed to create its node — the caller should treat that as "nothing usable was
 *         built" the same way any other `graph.addNode()` failure is handled elsewhere.
 */
DefaultChannel buildDefaultAudioChannel(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node& source,
                                        const DefaultChannelLayout& layout, int sourceRightChannel = 1);

/**
 * T183 (P9-3b): when `instrument` is currently in poly mode (it has a "poly" AudioParameterBool and
 * it's on), its L-octet (raw ch0-7) can carry up to 8 simultaneous voices, which
 * buildDefaultAudioChannel() cannot accept directly — it wants one stereo pair. Creates a Voice Mixer
 * (docs/mixer.md §5.4/§5.8: any chain ending poly gets one ahead of the strip), wires `instrument`'s
 * raw ch0-7 into it, and returns it so the caller can pass ITS ch0/ch1 as `source` to
 * buildDefaultAudioChannel() instead of `instrument` directly.
 *
 * A factory-created instrument defaults to poly OFF (Oscillator/Wavetable's own `poly` parameter
 * default), so this returns nullptr on the golden "+ Track -> Instrument" path today; it exists so
 * poly instruments are handled correctly wherever they arise (a caller that explicitly turns poly on
 * before building the chain, or a future direct-poly picker), without the strip ever seeing more than
 * one stereo pair.
 *
 * @return nullptr, `uuidOut` untouched, when `instrument` has no "poly" parameter, it's off, or a
 *         factory/addNode failure occurred — the caller's existing `source`/`sourceRightChannel`
 *         stay valid as-is.
 */
juce::AudioProcessorGraph::Node* addVoiceMixerForPolyInstrument(juce::AudioProcessorGraph& graph,
                                                                juce::AudioProcessorGraph::Node& instrument,
                                                                juce::Point<int> position, juce::String& uuidOut);

} // namespace synth
