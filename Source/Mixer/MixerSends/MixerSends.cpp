// Concern: the send slot flows (add / remove / retarget / resolve target / offer
// legal targets). See MixerSends.h for the contract; docs/mixer/sends-and-buses.md for the design.

#include "MixerSends.h"

#include "MacroSet.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/RecordTapModule.h"
#include <algorithm>
#include <set>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;

juce::AudioProcessor* processorAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? node->getProcessor() : nullptr;
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    return dynamic_cast<ChannelStripModule*>(processorAt(graph, id));
}

ModuleBase* moduleAt(juce::AudioProcessorGraph& graph, NodeID id) {
    return dynamic_cast<ModuleBase*>(processorAt(graph, id));
}

/** `module`'s PortRole::Sidechain (Key) raw input channels, ascending -- queried off the module's own
 *  input map rather than hard-coded, so any module that grows a Key pair is a Key target for free. */
std::vector<int> keyInputChannels(juce::AudioProcessorGraph& graph, NodeID module) {
    std::vector<int> channels;
    if (auto* processor = moduleAt(graph, module))
        for (int raw = 0; raw < processor->getTotalNumInputChannels(); ++raw)
            if (processor->mapInputChannel(raw).role == PortRole::Sidechain)
                channels.push_back(raw);
    return channels;
}

/** True when `conn` is an audio edge landing on a module's Key input. */
bool landsOnKey(juce::AudioProcessorGraph& graph, const Connection& conn) {
    if (conn.destination.isMIDI())
        return false;
    auto* module = moduleAt(graph, conn.destination.nodeID);
    return module != nullptr && module->mapInputChannel(conn.destination.channelIndex).role == PortRole::Sidechain;
}

bool isReachTerminal(const juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return true;
    if (dynamic_cast<const RecordTapModule*>(processor) != nullptr ||
        dynamic_cast<const MasterModule*>(processor) != nullptr)
        return true;
    return dynamic_cast<const juce::AudioProcessorGraph::AudioGraphIOProcessor*>(processor) != nullptr;
}

/** Forward from `start`, expanding THROUGH strips, to see whether `goal` is reachable -- the cycle
 *  guard behind enumerateSendTargets / enumerateKeySendTargets. Stops at the terminals; visited-set
 *  cycle guarded.
 *
 *  It follows EVERY connection, not just isSignalEdge's signal edges. The question here is
 *  "would the new cable close a RENDER cycle?", and a render cycle does not care what an input is
 *  for: a key edge (PortRole::Sidechain), a ModCV cable or a hidden attenuverter leg is exactly as
 *  much a processing dependency as an audio input. isSignalEdge deliberately ignores those because
 *  it answers a different question (what the mixer shows as a channel's signal path) -- so a strip
 *  whose output keys a Compressor on the source's own chain used to be offered as a send target and
 *  would have wired a cycle straight past the only defence there is (addConnection accepts one). */
bool reaches(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections, NodeID start, NodeID goal) {
    std::vector<NodeID> queue{start};
    std::set<NodeID> visited{start};

    while (!queue.empty()) {
        const auto nodeId = queue.back();
        queue.pop_back();

        for (const auto& conn : connections) {
            if (conn.source.nodeID != nodeId)
                continue;
            const auto destId = conn.destination.nodeID;
            if (destId == goal)
                return true;
            if (!visited.insert(destId).second)
                continue;
            if (isReachTerminal(processorAt(graph, destId)))
                continue;
            queue.push_back(destId);
        }
    }
    return false;
}

/** Removes every connection leaving `slot`'s two raw output channels, and RETURNS them, so a caller
 *  that fails half-way can put them back. Collect-then-remove: removing while iterating the list it
 *  came from invalidates it (spliceMasterNode's own reasoning). */
std::vector<Connection> dropSlotCables(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot) {
    const int left = ChannelStripModule::sendLeftChannel(slot);
    const int right = ChannelStripModule::sendRightChannel(slot);

    std::vector<Connection> doomed;
    for (const auto& conn : graph.getConnections())
        if (conn.source.nodeID == sourceStrip &&
            (conn.source.channelIndex == left || conn.source.channelIndex == right))
            doomed.push_back(conn);
    for (const auto& conn : doomed)
        graph.removeConnection(conn);
    return doomed;
}

