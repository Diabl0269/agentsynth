#pragma once

// MixerSends.h (docs/mixer/sends-and-buses.md): the Core flows behind a strip's send slots.
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
#include <utility>
#include <vector>

namespace synth {

class ConnectionIndex;
class MacroSet;

// ---- Buses -------------------------------------------------------------------------------------
//
// A bus is an ordinary ChannelStripModule whose inputs are other strips' outputs
// (docs/mixer/sends-and-buses.md#a-bus-is-a-channel-strip) -- there is no bus node type, so "is this a bus?" is a
// query, not a class check.

/** The strips feeding `stripId`, ascending NodeID: a backward walk along signal edges that stops AT
 *  the first strip it meets (that strip IS a source, not something to expand through). Pure query. */
std::vector<juce::AudioProcessorGraph::NodeID> findStripsFeedingStrip(juce::AudioProcessorGraph& graph,
                                                                      juce::AudioProcessorGraph::NodeID stripId);
/** The same over an index built once for the caller's pass (the graph must not change while it is used). */
std::vector<juce::AudioProcessorGraph::NodeID> findStripsFeedingStrip(juce::AudioProcessorGraph& graph,
                                                                      const ConnectionIndex& cables,
                                                                      juce::AudioProcessorGraph::NodeID stripId);

/** True when `stripId` is a group/send bus: its "isBus" flag, or strips among its signal predecessors
 *  and no track source among them (a track channel that receives a send stays a track channel). */
bool isBusStrip(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID stripId);

/** "Bus N", N = `stripId`'s 1-based position among the graph's bus strips in ascending NodeID. */
juce::String busFallbackName(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID stripId);

// ---- Sends ---------------------------------------------------------------------------------------

/** What a send slot feeds: a strip's main input, or (`key`) a module's Key input. */
struct SendTarget {
    juce::AudioProcessorGraph::NodeID node;
    bool key = false;
    bool isValid() const noexcept { return node != juce::AudioProcessorGraph::NodeID{}; }
    bool operator==(const SendTarget& other) const noexcept { return node == other.node && key == other.key; }
    bool operator!=(const SendTarget& other) const noexcept { return !(*this == other); }
};

/** The strip a send slot currently feeds; invalid when inactive, unwired, or a Key send. Pure query. */
juce::AudioProcessorGraph::NodeID findSendTarget(juce::AudioProcessorGraph& graph,
                                                 juce::AudioProcessorGraph::NodeID sourceStrip, int slot);

/** Strip or Key target of slot `slot` (findSendTarget is its strip-only view). Invalid when unwired. */
SendTarget resolveSendTarget(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slot);
/** The same over an index built once for the caller's pass (the graph must not change while it is used). */
SendTarget resolveSendTarget(juce::AudioProcessorGraph& graph, const ConnectionIndex& cables,
                             juce::AudioProcessorGraph::NodeID sourceStrip, int slot);

/** The first strip `module`'s own output reaches -- the channel a Key target sits on. Invalid if none. */
juce::AudioProcessorGraph::NodeID findKeyTargetChannel(juce::AudioProcessorGraph& graph,
                                                       juce::AudioProcessorGraph::NodeID module);

/** "Key: <module title> on <channelName>", or "Key: <module title>" when `channelName` is empty. */
juce::String keySendTargetName(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID module,
                               const juce::String& channelName);

/** Display name of a strip send target ("No target" when invalid). `macros` may be null. */
juce::String sendTargetName(juce::AudioProcessorGraph& graph, const MacroSet* macros,
                            juce::AudioProcessorGraph::NodeID target);
/** sendTargetName for either kind: a Key target reads "Key: Compressor 1 on <sendTargetName>". */
juce::String sendTargetName(juce::AudioProcessorGraph& graph, const MacroSet* macros, const SendTarget& target);

/** "Send to <target>" / "Send N (no target)" -- the accessible-title format MixerSendList's
 *  knob uses, shared so a lane created for the same slot always agrees with the knob that
 *  drives it. `slot` is 0-based; `macros` may be null. */
juce::String describeSendSlotLabel(juce::AudioProcessorGraph& graph, const MacroSet* macros,
                                   juce::AudioProcessorGraph::NodeID sourceStrip, int slot);

/** Every OTHER strip `sourceStrip` may legally send to (no cycle), ascending NodeID. Pure query. */
std::vector<juce::AudioProcessorGraph::NodeID> enumerateSendTargets(juce::AudioProcessorGraph& graph,
                                                                    juce::AudioProcessorGraph::NodeID sourceStrip);

/** Every module with a Key (PortRole::Sidechain) input `sourceStrip` may legally key, ascending NodeID. */
std::vector<juce::AudioProcessorGraph::NodeID> enumerateKeySendTargets(juce::AudioProcessorGraph& graph,
                                                                       juce::AudioProcessorGraph::NodeID sourceStrip);

/** Activates the lowest free slot and wires it into strip `target`. Slot index, or -1 with NOTHING changed. */
int addSend(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip,
            juce::AudioProcessorGraph::NodeID target);
/** addSend for either kind: a Key target is wired onto the module's Key L/R inputs. Same refusals. */
int addSend(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, const SendTarget& target);

/** Clears slot `slot`'s cables and its active bit (and its mute/bypass/mono bits). Higher slots keep
 *  their own raw channels, so nothing else is re-wired (only the VISIBLE jack indices renumber).
 *  False when `sourceStrip` is not a strip or the slot was not active. */
bool removeSend(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slot);

/** Mutes/unmutes an active slot, never its level parameter. False (no change) when `sourceStrip`
 *  is not a strip or `slot` is not active. */
bool setSendMuted(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slot,
                  bool muted);

/** Bypasses/restores an active slot (its audio ramps out and back in), never its level parameter. False (no
 *  change) when `sourceStrip` is not a strip or `slot` is not active. */
bool setSendBypassed(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slot,
                     bool bypassed);

/** Sums/unsums an active slot's L/R to mono. False when `sourceStrip`/`slot` don't resolve. */
bool setSendMono(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slot, bool mono);

/** Repoints an active slot at strip `target`. False, with nothing changed, on addSend's refusals. */
bool retargetSend(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slot,
                  juce::AudioProcessorGraph::NodeID target);
/** retargetSend for either kind -- strip to Key, Key to strip, or Key to another Key. */
bool retargetSend(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slot,
                  const SendTarget& target);

bool swapSends(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int slotA,
               int slotB); // Reorder: swaps cables, active/pre/mute/mono bits, level/pan values
bool moveSendRow(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID sourceStrip, int fromRow,
                 int toRow,
                 std::vector<std::pair<int, int>>* appliedSwaps = nullptr); // moves visible row fromRow to toRow

} // namespace synth
