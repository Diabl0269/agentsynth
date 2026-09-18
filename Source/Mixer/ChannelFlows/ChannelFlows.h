#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth {

class MacroSet;
struct Macro;

/** The nodes buildDefaultAudioChannel() created (or, for `master`, spliced/reused) — every field
 *  empty/null when the build failed partway (out-of-memory-class failures only; see
 *  buildDefaultAudioChannel below). `strip`/`master` are raw observing pointers, valid exactly as
 *  long as the graph node itself is (same lifetime rule every other `Node*` in this codebase
 *  follows). */
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
 *  is used only when Master is newly spliced (ignored when it already exists — see
 *  ChannelFlowsDefaultChannel.cpp's buildDefaultAudioChannel comment for the splice-vs-reuse
 *  ordering). */
struct DefaultChannelLayout {
    juce::Point<int> eq;
    juce::Point<int> compressor;
    juce::Point<int> strip;
    juce::Point<int> master;
};

/**
 * Builds the factory default mixer channel (docs/mixer.md §5.7/D3, T173a): EQ (bypassed) ->
 * Compressor (bypassed) -> Channel Strip (Stereo) -> Master (Mix), after `source`. See
 * ChannelFlowsDefaultChannel.cpp for the channel-numbering derivation, the Master-splice ordering,
 * and why Strip->Master is a plain edge.
 *
 * NO UNDO — a plain graph mutation; the caller must already be inside its own undo transaction.
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
 * When `instrument` is in poly mode, wires its raw ch0-7 into a new Voice Mixer and returns it —
 * pass its ch0/ch1 as `source` to buildDefaultAudioChannel() instead of `instrument` directly. See
 * ChannelFlowsDefaultChannel.cpp for why and for the FRO46 poly-envelope exception.
 *
 * @return nullptr, `uuidOut` untouched, when `instrument` has no "poly" parameter, it's off, or a
 *         factory/addNode failure occurred — the caller's existing `source`/`sourceRightChannel`
 *         stay valid as-is.
 */
juce::AudioProcessorGraph::Node* addVoiceMixerForPolyInstrument(juce::AudioProcessorGraph& graph,
                                                                juce::AudioProcessorGraph::Node& instrument,
                                                                juce::Point<int> position, juce::String& uuidOut);

/** True when `processor` declares a "poly" AudioParameterBool and it's currently on — the same
 *  check addVoiceMixerForPolyInstrument uses internally, exposed so a caller can decide which of
 *  addVoiceMixerForPolyInstrument / addPolyEnvelopeAndVCAForInstrument / addEnvelopeAndVCAForRawInstrument
 *  applies BEFORE calling any of them. */
bool isProcessorPoly(juce::AudioProcessor* processor);

/** Sets `processor`'s "poly" AudioParameterBool if it declares one (no-op otherwise) — the write
 *  counterpart to isProcessorPoly, for a caller that wants an instrument it just created to start
 *  poly before running the rest of addInstrumentTrack's poly-aware wiring. */
void setProcessorPoly(juce::AudioProcessor* processor, bool poly);

/** The nodes addEnvelopeAndVCAForRawInstrument() created; `vca` is null (both uuids empty) on a
 *  partial factory/addNode failure — same "nothing usable was built" contract as DefaultChannel. */
struct EnvelopeAndVCA {
    juce::String adsrUuid;
    juce::String vcaUuid;
    juce::AudioProcessorGraph::Node* vca = nullptr;
};

/**
 * Inserts an ADSR + VCA envelope stage, gated by the track's own MIDI, ahead of the rest of the
 * default chain (Oscillator/Wavetable have no envelope of their own otherwise). See
 * ChannelFlowsDefaultChannel.cpp for why, the wiring, and the FRO46 poly counterpart.
 *
 * NO UNDO — same contract as buildDefaultAudioChannel/addVoiceMixerForPolyInstrument: a plain graph
 * mutation for a caller already inside its own undo transaction.
 *
 * @param trackIn the track's Track In node (already live, feeding `chainSource`'s underlying
 *                instrument via MIDI) — its MIDI output is fanned to the new ADSR too.
 * @param chainSource the node the caller would otherwise pass to buildDefaultAudioChannel as
 *                `source` (the raw instrument — never a Voice Mixer's sum any more, see above)
 *                — becomes the VCA's audio input instead.
 * @param chainSourceRightChannel the raw channel carrying `chainSource`'s right leg (same meaning as
 *                buildDefaultAudioChannel's `sourceRightChannel`).
 * @return the created nodes' uuids and the VCA node itself — pass the VCA as the new `chainSource`
 *         (with `sourceRightChannel` = VCAModule::kRightBase) to buildDefaultAudioChannel. `vca` is
 *         null, both uuids untouched/empty, on a partial factory/addNode failure.
 */