/** Wires `slot`'s stereo pair into `target`: a strip's own ch0 / kRightBase, or a Key
 *  target's first two PortRole::Sidechain channels (both legs onto the one Key channel if a module
 *  ever declares a single one). Rolls the left leg back if the right one is refused, so a half-wired
 *  send never exists. */
bool wireSlot(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot, const SendTarget& target) {
    int leftDest = 0, rightDest = ChannelStripModule::kRightBase;
    if (target.key) {
        const auto keys = keyInputChannels(graph, target.node);
        if (keys.empty())
            return false;
        leftDest = keys[0];
        rightDest = keys.size() > 1 ? keys[1] : keys[0];
    }
    const Connection leftEdge{{sourceStrip, ChannelStripModule::sendLeftChannel(slot)}, {target.node, leftDest}};
    const Connection rightEdge{{sourceStrip, ChannelStripModule::sendRightChannel(slot)}, {target.node, rightDest}};
    if (!graph.addConnection(leftEdge))
        return false;
    if (!graph.addConnection(rightEdge)) {
        graph.removeConnection(leftEdge);
        return false;
    }
    return true;
}

/** The one legality rule behind every add/retarget and both menus: a strip target is another
 *  strip, a Key target a module with a Key input; either way its own output must not reach back
 *  to `sourceStrip` along ANY edge (reaches' own comment). */
bool targetIsLegal(juce::AudioProcessorGraph& graph, NodeID sourceStrip, const SendTarget& target) {
    if (target.node == sourceStrip || stripAt(graph, sourceStrip) == nullptr)
        return false;
    if (target.key ? keyInputChannels(graph, target.node).empty() : stripAt(graph, target.node) == nullptr)
        return false;
    const auto connections = graph.getConnections();
    return !reaches(graph, connections, target.node, sourceStrip);
}
} // namespace

std::vector<NodeID> findStripsFeedingStrip(juce::AudioProcessorGraph& graph, NodeID stripId) {
    std::vector<NodeID> sources;
    if (stripAt(graph, stripId) == nullptr)
        return sources;

    const auto connections = graph.getConnections();
    std::vector<NodeID> queue{stripId};
    std::set<NodeID> visited{stripId};

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        for (const auto& conn : connections) {
            if (conn.destination.nodeID != nodeId || !isSignalEdge(graph, connections, conn))
                continue;
            const auto sourceId = conn.source.nodeID;
            if (!visited.insert(sourceId).second)
                continue;
            if (stripAt(graph, sourceId) != nullptr) {
                sources.push_back(sourceId); // a strip feeding this one -- never expanded past
                continue;
            }
            queue.push_back(sourceId);
        }
    }
    std::sort(sources.begin(), sources.end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });
    return sources;
}

// The trusted "isBus" flag is what a freshly added, still-unfed bus has to go on -- it has no
// predecessors yet. Strips feeding it with NO track source feeding it is the structural fallback,
// so a patch built before the flag existed and a hand-wired group bus both still classify. A strip
// a track plays into is that track's channel whatever else feeds it: a send from track A
// into track B's channel must not turn B into a bus. Judged live, so no load-time migration.
bool isBusStrip(juce::AudioProcessorGraph& graph, NodeID stripId) {
    auto* strip = stripAt(graph, stripId);
    if (strip == nullptr)
        return false;
    if (strip->isBus())
        return true;
    return !findStripsFeedingStrip(graph, stripId).empty() && findTrackSourcesFeedingStrip(graph, stripId).empty();
}

// The name a bus column and a bus's stem file fall back to, a bus having no feeding track to take a
// name from.
juce::String busFallbackName(juce::AudioProcessorGraph& graph, NodeID stripId) {
    std::vector<NodeID> buses;
    for (auto* node : graph.getNodes())
        if (node != nullptr && isBusStrip(graph, node->nodeID))
            buses.push_back(node->nodeID);
    std::sort(buses.begin(), buses.end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });

    const auto found = std::find(buses.begin(), buses.end(), stripId);
    const int ordinal = found != buses.end() ? static_cast<int>(std::distance(buses.begin(), found)) + 1 : 1;
    return "Bus " + juce::String(ordinal);
}

