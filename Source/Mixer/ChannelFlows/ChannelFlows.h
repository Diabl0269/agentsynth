#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth {

class MacroSet;
struct Macro;

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
 * FRO46 (P9-3j): for a poly Oscillator/Wavetable specifically, the caller no longer calls this —
 * addPolyEnvelopeAndVCAForInstrument() below replaces it entirely (its poly VCA does its own
 * 8-voice summing, so a separate Voice Mixer stage is redundant). This function is still the right
 * one for every other poly instrument (e.g. a poly Sampler, which has no auto-wired envelope).
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
 * P9-3i (FRO43): Oscillator/Wavetable have no envelope of their own — a held or even released note
 * drones forever. Inserts an ADSR gated by the track's own MIDI (fanned alongside the existing
 * Track In -> instrument wire) driving a VCA's gain, ahead of the rest of the default chain:
 *
 *     Track In --MIDI--> ADSR --Env(ch0)--> VCA's Gain CV (ch1)
 *     chainSource L/R -----------------------> VCA Audio L/R (ch0 / VCAModule::kRightBase)
 *
 * Both nodes are forced non-poly, regardless of the instrument's own "poly" parameter: ADSRModule's
 * poly branch is CV-gate-only (it never reads the MIDI note-on/off fallback tracked via the
 * `heldNotes` bitset, which exists solely in its non-poly branch), so a poly ADSR fed only Track
 * In's MIDI would output a permanent zero envelope -> total silence, not degraded polyphony.
 * Non-poly composes correctly whether or not `chainSource` is already a Voice Mixer's poly-voice
 * sum (addVoiceMixerForPolyInstrument above) — this is deliberately called AFTER that stage,
 * never before it, so a poly instrument's per-voice audio is summed to one stereo pair before the
 * (necessarily-mono) VCA gates it.
 *
 * ADSR's sustain (stock factory default 1.0 as of FRO110; this override predates that and is kept
 * for clarity/explicitness) is set to 0.7 so a held note settles at a musical level instead of the
 * full peak; the release stage (stock default, unchanged) is what fixes the drone-after-note-off
 * bug. VCA's
 * gain (stock factory default 0.5) is overridden to 1.0 so the envelope alone governs perceived
 * level, not an extra silent 50% attenuation stacked under it.
 *
 * FRO46 (P9-3j) superseded this as the poly instrument's ONLY option: when the instrument is poly,
 * the caller now builds addPolyEnvelopeAndVCAForInstrument() below instead of this one (true
 * per-voice envelopes, no Voice Mixer). This function remains exactly as before for the non-poly
 * case — see that function's header comment for why a poly ADSR fed only raw MIDI can't work, and
 * how the poly path solves it (a Poly MIDI node supplying per-voice CV instead).
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
 * FRO46 (P9-3j): addEnvelopeAndVCAForRawInstrument's ADSR+VCA are forced non-poly because
 * ADSRModule's poly branch is CV-gate-only — it never reads the MIDI note-on/off fallback that
 * drives its mono branch (ADSRModule.h's `heldNotes` bitset), so a poly ADSR fed only Track In's
 * raw MIDI would output a permanent zero envelope. This is the poly counterpart: instead of MIDI
 * driving a mono ADSR, a Poly MIDI node (the codebase's existing per-voice MIDI-to-CV converter —
 * docs/modules.md "Poly MIDI Module") turns Track In's MIDI into per-voice pitch/gate CV, which
 * drives a genuinely poly ADSR and VCA:
 *
 *     Track In --MIDI--> Poly MIDI --Pitch(ch0-7)--> instrument's poly Pitch CV in (ch0-7)
 *                         Poly MIDI --Gate(ch8-15)--> ADSR's poly Gate CV in (ch0-7)
 *     ADSR poly Env (ch0-7) --> VCA's poly Gain CV in (ch8-15, VCAModule::kPolyCVBase)
 *     instrument's poly Audio L (ch0-7) --> VCA's poly Audio L in (ch0-7)
 *
 * VCA's own poly branch already sums all 8 gated voices to a stereo-shaped pair (ch0 = left sum,
 * ch1 = the module's legacy duplicate of it — VCAModule.h), so unlike the non-poly path, no
 * separate Voice Mixer is inserted; this REPLACES addVoiceMixerForPolyInstrument entirely for the
 * Oscillator/Wavetable case, it does not compose after it. The instrument's R-octet is deliberately
 * NOT wired into VCA's own Audio R poly block (ch16-23) — same known limitation
 * addVoiceMixerForPolyInstrument's own comment documents (a poly instrument's stereo image isn't
 * preserved; downstream reads the mono ch0/ch1 duplicate). Only call this when the instrument's own
 * "poly" parameter is already on (Oscillator/Wavetable only) — for every other case, call
 * addVoiceMixerForPolyInstrument + addEnvelopeAndVCAForRawInstrument instead, never both paths for
 * the same instrument.
 *
 * The existing Track In -> instrument MIDI connection (wired unconditionally at instrument-track
 * creation) is left as-is: it's harmless in poly mode. OscillatorModule/WavetableOscillatorModule
 * only ever consult raw MIDI as a last-resort fallback for voice 0's pitch when no CV is present on
 * ch0, and Poly MIDI supplies real per-voice Hz once a note sounds — the fallback simply never
 * triggers once this is wired.
 *
 * NO UNDO — same contract as the other ChannelFlows builders.
 *
 * @param trackIn the track's Track In node — its MIDI output is fanned to the new Poly MIDI node.
 * @param instrument the poly Oscillator/Wavetable node (caller has already confirmed its "poly"
 *                param is on).
 * @return the created nodes' uuids and the VCA node itself — pass the VCA as the new `chainSource`
 *         (with `sourceRightChannel` = 1, the legacy ch0/ch1 duplicate — see the limitation note
 *         above) to buildDefaultAudioChannel. `vca` is null, every uuid empty, on a partial
 *         factory/addNode failure.
 */