EnvelopeAndVCA addEnvelopeAndVCAForRawInstrument(juce::AudioProcessorGraph& graph,
                                                 juce::AudioProcessorGraph::Node& trackIn,
                                                 juce::AudioProcessorGraph::Node& chainSource,
                                                 int chainSourceRightChannel, juce::Point<int> adsrPosition,
                                                 juce::Point<int> vcaPosition);

/** The nodes addPolyEnvelopeAndVCAForInstrument() created; `vca` is null (every uuid empty) on a
 *  partial factory/addNode failure — same "nothing usable was built" contract as EnvelopeAndVCA. */
struct PolyEnvelopeAndVCA {
    juce::String polyMidiUuid;
    juce::String adsrUuid;
    juce::String vcaUuid;
    juce::AudioProcessorGraph::Node* vca = nullptr;
};

/**
 * The poly counterpart to addEnvelopeAndVCAForRawInstrument: wires a Poly MIDI node, ADSR and VCA
 * for true per-voice envelopes. See ChannelFlowsDefaultChannel.cpp for why and the wiring. Only call
 * this when the instrument's own "poly" parameter is already on (Oscillator/Wavetable only) — for
 * every other case, call addVoiceMixerForPolyInstrument + addEnvelopeAndVCAForRawInstrument instead,
 * never both paths for the same instrument.
 *
 * NO UNDO — same contract as the other ChannelFlows builders.
 *
 * @param trackIn the track's Track In node — its MIDI output is fanned to the new Poly MIDI node.
 * @param instrument the poly Oscillator/Wavetable node (caller has already confirmed its "poly"
 *                param is on).
 * @return the created nodes' uuids and the VCA node itself — pass the VCA as the new `chainSource`
 *         (with `sourceRightChannel` = 1, the legacy ch0/ch1 duplicate — see the limitation note in
 *         ChannelFlowsDefaultChannel.cpp) to buildDefaultAudioChannel. `vca` is null, every uuid
 *         empty, on a partial factory/addNode failure.
 */
PolyEnvelopeAndVCA addPolyEnvelopeAndVCAForInstrument(juce::AudioProcessorGraph& graph,
                                                      juce::AudioProcessorGraph::Node& trackIn,
                                                      juce::AudioProcessorGraph::Node& instrument,
                                                      juce::Point<int> polyMidiPosition, juce::Point<int> adsrPosition,
                                                      juce::Point<int> vcaPosition);

/**
 * BFS forward from `start` along every signal edge to find every point where `start`'s own signal
 * path reaches the output without already passing through a `ChannelStripModule`; each is returned
 * as the `Connection` that crosses it (buildChannelForFeeds below rebuilds a channel from the
 * results). See ChannelFlowsAutoChannel.cpp for the traversal rules.
 *
 * NO UNDO, NO GRAPH MUTATION — a pure query. Safe to call on a node with nothing downstream yet
 * (empty result — nothing to auto-channel).
 */
std::vector<juce::AudioProcessorGraph::Connection> findUnchanneledOutputFeeds(juce::AudioProcessorGraph& graph,
                                                                              juce::AudioProcessorGraph::NodeID start);

/**
 * Builds a channel — the same EQ (bypassed) -> Compressor (bypassed) -> Channel Strip -> Master
 * (Mix) chain and ordering as buildDefaultAudioChannel documents (shared internal builder) — from
 * `exits` (as returned by findUnchanneledOutputFeeds), instead of from one fixed stereo-pair source.
 * See ChannelFlowsAutoChannel.cpp for how `exits` is classified and rewired.
 *
 * NO UNDO — same contract as buildDefaultAudioChannel: a plain graph mutation for a caller already
 * inside its own undo transaction.
 *
 * @param exits must be non-empty (the caller checks findUnchanneledOutputFeeds's result first) and
 *              every connection in it must still be live in `graph`.
 * @param layout canvas positions for EQ/Compressor/Strip, and for Master if this call is the one
 *               that splices it — same contract as DefaultChannelLayout above.
 * @return the created chain's uuids/nodes, empty-string/null on a partial factory/addNode failure
 *         — same contract as buildDefaultAudioChannel.
 */
