#pragma once

// SoloAudibleSet.h -- FRO15 (P9-9, docs/mixer/sends-and-buses.md#solo-is-a-per-leg-audible-mask): which of each Channel
// Strip's output legs stay audible while the mixer solo gate is active.
//
// Core, headless, no UI and no AudioEngine dependency (Source/Mixer/CLAUDE.md's "Core, no-UI-dep"
// discipline, same as MixerModel and ChannelFlows): a pure read off the live graph, recomputed on
// the message thread inside AudioEngine::refreshSoloGate -- which every graph change already
// reaches via publishTimeline -- and published to each strip as one atomic mask.
//
// WHY A PER-LEG MASK AND NOT A SINGLE FLAG. Sends make "is this strip soloed?" the wrong question.
// Soloing a reverb bus under a whole-strip flag gives you the source's dry main output PLUS the
// source through the bus, which is not what "solo the reverb" means; and a group bus only works at
// all if its sources' MAIN legs stay open while it is soloed. Both fall out of one rule applied per
// leg rather than per strip.

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

namespace synth {

/**
 * The audible-leg mask for every ChannelStripModule in `graph`, keyed by node id. Bit 0
 * (ChannelStripModule::kMainLegBit) is the main stereo pair; bit 1+k
 * (ChannelStripModule::sendLegBit(k)) is send slot k. A set bit means "this leg may be written";
 * a clear bit means the strip silences it while the gate is active.
 *
 * The rule: a strip's leg is audible iff (a) the strip is itself soloed, or (b) the strip is
 * downstream of a soloed strip, or (c) that leg lies on a signal path that reaches a strip
 * satisfying (a) or (b). Everything else is silenced.
 *
 *   1. S = the soloed strips. With S empty every mask is ~0u -- byte-for-byte today's "nothing is
 *      soloed, nothing is gated" behaviour, and the map is still complete so a caller can publish
 *      unconditionally.
 *   2. D = S plus every strip reachable DOWNSTREAM of a member of S along signal edges, expanding
 *      THROUGH strips -- so soloing a source keeps the buses it feeds audible, transitively.
 *   3. Every strip in D gets ~0u: soloing a bus must not gate the bus's own sends.
 *   4. Every other strip is decided leg by leg: a forward walk from that leg's raw output channel,
 *      stopping at the first strip it meets (never expanding past it -- the same rule
 *      findStripFedByTrackSource uses) and at the terminals (Audio Output, Rec Tap, Master). The
 *      bit is set iff the walk lands on a CONTRIBUTING strip (below). The left and right legs of a
 *      pair are OR'd, since they are silenced together.
 *   5. "Contributing" is D grown to a FIXED POINT: a strip with any open leg feeds the soloed path,
 *      so a strip whose leg reaches IT feeds the path too. Step 4 alone stops at the first strip and
 *      would therefore only ever answer "does this leg reach D in one hop?", which silences every
 *      source sitting behind a chain of buses (source -> inner bus -> soloed outer bus) -- rule (c)
 *      above says "a signal path reaching", not "an edge landing on". Feeding the path is not the
 *      same as being in D: a contributing strip still gets a per-leg mask, not ~0u, so only the legs
 *      that actually reach the path open.
 *
 * Every walk uses synth::isSignalEdge, the one shared signal-edge rule, and carries a visited-set
 * cycle guard.
 *
 * DOCUMENTED LIMITATION. The gate is per-LEG, not per-EDGE: a main leg that feeds both Master and a
 * soloed bus stays open, so that strip's dry signal is still heard alongside the bus. Splitting it
 * would need a delay-compensated per-edge mute node -- out of scope (docs/mixer/sends-and-buses.md).
 *
 * Pure query: no mutation, no undo, safe to call as often as a caller likes (a handful of strips
 * and a bounded walk each, never per-block).
 */
std::map<juce::AudioProcessorGraph::NodeID, juce::uint32> computeSoloAudibleLegs(juce::AudioProcessorGraph& graph);

} // namespace synth