// One breadth-first walk answers both target kinds. A Key edge must be tested BEFORE the
// isSignalEdge filter, since that filter exists precisely to drop key edges (a bass keyed from a
// kick is not a bus) -- which is why a Key send used to resolve to "no target" at all. Hits are
// queued rather than returned on sight so the NEAREST target wins whichever kind it is, and a Key
// hit is terminal: whatever the keyed module feeds is its channel's business, not this send's.
SendTarget resolveSendTarget(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot))
        return {};

    const auto connections = graph.getConnections();
    std::vector<SendTarget> queue; // key == true entries are Key hits, never expanded
    std::set<NodeID> visited{sourceStrip};
    std::set<NodeID> keyHits;
    const auto follow = [&](const Connection& conn) {
        if (landsOnKey(graph, conn)) {
            if (keyHits.insert(conn.destination.nodeID).second)
                queue.push_back({conn.destination.nodeID, true});
            return;
        }
        if (!isSignalEdge(graph, connections, conn))
            return;
        if (visited.insert(conn.destination.nodeID).second)
            queue.push_back({conn.destination.nodeID, false});
    };

    const int leg = ChannelStripModule::sendLeftChannel(slot);
    for (const auto& conn : connections)
        if (conn.source.nodeID == sourceStrip && conn.source.channelIndex == leg)
            follow(conn);

    for (size_t head = 0; head < queue.size(); ++head) {
        const auto entry = queue[head];
        if (entry.key)
            return entry; // the send lands on this module's Key input
        auto* processor = processorAt(graph, entry.node);
        if (dynamic_cast<ChannelStripModule*>(processor) != nullptr)
            return entry; // the first strip the send reaches IS the bus it feeds
        if (isReachTerminal(processor))
            continue; // the send left the patch without ever reaching a bus

        for (const auto& conn : connections)
            if (conn.source.nodeID == entry.node)
                follow(conn);
    }
    return {};
}

// The strip-only view of resolveSendTarget: a forward walk from the slot's raw LEFT output to the
// FIRST ChannelStripModule it reaches (the same "stop at the first strip" rule
// findStripFedByTrackSource uses), so a module the user inserted on the send path still resolves to
// the bus behind it. Invalid when the slot is inactive, unconnected, its cable leaves the patch
// without passing a strip, or it feeds a Key input -- every bus/solo/stem caller keeps
// meaning "the STRIP this send feeds".
NodeID findSendTarget(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot) {
    const auto target = resolveSendTarget(graph, sourceStrip, slot);
    return target.key ? NodeID{} : target.node;
}

// Deliberately the same forward walk the track/channel link uses (findStripFedByTrackSource works
// from any node): the channel a keyed module "sits on" is the channel its own audio reaches.
NodeID findKeyTargetChannel(juce::AudioProcessorGraph& graph, NodeID module) {
    return findStripFedByTrackSource(graph, module);
}

// The module half is the card title the canvas paints (a custom "displayName" first, then the
// processor's auto-numbered "Compressor 2" -- GraphEditor::getModuleTitle's own rule, restated
// here because Core cannot reach a GraphEditor).
juce::String keySendTargetName(juce::AudioProcessorGraph& graph, NodeID module, const juce::String& channelName) {
    auto* node = graph.getNodeForId(module);
    if (node == nullptr)
        return "No target";
    auto title = node->properties["displayName"].toString();
    if (title.isEmpty() && node->getProcessor() != nullptr)
        title = node->getProcessor()->getName();
    return "Key: " + title + (channelName.isNotEmpty() ? " on " + channelName : juce::String());
}

// Its grouping macro's name when `macros` has one, else the bus fallback name ("Bus N") or plain
// "Channel" for an ordinary strip. Shared by MixerSendList's menus and the automation lane picker,
// so both always name the same bus the same way.
juce::String sendTargetName(juce::AudioProcessorGraph& graph, const MacroSet* macros, NodeID target) {
    auto* node = target == NodeID{} ? nullptr : graph.getNodeForId(target);
    if (node == nullptr)
        return "No target";
    if (macros != nullptr)
        if (const auto* macro = macros->findByMember(node->properties["uuid"].toString()))
            return macro->name;
    return isBusStrip(graph, target) ? busFallbackName(graph, target) : juce::String("Channel");
}

juce::String sendTargetName(juce::AudioProcessorGraph& graph, const MacroSet* macros, const SendTarget& target) {
    if (!target.key)
        return sendTargetName(graph, macros, target.node);
    const auto channel = findKeyTargetChannel(graph, target.node);
    return keySendTargetName(graph, target.node,
                             channel != NodeID{} ? sendTargetName(graph, macros, channel) : juce::String());
}