DefaultChannel buildChannelForFeeds(juce::AudioProcessorGraph& graph,
                                    const std::vector<juce::AudioProcessorGraph::Connection>& exits,
                                    const DefaultChannelLayout& layout);

/**
 * An EMPTY group/send bus — the same bypassed EQ -> bypassed Compressor -> Channel Strip (Stereo)
 * -> Master (Mix) chain every other channel gets, with nothing feeding the EQ yet. See
 * ChannelFlowsDefaultChannel.cpp for why there is no separate bus node type.
 *
 * NO UNDO, NO MACROS — same contract as buildDefaultAudioChannel; the caller boxes the returned
 * uuids into a macro inside its own transaction.
 */
DefaultChannel buildBusChannel(juce::AudioProcessorGraph& graph, const DefaultChannelLayout& layout);

// ---- FRO25 (P9-3d, docs/mixer.md §5.8): "Make channel" ------------------------------------------

/** True for a track's own source node — a Track In (ModuleType::TimelineMidiSource) or Track Audio
 *  (ModuleType::TimelineAudioSource). See ChannelFlowsMakeChannel.cpp for why. */
bool isTrackSourceNode(const juce::AudioProcessor* processor);

/**
 * What "Make channel" would do for the chain starting at `source`, read off the live graph — a pure
 * query (NO GRAPH MUTATION, NO UNDO), so a menu can enable/disable itself from `needsChannel` and a
 * click can report `refusal` without touching anything. See ChannelFlowsMakeChannel.cpp for how the
 * own/shared/side-input regions and `exits`/`stripCrossings`/`buses` are derived.
 */
struct MakeChannelPlan {
    /** False (menu disabled, click a no-op) when the own region already contains a Channel Strip,
     *  when nothing `source` reaches ever lands on an output, or when there is nothing to channel. */
    bool needsChannel = false;
    /** Non-empty (and nothing may be built) when a node that would move is already in a macro (flat
     *  model — MacroSet::findByMember), or when the own region feeds shared modules from more than
     *  one point with no output of its own. */
    juce::String refusal;
    std::vector<juce::AudioProcessorGraph::NodeID> members; // own region + absorbed side inputs
    std::vector<juce::AudioProcessorGraph::Connection> exits;
    std::vector<juce::AudioProcessorGraph::Connection> stripCrossings;
    struct Bus {
        juce::AudioProcessorGraph::NodeID head;
        std::vector<juce::AudioProcessorGraph::NodeID> members; // shared nodes + their side inputs
    };
    std::vector<Bus> buses;
};

MakeChannelPlan planMakeChannel(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID source,
                                const MacroSet& macros);

/** Where buildMakeChannel places new cards: called with the node a new card row should start to
 *  the right of. Core cannot size UI cards (see DefaultChannelLayout), so the AppUI caller does. */
using ChannelLayoutFn = std::function<DefaultChannelLayout(juce::AudioProcessorGraph::Node& rightOf)>;

/** The nodes buildMakeChannel built, as macro member lists ready for the caller's boxing pass.
 *  `memberUuids` is empty when this track got no strip of its own (a MIDI-only merge: the shared
 *  instrument's bus is the one channel). Each list holds pre-existing members first, then any
 *  Voice Mixer, then EQ, Compressor, Strip — Master is never a member. */
struct MadeChannel {
    DefaultChannel channel;
    std::vector<juce::String> memberUuids;
    struct Bus {
        DefaultChannel channel;
        juce::String headUuid;
        std::vector<juce::String> memberUuids;
    };
    std::vector<Bus> buses;
};

/**
 * Executes `plan` (planMakeChannel's result, computed against this same graph state). See
 * ChannelFlowsMakeChannel.cpp for the wiring and the poly-feed handling.
 *
 * Assigns every member a uuid (mirrored into the processor). NO UNDO, NO MACROS — a plain graph
 * mutation for a caller already inside its own undo transaction, which then boxes the returned
 * member lists. No-op (empty result) when `plan` is refused or has nothing to do.
 */
MadeChannel buildMakeChannel(juce::AudioProcessorGraph& graph, const MakeChannelPlan& plan,
                             const ChannelLayoutFn& layoutRightOf);

/**
 * The chain source "Make channel" acts on for a canvas selection (or one right-clicked module):
 * the one selected track source; else the one track source upstream of the selection; else the one
 * root (a node with no signal predecessor) upstream of it. Invalid NodeID when none, or when more
 * than one candidate makes the choice ambiguous. Upstream walks use planMakeChannel's signal-edge
 * rule (no attenuverters, no ModCV pins). Pure query.
 */
