// Concern: FRO15 (P9-9) -- the send slot flows (add / remove / retarget / resolve target / offer
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

bool isReachTerminal(const juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return true;
    if (dynamic_cast<const RecordTapModule*>(processor) != nullptr ||
        dynamic_cast<const MasterModule*>(processor) != nullptr)
        return true;
    return dynamic_cast<const juce::AudioProcessorGraph::AudioGraphIOProcessor*>(processor) != nullptr;
}

/** Forward from `start`, expanding THROUGH strips, to see whether `goal` is reachable -- the cycle
 *  guard behind enumerateSendTargets. Stops at the terminals; visited-set cycle guarded. */
bool reaches(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections, NodeID start, NodeID goal) {
    std::vector<NodeID> queue{start};
    std::set<NodeID> visited{start};

    while (!queue.empty()) {
        const auto nodeId = queue.back();
        queue.pop_back();

        for (const auto& conn : connections) {
            if (conn.source.nodeID != nodeId || !isSignalEdge(graph, connections, conn))
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

/** Wires `slot`'s stereo pair into `target`'s own ch0 / kRightBase. Rolls the left leg back if the
 *  right one is refused, so a half-wired send never exists. */
bool wireSlot(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot, NodeID target) {
    const Connection leftEdge{{sourceStrip, ChannelStripModule::sendLeftChannel(slot)}, {target, 0}};
    const Connection rightEdge{{sourceStrip, ChannelStripModule::sendRightChannel(slot)},
                               {target, ChannelStripModule::kRightBase}};
    if (!graph.addConnection(leftEdge))
        return false;
    if (!graph.addConnection(rightEdge)) {
        graph.removeConnection(leftEdge);
        return false;
    }
    return true;
}

bool targetIsLegal(juce::AudioProcessorGraph& graph, NodeID sourceStrip, NodeID target) {
    if (target == sourceStrip || stripAt(graph, target) == nullptr || stripAt(graph, sourceStrip) == nullptr)
        return false;
    const auto connections = graph.getConnections();
    return !reaches(graph, connections, target, sourceStrip);
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

bool isBusStrip(juce::AudioProcessorGraph& graph, NodeID stripId) {
    auto* strip = stripAt(graph, stripId);
    if (strip == nullptr)
        return false;
    return strip->isBus() || !findStripsFeedingStrip(graph, stripId).empty();
}

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

NodeID findSendTarget(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot))
        return {};

    const auto connections = graph.getConnections();
    const int leg = ChannelStripModule::sendLeftChannel(slot);

    std::vector<NodeID> queue;
    std::set<NodeID> visited{sourceStrip};
    for (const auto& conn : connections) {
        if (conn.source.nodeID != sourceStrip || conn.source.channelIndex != leg)
            continue;
        if (!isSignalEdge(graph, connections, conn))
            continue;
        if (visited.insert(conn.destination.nodeID).second)
            queue.push_back(conn.destination.nodeID);
    }

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        auto* processor = processorAt(graph, nodeId);
        if (dynamic_cast<ChannelStripModule*>(processor) != nullptr)
            return nodeId; // the first strip the send reaches IS the bus it feeds
        if (isReachTerminal(processor))
            continue; // the send left the patch without ever reaching a bus

        for (const auto& conn : connections) {
            if (conn.source.nodeID != nodeId || !isSignalEdge(graph, connections, conn))
                continue;
            if (visited.insert(conn.destination.nodeID).second)
                queue.push_back(conn.destination.nodeID);
        }
    }
    return {};
}

juce::String sendTargetName(juce::AudioProcessorGraph& graph, const MacroSet* macros, NodeID target) {
    auto* node = target == NodeID{} ? nullptr : graph.getNodeForId(target);
    if (node == nullptr)
        return "No target";
    if (macros != nullptr)
        if (const auto* macro = macros->findByMember(node->properties["uuid"].toString()))
            return macro->name;
    return isBusStrip(graph, target) ? busFallbackName(graph, target) : juce::String("Channel");
}

juce::String describeSendSlotLabel(juce::AudioProcessorGraph& graph, const MacroSet* macros, NodeID sourceStrip,
                                   int slot) {
    const auto target = findSendTarget(graph, sourceStrip, slot);
    if (target == NodeID{})
        return "Send " + juce::String(slot + 1) + " (no target)";
    return "Send to " + sendTargetName(graph, macros, target);
}

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

int addSend(juce::AudioProcessorGraph& graph, NodeID sourceStrip, NodeID target) {
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
    strip->setSendMuted(slot, false); // FRO295: a reused slot always starts unmuted
    strip->setSendMono(slot, false);  // FRO294: and stereo
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

// FRO296 (docs/mixer/sends-and-buses.md#reordering-sends): the user wants "Send 1" to always be the
// top row everywhere -- the knob's title, a lane's name, the target menu -- so reordering SWAPS THE
// REAL SLOTS rather than reshuffling a display-only order on top of them. sendNLevel/sendNPan are
// fixed per-slot identities ("send1Level" always names slot 0), so a swap moves what those two
// parameter OBJECTS hold, never which object is which -- same reasoning as retargetSend never
// re-creating a slot's parameters. Automation-lane rebinding is deliberately NOT here: a lane
// belongs to a TimelineDoc, which this headless unit never sees (same boundary as retargetSend not
// touching MIDI Learn) -- the caller that owns the doc (MixerPanelComponent) replays moveSendRow's
// own swap sequence against it.
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
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot) || !targetIsLegal(graph, sourceStrip, target))
        return false;
    if (findSendTarget(graph, sourceStrip, slot) == target)
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