PolyEnvelopeAndVCA addPolyEnvelopeAndVCAForInstrument(juce::AudioProcessorGraph& graph,
                                                      juce::AudioProcessorGraph::Node& trackIn,
                                                      juce::AudioProcessorGraph::Node& instrument,
                                                      juce::Point<int> polyMidiPosition, juce::Point<int> adsrPosition,
                                                      juce::Point<int> vcaPosition);

/**
 * T184 (P9-3c, docs/mixer.md §5.2 "main workflow"): BFS forward from `start`, following every
 * outgoing graph edge (audio AND MIDI — an `AudioProcessorGraph::Connection` is always one or the
 * other), to find every point where `start`'s own signal path reaches the output WITHOUT already
 * passing through a `ChannelStripModule`. Each such point is returned as the exact `Connection`
 * that crosses it — the caller (buildChannelForFeeds below) removes those edges and rebuilds a
 * channel from their sources.
 *
 * Traversal rules:
 *   - Never enter an `AttenuverterModule` node — `AudioEngine::addModRouting` always wraps a
 *     hidden modulation leg in one of these; its own outgoing edge is a mod-CV destination
 *     parameter, not part of `start`'s audio/MIDI signal path, and is neither traversed nor
 *     itself an exit.
 *   - Never expand PAST a `ChannelStripModule` — that branch already terminates in a channel, so
 *     nothing downstream of it is `start`'s to claim. Not an exit either (it is not one of the
 *     three terminal types below).
 *   - Never expand past a terminal: Audio Output (`juce::AudioGraphIOProcessor` named "Audio
 *     Output"), `RecordTapModule`, or `MasterModule`. An edge landing on one of these IS an exit
 *     when it lands on the right channel — Audio Output/Rec Tap ch0/ch1, or Master's
 *     `kDirectLeft`/`kDirectRight` (its ALREADY-channeled `kMixLeft`/`kMixRight` inputs are never
 *     an exit — they can only be fed by an existing strip's own output, which this BFS never
 *     reaches, having stopped at the strip).
 *   - Every other node (an instrument, an FX module, a macro port pass-through, ...) is just
 *     traversed through, exactly like any other hop in the chain.
 *
 * Cycle-safe (a visited-node set), and safe to call on a node with nothing downstream yet (empty
 * result — nothing to auto-channel). NO UNDO, NO GRAPH MUTATION — a pure query; Core cannot depend
 * on AppUndoManager/GraphEditor.
 */