juce::String describeSendSlotLabel(juce::AudioProcessorGraph& graph, const MacroSet* macros, NodeID sourceStrip,
                                   int slot) {
    const auto target = resolveSendTarget(graph, sourceStrip, slot);
    if (!target.isValid())
        return "Send " + juce::String(slot + 1) + " (no target)";
    return "Send to " + sendTargetName(graph, macros, target);
}

// Every OTHER ChannelStripModule minus any whose own output already reaches `sourceStrip` -- those
// would close a feedback loop. This walk is the ONLY cycle defence, not merely the menu's half of
// one: juce::AudioProcessorGraph does not refuse a cycle (canConnect checks node existence, channel
// bounds and "not already connected", nothing more), so addSend applies the same check rather than
// leaning on a backstop that isn't there.
std::vector<NodeID> enumerateSendTargets(juce::AudioProcessorGraph& graph, NodeID sourceStrip) {
    std::vector<NodeID> targets;
    if (stripAt(graph, sourceStrip) == nullptr)
        return targets;

    const auto connections = graph.getConnections();
    for (auto* node : graph.getNodes()) {
        if (node == nullptr || node->nodeID == sourceStrip)
            continue;
        if (dynamic_cast<ChannelStripModule*>(node->getProcessor()) == nullptr)
            continue;
        if (reaches(graph, connections, node->nodeID, sourceStrip))
            continue; // would close a feedback loop -- never offered
        targets.push_back(node->nodeID);
    }
    std::sort(targets.begin(), targets.end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });
    return targets;
}

std::vector<NodeID> enumerateKeySendTargets(juce::AudioProcessorGraph& graph, NodeID sourceStrip) {
    std::vector<NodeID> targets;
    if (stripAt(graph, sourceStrip) == nullptr)
        return targets;

    for (auto* node : graph.getNodes())
        if (node != nullptr && targetIsLegal(graph, sourceStrip, {node->nodeID, true}))
            targets.push_back(node->nodeID);
    std::sort(targets.begin(), targets.end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });
    return targets;
}

int addSend(juce::AudioProcessorGraph& graph, NodeID sourceStrip, NodeID target) {
    return addSend(graph, sourceStrip, SendTarget{target, false});
}

// Activates `sourceStrip`'s lowest free slot (post-fader, unity -- see ChannelStripModule::addSend)
// and wires its stereo pair into the target (wireSlot). -1 when either node is the wrong kind, all
// kMaxSends slots are in use, the target would close a cycle, or the graph refuses the connection
// -- in every failing case NOTHING is changed, so the caller can abandon its undo transaction
// rather than record a no-op step.
int addSend(juce::AudioProcessorGraph& graph, NodeID sourceStrip, const SendTarget& target) {
    if (!targetIsLegal(graph, sourceStrip, target))
        return -1;

    auto* strip = stripAt(graph, sourceStrip);
    const int slot = strip->addSend();
    if (slot < 0)
        return -1;

    if (!wireSlot(graph, sourceStrip, slot, target)) {
        strip->setSendActive(slot, false); // nothing changed -- see the header's "no-op" contract
        return -1;
    }
    return slot;
}

bool removeSend(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot))
        return false;

    dropSlotCables(graph, sourceStrip, slot);
    strip->setSendActive(slot, false);
    strip->setSendPreFader(slot, false);
    strip->setSendMuted(slot, false); // A reused slot always starts unmuted
    strip->setSendMono(slot, false);  // And stereo
    return true;
}

bool setSendMuted(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot, bool muted) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot))
        return false;
    strip->setSendMuted(slot, muted);
    return true;
}

bool setSendMono(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot, bool mono) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot))
        return false;
    strip->setSendMono(slot, mono);
    return true;
}

namespace {

/** Remaps `conn`'s SOURCE channel from `fromSlot`'s raw pair to `toSlot`'s -- the destination side
 *  (whatever the send actually feeds) is untouched, which is what carries a module inserted on the
 *  send path along with the swap for free. */
Connection remapSlotSource(const Connection& conn, NodeID sourceStrip, int fromSlot, int toSlot) {
    int channel = conn.source.channelIndex;
    if (channel == ChannelStripModule::sendLeftChannel(fromSlot))
        channel = ChannelStripModule::sendLeftChannel(toSlot);
    else if (channel == ChannelStripModule::sendRightChannel(fromSlot))
        channel = ChannelStripModule::sendRightChannel(toSlot);
    return Connection{{sourceStrip, channel}, conn.destination};
}

} // namespace

