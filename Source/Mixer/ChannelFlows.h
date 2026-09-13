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
 * poly branch is CV-gate-only (it never reads the MIDI note-on/off fallback `midiGateHeld`, which
 * exists solely in its non-poly branch), so a poly ADSR fed only Track In's MIDI would output a
 * permanent zero envelope -> total silence, not degraded polyphony. Non-poly composes correctly
 * whether or not `chainSource` is already a Voice Mixer's poly-voice sum (addVoiceMixerForPolyInstrument
 * above) — this is deliberately called AFTER that stage, never before it, so a poly instrument's
 * per-voice audio is summed to one stereo pair before the (necessarily-mono) VCA gates it.
 *
 * ADSR's sustain (stock factory default 0.0) is overridden to 0.7 so a held note actually sustains
 * instead of plucking and decaying to silence after ~0.25s regardless of how long the key is held;
 * the release stage (stock default, unchanged) is what fixes the drone-after-note-off bug. VCA's
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
 * drives its mono branch (ADSRModule.h's `midiGateHeld`), so a poly ADSR fed only Track In's raw
 * MIDI would output a permanent zero envelope. This is the poly counterpart: instead of MIDI
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

} // namespace synth