std::vector<juce::AudioProcessorGraph::Connection> findUnchanneledOutputFeeds(juce::AudioProcessorGraph& graph,
                                                                              juce::AudioProcessorGraph::NodeID start);

/**
 * T184 (P9-3c): builds a channel — Parametric EQ (bypassed) -> Compressor (bypassed) -> Channel
 * Strip (Stereo) -> Master (Mix), exactly the same chain and ordering buildDefaultAudioChannel
 * documents above (shared internal builder) — from `exits` (as returned by
 * findUnchanneledOutputFeeds), instead of from one fixed stereo-pair source.
 *
 * Every edge in `exits` is REMOVED FIRST (collected, then removed, same reasoning as
 * spliceMasterNode's own splice), classified Left/Right by its DESTINATION channel (raw ch0 or
 * `MasterModule::kDirectLeft` -> Left; ch1 or `MasterModule::kDirectRight` -> Right), and its
 * SOURCE re-wired into the new EQ's matching input instead — so every exit's original source now
 * feeds the new channel, and `AudioProcessorGraph`'s many-to-one summing at the EQ's input means
 * more than one exit landing on the same side (e.g. two separate Direct feeds) still sums exactly
 * as it did before, just one hop later. The sound does not change.
 *
 * NO UNDO — same contract as buildDefaultAudioChannel: a plain graph mutation for a caller already
 * inside its own undo transaction (GraphEditor::endConnectionDrag's T184 hook).
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
 * FRO15 (P9-9, docs/mixer.md §5.15): an EMPTY group/send bus — the same bypassed EQ -> bypassed
 * Compressor -> Channel Strip (Stereo) -> Master (Mix) chain every other channel gets, with nothing
 * feeding the EQ yet, and the strip marked isBus() so the mixer gives its column the BUS badge.
 * This lives here rather than in MixerSends because it IS that shared chain builder with an empty
 * feed list — a bus is an ordinary channel whose inputs happen to be other strips' outputs (D1), so
 * there is deliberately no separate bus node type and no second chain builder.
 *
 * NO UNDO, NO MACROS — same contract as buildDefaultAudioChannel; the caller boxes the returned
 * uuids into a macro inside its own transaction.
 */
DefaultChannel buildBusChannel(juce::AudioProcessorGraph& graph, const DefaultChannelLayout& layout);

// ---- FRO25 (P9-3d, docs/mixer.md §5.8): "Make channel" ------------------------------------------

/** True for a track's own source node — a Track In (ModuleType::TimelineMidiSource) or Track Audio
 *  (ModuleType::TimelineAudioSource). "Which modules does this track use" is answered against every
 *  OTHER such node in the graph, never against TimelineDoc (Core has no reference to it). */
bool isTrackSourceNode(const juce::AudioProcessor* processor);