// The user wants "Send 1" to always be the top row everywhere -- the knob's title, a lane's name,
// the target menu -- so reordering SWAPS THE REAL SLOTS rather than reshuffling a display-only
// order on top of them. sendNLevel/sendNPan are fixed per-slot identities ("send1Level" always
// names slot 0), so a swap moves what those two parameter OBJECTS hold, never which object is which
// -- same reasoning as retargetSend never re-creating a slot's parameters. Automation-lane
// rebinding is deliberately NOT here: a lane belongs to a TimelineDoc, which this headless unit
// never sees (same boundary as retargetSend not touching MIDI Learn) -- the caller that owns the
// doc (MixerPanelComponent) replays moveSendRow's own swap sequence against it
// (see docs/mixer/sends-and-buses.md#reordering-sends).
bool swapSends(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slotA, int slotB) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || slotA < 0 || slotA >= ChannelStripModule::kMaxSends || slotB < 0 ||
        slotB >= ChannelStripModule::kMaxSends)
        return false;
    if (slotA == slotB)
        return true;

    const auto cablesA = dropSlotCables(graph, sourceStrip, slotA);
    const auto cablesB = dropSlotCables(graph, sourceStrip, slotB);

    // Same "collect what was added, roll it all back on a partial refusal" shape as retargetSend's
    // own rollback, just over two cable sets instead of one.
    std::vector<Connection> added;
    bool ok = true;
    for (const auto& conn : cablesA) {
        const auto remapped = remapSlotSource(conn, sourceStrip, slotA, slotB);
        if (!graph.addConnection(remapped)) {
            ok = false;
            break;
        }
        added.push_back(remapped);
    }
    if (ok)
        for (const auto& conn : cablesB) {
            const auto remapped = remapSlotSource(conn, sourceStrip, slotB, slotA);
            if (!graph.addConnection(remapped)) {
                ok = false;
                break;
            }
            added.push_back(remapped);
        }

    if (!ok) {
        for (const auto& conn : added)
            graph.removeConnection(conn);
        for (const auto& conn : cablesA)
            graph.addConnection(conn);
        for (const auto& conn : cablesB)
            graph.addConnection(conn);
        return false;
    }

    // State bits: a plain field swap, both directions at once so neither read sees the other's
    // already-written value.
    const bool activeA = strip->isSendActive(slotA), activeB = strip->isSendActive(slotB);
    const bool preA = strip->isSendPreFader(slotA), preB = strip->isSendPreFader(slotB);
    const bool muteA = strip->isSendMuted(slotA), muteB = strip->isSendMuted(slotB);
    const bool monoA = strip->isSendMono(slotA), monoB = strip->isSendMono(slotB);
    strip->setSendActive(slotA, activeB);
    strip->setSendActive(slotB, activeA);
    strip->setSendPreFader(slotA, preB);
    strip->setSendPreFader(slotB, preA);
    strip->setSendMuted(slotA, muteB);
    strip->setSendMuted(slotB, muteA);
    strip->setSendMono(slotA, monoB);
    strip->setSendMono(slotB, monoA);

    // Parameter VALUES, not the parameter objects -- "send1Level" always names slot 0, so swapping
    // slots means swapping what the two fixed-identity parameters hold. Both slots share the same
    // range (ChannelStripModule's ctor gives every send the same NormalisableRange), so converting
    // through slotA's own range for both writes is exact -- same shape as MixerSendList's own knob
    // attachment writing through setValueNotifyingHost.
    if (auto* levelA = strip->getSendLevelParameter(slotA))
        if (auto* levelB = strip->getSendLevelParameter(slotB)) {
            const auto& range = levelA->getNormalisableRange();
            const float valueA = levelA->get(), valueB = levelB->get();
            levelA->setValueNotifyingHost(range.convertTo0to1(valueB));
            levelB->setValueNotifyingHost(range.convertTo0to1(valueA));
        }
    if (auto* panA = strip->getSendPanParameter(slotA))
        if (auto* panB = strip->getSendPanParameter(slotB)) {
            const auto& range = panA->getNormalisableRange();
            const float valueA = panA->get(), valueB = panB->get();
            panA->setValueNotifyingHost(range.convertTo0to1(valueB));
            panB->setValueNotifyingHost(range.convertTo0to1(valueA));
        }

    return true;
}

