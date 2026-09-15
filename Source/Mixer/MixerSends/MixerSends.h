#pragma once

// MixerSends.h -- FRO15 (P9-9, docs/mixer.md §5.15): the Core flows behind a strip's send slots.
//
// Headless, no UI dependency (Source/Mixer/CLAUDE.md), and -- like ChannelFlows and MixerModel's
// insert splices -- NO UNDO OF THEIR OWN: each is a plain graph mutation for a caller already
// inside one AppUndoManager::recordGraphTimelineAndMacroChange transaction.
//
// A send is a strip-owned OUTPUT leg (ChannelStripModule's class comment), so "add a send" is two
// things at once: activate a slot on the source strip, so its jacks exist, and wire that slot's raw
// stereo pair into the target strip's input. The TARGET IS NEVER STORED -- node ids are reassigned
// on every rebuild-from-JSON, so a stored id goes stale on undo; "which bus does slot k feed?" is
// answered by walking the graph from slot k's own output channel (findSendTarget below).

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth {

// ---- Buses -------------------------------------------------------------------------------------
//
// A bus is an ordinary ChannelStripModule whose inputs are other strips' outputs (§5.15 D1) -- there
// is no bus node type, so "is this a bus?" is a query, not a class check.

/** The strips feeding `stripId`, ascending NodeID: a backward walk along signal edges that stops AT
 *  the first strip it meets (that strip IS a source, not something to expand through). Pure query. */
std::vector<juce::AudioProcessorGraph::NodeID> findStripsFeedingStrip(juce::AudioProcessorGraph& graph,
                                                                      juce::AudioProcessorGraph::NodeID stripId);

/** True when `stripId` is a group/send bus: it carries the trusted "isBus" flag (which is what a
 *  freshly added, still-unfed bus has to go on -- it has no predecessors yet), OR at least one of
 *  its signal predecessors is another strip (the structural fallback, so a patch built before the
 *  flag existed and a hand-wired group bus both still classify). */
bool isBusStrip(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID stripId);

/** "Bus N", N being the 1-based position of `stripId` among the graph's bus strips in ascending
 *  NodeID -- the name a bus column and a bus's stem file fall back to, a bus having no feeding track
 *  to take a name from. */
juce::String busFallbackName(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID stripId);

// ---- Sends ---------------------------------------------------------------------------------------

/** The strip a send slot currently feeds: a forward walk from the slot's raw LEFT output channel to
 *  the FIRST ChannelStripModule it reaches (the same "stop at the first strip" rule
 *  findStripFedByTrackSource uses), so a module the user inserted on the send path on the canvas
 *  still resolves to the bus behind it. Invalid NodeID when the slot is inactive, unconnected, or
 *  its cable leaves the patch without passing a strip. Pure query. */
juce::AudioProcessorGraph::NodeID findSendTarget(juce::AudioProcessorGraph& graph,
                                                 juce::AudioProcessorGraph::NodeID sourceStrip, int slot);

/** Every strip a new (or retargeted) send from `sourceStrip` may legally feed: every OTHER
 *  ChannelStripModule in the graph, in ascending NodeID, minus any whose own signal already reaches
 *  `sourceStrip` -- those would close a feedback loop. (AudioProcessorGraph refusing the connection
 *  is the backstop; this is the menu's own guard, so a cyclic target is never offered in the first
 *  place.) Pure query. */
std::vector<juce::AudioProcessorGraph::NodeID> enumerateSendTargets(juce::AudioProcessorGraph& graph,
                                                                    juce::AudioProcessorGraph::NodeID sourceStrip);

/** Activates `sourceStrip`'s lowest free slot (post-fader, unity -- see ChannelStripModule::addSend)
 *  and wires its stereo pair into `target`'s ch0/kRightBase. Returns the slot index, or -1 when
 *  either node is not a strip, all kMaxSends slots are in use, `target` would close a cycle, or the
 *  graph refuses the connection -- in every failing case NOTHING is changed, so the caller can
 *  abandon its undo transaction rather than record a no-op step. */
int addSend(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip,
            juce::AudioProcessorGraph::NodeID target);

/** Clears slot `slot`'s cables and its active bit. Higher slots keep their own raw channels, so
 *  nothing else is re-wired (only the VISIBLE jack indices renumber). False when `sourceStrip` is
 *  not a strip or the slot was not active. */
bool removeSend(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slot);

/** Repoints an active slot at a different target: drops its current cables and wires the new pair.
 *  False (and nothing changed) on the same refusals as addSend. */
bool retargetSend(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slot,
                  juce::AudioProcessorGraph::NodeID target);

} // namespace synth