/**
 * What "Make channel" would do for the chain starting at `source`, read off the live graph — a pure
 * query (NO GRAPH MUTATION, NO UNDO), so a menu can enable/disable itself from `needsChannel` and a
 * click can report `refusal` without touching anything.
 *
 * Signal reach: a forward walk from a node along every MIDI edge and every audio edge whose
 * destination pin is not a modulation input (PortRole::ModCV) — so a plain CV cable into another
 * track's cutoff, like an AudioEngine::addModRouting leg (an AttenuverterModule, never entered),
 * never makes two chains "the same chain". The walk passes THROUGH Channel Strips and macro port
 * nodes and stops at the terminals (Audio Output, Record Tap, Master).
 *
 *   - own region: nodes `source` reaches that no OTHER track source reaches (macro port nodes are
 *     walked through but never own anything). These are the modules "used only by this track".
 *   - shared region: nodes `source` reaches that another track also reaches — never moved into this
 *     track's channel. Where this track's own region feeds into it is a MERGE: each such merge head
 *     that still reaches the output without a strip becomes its OWN bus channel (`buses`), holding
 *     the shared nodes downstream of it.
 *   - side inputs: a node no track source reaches (an LFO, a free oscillator) whose every consumer
 *     (looking through modulation attenuverters) is already a member is absorbed into the member
 *     set, to a fixpoint. A side input with a consumer anywhere else — the shared-LFO case — stays
 *     outside, and the caller's auto-port pass fronts its cable with a macro port.
 *
 * The new strip takes over `exits` (edges from the own region onto the output) and `stripCrossings`
 * (audio edges from the own region into shared modules carrying the same signal the exits carry,
 * or — with no exits at all — every audio edge into the shared region, provided each side feeds one
 * consistent signal; otherwise `refusal`). An audio edge into the shared region that carries a
 * DIFFERENT signal from the exits stays a pre-strip send. Channel Strip, bypassed EQ/Compressor
 * and Master are unity at their defaults, so the rebuilt graph renders identically.
 *
 * `needsChannel` is false (menu disabled, click a no-op) when the own region already contains a
 * Channel Strip, when nothing `source` reaches ever lands on an output, or when there is nothing
 * to channel. `refusal` is non-empty (and nothing may be built) when a node that would move is
 * already in a macro (flat model — MacroSet::findByMember), or when the own region feeds shared
 * modules from more than one point with no output of its own.
 */
struct MakeChannelPlan {
    bool needsChannel = false;
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
 * Executes `plan` (planMakeChannel's result, computed against this same graph state): rebuilds the
 * own exits/crossings through EQ (bypassed) -> Compressor (bypassed) -> Channel Strip, whose output
 * goes to Master's Mix for the exits (Master spliced exactly as buildChannelForFeeds does) and to
 * the original shared input pins for the crossings — plain edges, never macro ports, see
 * buildDefaultAudioChannel's own comment. Then builds each bus with buildChannelForFeeds' chain.
 *
 * A feed from a poly module's poly jack (isProcessorPoly, span > 1 — so a poly VCA, which self-sums
 * to one channel, never qualifies) gets addVoiceMixerForPolyInstrument ahead of the strip instead
 * (docs/mixer.md §5.4/§5.8). The one intended sound change: the channel then carries every voice,
 * where a bare poly jack wired to a mono input carried voice 0 only.
 *
 * Assigns every member a uuid via AIStateMapper::ensureNodeUuid (mirrored into the processor).
 * NO UNDO, NO MACROS — a plain graph mutation for a caller already inside its own undo transaction,
 * which then boxes the returned member lists. No-op (empty result) when `plan` is refused or has
 * nothing to do.
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
 * un-ported jack) — the founder's "saving a track also captures a shared LFO" requirement.
 *
 * Seeds from every connection landing on a member of `channelMacroId` whose SOURCE is not itself a
 * member (this scans every member's incoming edges rather than only the macro's own ports, which
 * subsumes the ported case for free and also catches a boundary crossing with T148 auto-porting
 * off). From each seed, walks further upstream along every incoming edge to a fixpoint
 * (visited-set, cycle-safe): an Attenuverter on the path IS entered (unlike planMakeChannel's
 * FORWARD walk, which never enters one — here the modulator behind it is invisible otherwise) but
 * never itself added to the result, since it is rebuilt from "modulations" on import, same as
 * every other snippet/macro-port case. The walk stops at, and never adds, another channel's own
 * Channel Strip, or any member of a DIFFERENT macro that is itself a channel (isChannelMacro) — a
 * node reached only through such a boundary is that other channel's business, not this preset's; a
 * node in some third, non-channel macro (a plain FX group) is captured normally.
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
 *  "stop at another CHANNEL's strip, but not at a plain FX group" rule above. */
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
 *  reaches no channel yet" (no chip, no link).
 *
 *  A track whose signal fans out and reaches TWO distinct strips gets whichever the BFS visits
 *  first -- arbitrary but deterministic. §5.2's link rule is defined channel-side ("is THIS track
 *  the channel's only source"), so a track feeding two channels at once is out of scope. */
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