// `fromRow`/`toRow` index the ACTIVE-slot-ordered visible list (MixerModelSends.cpp's
// buildSendsForColumn), never a raw slot number. `appliedSwaps`, when non-null, is cleared and
// filled with the exact (slotA, slotB) pairs applied, in order -- MixerPanelComponent replays the
// same sequence against every automation lane bound to a moved slot's sendNLevel/sendNPan. False
// (nothing changed, `appliedSwaps` left empty) for `fromRow == toRow`, an out-of-range row, or an
// all-or-nothing refusal partway through the sequence (every completed swap is undone first).
bool moveSendRow(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int fromRow, int toRow,
                 std::vector<std::pair<int, int>>* appliedSwaps) {
    if (appliedSwaps != nullptr)
        appliedSwaps->clear();

    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || fromRow == toRow)
        return false;

    std::vector<int> activeSlots;
    for (int slot = 0; slot < ChannelStripModule::kMaxSends; ++slot)
        if (strip->isSendActive(slot))
            activeSlots.push_back(slot);
    if (fromRow < 0 || fromRow >= (int)activeSlots.size() || toRow < 0 || toRow >= (int)activeSlots.size())
        return false;

    // A walk of adjacent swaps through the ACTIVE-slot list, from fromRow to toRow. `activeSlots`
    // is the FIXED list of which physical slot sits at each visible position -- swapping two
    // already-active slots' CONTENT never changes which slot numbers are active, so position i is
    // slot activeSlots[i] for the whole walk (never re-derived or re-ordered mid-loop). Each step
    // moves the row being dragged one position closer to toRow by exchanging its content with its
    // neighbour's, so after the whole walk it has passed through every position in between and
    // ended up at toRow -- while a sparse gap (an inactive slot between two active ones) is simply
    // never one of the positions a step touches.
    std::vector<std::pair<int, int>> applied;
    const int step = toRow > fromRow ? 1 : -1;
    for (int i = fromRow; i != toRow; i += step) {
        const int a = activeSlots[(size_t)i];
        const int b = activeSlots[(size_t)(i + step)];
        if (!swapSends(graph, sourceStrip, a, b)) {
            // All-or-nothing across the whole sequence: swapSends is its own inverse, so undoing
            // every step already applied is just replaying them again, in reverse order.
            for (auto it = applied.rbegin(); it != applied.rend(); ++it)
                swapSends(graph, sourceStrip, it->first, it->second);
            return false;
        }
        applied.emplace_back(a, b);
    }

    if (appliedSwaps != nullptr)
        *appliedSwaps = std::move(applied);
    return true;
}

bool retargetSend(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot, NodeID target) {
    return retargetSend(graph, sourceStrip, slot, SendTarget{target, false});
}

bool retargetSend(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot, const SendTarget& target) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot) || !targetIsLegal(graph, sourceStrip, target))
        return false;
    // The whole SendTarget, not just the node: a Key send on a module and a strip send landing on
    // that same node would otherwise read as "already there".
    if (resolveSendTarget(graph, sourceStrip, slot) == target)
        return true; // already there

    // Put the old cables back if the new pair is refused, or a failed retarget would leave the slot
    // silently unwired -- the header promises the same "nothing changed" as addSend. DEFENSIVE, not
    // a path anything reaches today: juce::AudioProcessorGraph::canConnect checks node existence,
    // channel bounds and "not already connected" and nothing else -- notably it does NOT refuse a
    // cycle (measured: wiring one straight back through an Attenuverter is accepted), and
    // targetIsLegal has already ruled out every case left. Same shape, one level up, as wireSlot's
    // own left-leg rollback; it is what keeps the contract true if a future guard or channel-map
    // change ever makes a refusal reachable.
    const auto previous = dropSlotCables(graph, sourceStrip, slot);
    if (wireSlot(graph, sourceStrip, slot, target))
        return true;
    for (const auto& conn : previous)
        graph.addConnection(conn);
    return false;
}

} // namespace synth