juce::AudioProcessorGraph::NodeID resolveChannelSource(juce::AudioProcessorGraph& graph,
                                                       const std::vector<juce::AudioProcessorGraph::NodeID>& nodes);

// ---- FRO11 (P9-5, docs/mixer.md §5.6): the signal-edge rule, shared -------------------------------
//
// planMakeChannel's own "what counts as signal" test (never an attenuverter's hidden modulation
// leg, never an audio edge landing on a PortRole::ModCV pin), promoted out of
// ChannelFlowsMakeChannel.cpp's anonymous namespace so the mixer's insert-list query
// (Source/Mixer/MixerModel/MixerModelInserts.cpp) can walk a chain "in signal order" with the
// identical rule instead of re-implementing the ModCV/attenuverter exclusion a second time.

/** True when `c` is a signal edge by the rule above: every MIDI edge; every audio edge that
 *  neither touches an AttenuverterModule nor lands (after following any macro ports) on a
 *  PortRole::ModCV input. `connections` is the full connection list the caller already has (this
 *  never re-fetches it), since callers walking a whole chain call this once per candidate edge. */
bool isSignalEdge(juce::AudioProcessorGraph& graph,
                  const std::vector<juce::AudioProcessorGraph::Connection>& connections,
                  const juce::AudioProcessorGraph::Connection& c);

// ---- FRO13 (P9-7, docs/mixer.md §5.7): track presets ---------------------------------------------

/**
 * The outside-macro modules that feed `channelMacroId`'s members through a port (or a raw
 * un-ported jack) — the founder's "saving a track also captures a shared LFO" requirement. See
 * ChannelFlowsTrackPreset.cpp for the upstream-walk algorithm and its stop rules.
 *
 * Pure query, NO GRAPH MUTATION. Empty when `channelMacroId` doesn't resolve in `macros` or has
 * nothing feeding it from outside.
 */
std::vector<juce::AudioProcessorGraph::NodeID>
collectOutsideModulatorsForTrackPreset(juce::AudioProcessorGraph& graph, const MacroSet& macros,
                                       const juce::String& channelMacroId);

/** True when `macro` boxes a ChannelStripModule, i.e. it is a mixer channel rather than an
 *  ordinary group — gates the channel macro's own "Save track as preset.../Set as default" menu
 *  items (GraphEditorMacroPrompts.cpp::buildMacroMenu) and the outside-modulator walk's own
 *  "stop at another CHANNEL's strip, but not at a plain FX group" rule (ChannelFlowsTrackPreset.cpp). */
bool isChannelMacro(const Macro& macro, juce::AudioProcessorGraph& graph);

// ---- FRO14 (P9-4, docs/mixer.md §5.2): which channel a track plays into, and back ---------------
//
// Both are pure signal-reach reads (no mutation, no undo, no TimelineDoc), defined in
// ChannelFlowsTrackChannelLink.cpp. They follow the same signal-edge rule as each other: never
// cross an AttenuverterModule (a hidden modulation leg), never treat an audio edge landing on a
// PortRole::ModCV input as signal.

/** Forward from a track's own source node (isTrackSourceNode) to the FIRST ChannelStripModule its
 *  signal reaches, stopping there; a branch that leaves the patch at a terminal (Audio Output,
 *  Record Tap, Master) without passing a strip just ends. An invalid NodeID means "this track
 *  reaches no channel yet" (no chip, no link). See ChannelFlowsTrackChannelLink.cpp for the
 *  two-strips edge case. */
juce::AudioProcessorGraph::NodeID findStripFedByTrackSource(juce::AudioProcessorGraph& graph,
                                                            juce::AudioProcessorGraph::NodeID trackSourceId);

/** Backward from a strip, collecting every distinct track-source node whose signal reaches it,
 *  transitively through the instrument/macro chain. Never expands PAST another ChannelStripModule
 *  reached upstream (that strip is another channel's terminus -- whatever feeds IT is not this
 *  strip's to claim).
 *
 *  `result.size() == 1` IS §5.2's link predicate ("the track is the channel's only source"), and it
 *  is also FRO55's stem-naming rule ("exactly one feeding track names the file"), computed once for
 *  both -- see ChannelFlowsTrackChannelLink.cpp's file comment. */
std::vector<juce::AudioProcessorGraph::NodeID> findTrackSourcesFeedingStrip(juce::AudioProcessorGraph& graph,
                                                                            juce::AudioProcessorGraph::NodeID stripId);

} // namespace synth
