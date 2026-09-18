// Concern: FRO25 (P9-3d) "Make channel" -- planMakeChannel/buildMakeChannel/resolveChannelSource
// and the internal helpers that classify a chain's own region, shared merge heads and side inputs.
#include "ChannelFlows.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "ChannelFlowsInternal.h"
#include "MacroSet.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/RecordTapModule.h"
#include <algorithm>

namespace synth {

// ---- FRO25 (P9-3d): "Make channel" --------------------------------------------------------------

// FRO13 (P9-7): lifted out of the anonymous namespace below (was: private to this file) so
// ChannelFlowsTrackPreset.cpp's outside-modulator walk can reuse them too — see
// ChannelFlowsInternal.h's extern declarations for why this is the one definition.
juce::AudioProcessor* processorFor(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? node->getProcessor() : nullptr;
}

bool isAttenuverter(const juce::AudioProcessor* p) { return dynamic_cast<const AttenuverterModule*>(p) != nullptr; }

bool isStrip(const juce::AudioProcessor* p) { return dynamic_cast<const ChannelStripModule*>(p) != nullptr; }

bool isMacroPortNode(const juce::AudioProcessor* p) {
    auto* module = dynamic_cast<const ModuleBase*>(p);
    if (module == nullptr)
        return false;
    const auto type = module->getModuleType();
    return type == ModuleType::MacroInlet || type == ModuleType::MacroOutlet || type == ModuleType::MacroMidiInlet ||
           type == ModuleType::MacroMidiOutlet;
}

// planMakeChannel's signal-edge rule (ChannelFlows.h): every MIDI edge, and every audio edge that
// neither touches a hidden modulation attenuverter nor lands on a PortRole::ModCV pin. The pin a
// cable into `pin` ultimately lands on, looking through macro port nodes (each a pure per-channel
// pass-through). An auto-created port reports a plain Audio role and no stereo side of its own, so
// both the ModCV check below and crossingIsRight's L/R read must use the module behind it -- a
// Cutoff CV cable into a bus through its port is still a CV cable.
//
// FRO11 (P9-5): promoted out of this file's anonymous namespace to synth-namespace/external
// linkage (declared in ChannelFlows.h) so MixerModelInserts.cpp's insert-list walk can reuse the
// exact same rule instead of re-deriving it -- the whole point being that "what counts as signal"
// answers identically for "what would Make Channel do" and "what does the mixer column show".
static juce::AudioProcessorGraph::NodeAndChannel
resolveThroughPorts(juce::AudioProcessorGraph& graph,
                    const std::vector<juce::AudioProcessorGraph::Connection>& connections,
                    juce::AudioProcessorGraph::NodeAndChannel pin) {
    using Connection = juce::AudioProcessorGraph::Connection;
    for (int hop = 0; hop < 16 && isMacroPortNode(processorFor(graph, pin.nodeID)); ++hop) {
        const auto next = std::find_if(connections.begin(), connections.end(), [&](const Connection& c) {
            return c.source.nodeID == pin.nodeID && c.source.channelIndex == pin.channelIndex;
        });
        if (next == connections.end())
            break;
        pin = next->destination;
    }
    return pin;
}

bool isSignalEdge(juce::AudioProcessorGraph& graph,
                  const std::vector<juce::AudioProcessorGraph::Connection>& connections,
                  const juce::AudioProcessorGraph::Connection& c) {
    auto* src = processorFor(graph, c.source.nodeID);
    auto* dst = processorFor(graph, c.destination.nodeID);
    if (src == nullptr || dst == nullptr || isAttenuverter(src) || isAttenuverter(dst))
        return false;
    if (c.source.isMIDI())
        return true;
    const auto pin = resolveThroughPorts(graph, connections, c.destination);
    if (auto* module = dynamic_cast<ModuleBase*>(processorFor(graph, pin.nodeID)))
        return module->mapInputChannel(pin.channelIndex).role != PortRole::ModCV;
    return true;
}

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;
using NodeAndChannel = juce::AudioProcessorGraph::NodeAndChannel;

bool containsId(const std::vector<NodeID>& ids, NodeID id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void addUniqueId(std::vector<NodeID>& ids, NodeID id) {
    if (!containsId(ids, id))
        ids.push_back(id);
}

void addUniquePin(std::vector<NodeAndChannel>& pins, const NodeAndChannel& pin) {
    if (std::find(pins.begin(), pins.end(), pin) == pins.end())
        pins.push_back(pin);
}

// findUnchanneledOutputFeeds' own terminal set: Audio Output, Record Tap, Master.
bool isTerminal(const juce::AudioProcessor* p) {
    return p != nullptr && (dynamic_cast<const RecordTapModule*>(p) != nullptr ||
                            dynamic_cast<const MasterModule*>(p) != nullptr || p->getName() == "Audio Output");
}

// The device-input singleton (GraphEditor::isSingletonIOModule): walked through like any other hop
// so its exits are still found, but never boxed into a channel macro.
bool isAudioInput(const juce::AudioProcessor* p) { return p != nullptr && p->getName() == "Audio Input"; }

struct Reach {
    std::vector<NodeID> nodes; // BFS order, `start` first; never a terminal or an attenuverter
    bool reachesTerminal = false;
};

Reach reachFrom(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections, NodeID start) {
    Reach reach;
    reach.nodes.push_back(start);
    for (size_t i = 0; i < reach.nodes.size(); ++i) {
        const auto id = reach.nodes[i];
        auto* processor = processorFor(graph, id);
        if (processor == nullptr || isTerminal(processor) || isAttenuverter(processor))
            continue;
        for (const auto& c : connections) {
            if (c.source.nodeID != id || !isSignalEdge(graph, connections, c))
                continue;
            if (isTerminal(processorFor(graph, c.destination.nodeID))) {
                reach.reachesTerminal = true;
                continue;
            }
            addUniqueId(reach.nodes, c.destination.nodeID);
        }
    }
    return reach;
}

// Every node `id`'s output ends up in, looking through a modulation attenuverter to the node it
// modulates. A half-wired attenuverter (nothing on its output yet) reports itself, which is never a
// member — so it blocks absorption rather than letting a dangling routing drag its source along.
std::vector<NodeID> realConsumers(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections,
                                  NodeID id) {
    std::vector<NodeID> consumers;
    for (const auto& c : connections) {
        if (c.source.nodeID != id)
            continue;
        if (!isAttenuverter(processorFor(graph, c.destination.nodeID))) {
            addUniqueId(consumers, c.destination.nodeID);
            continue;
        }
        bool any = false;
        for (const auto& out : connections)
            if (out.source.nodeID == c.destination.nodeID) {
                addUniqueId(consumers, out.destination.nodeID);
                any = true;
            }
        if (!any)
            addUniqueId(consumers, c.destination.nodeID);
    }
    return consumers;
}

// Side-input absorption (see planMakeChannel's own comment below), to a fixpoint: a node no track
// reaches whose every consumer is already in `members` joins it. `excluded` is a set already
// claimed by another channel.
void absorbSideInputs(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections,
                      std::vector<NodeID>& members, const std::vector<NodeID>& reachedByTracks,
                      const std::vector<NodeID>& excluded) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* node : graph.getNodes()) {
            if (node == nullptr)
                continue;
            const auto id = node->nodeID;
            if (containsId(members, id) || containsId(reachedByTracks, id) || containsId(excluded, id))
                continue;
            auto* processor = node->getProcessor();
            if (dynamic_cast<ModuleBase*>(processor) == nullptr || isTerminal(processor) || isAttenuverter(processor) ||
                isMacroPortNode(processor) || isTrackSourceNode(processor) || isStrip(processor) ||
                isAudioInput(processor))
                continue;
            const auto consumers = realConsumers(graph, connections, id);
            if (consumers.empty())
                continue;
            if (std::all_of(consumers.begin(), consumers.end(), [&](NodeID c) { return containsId(members, c); })) {
                members.push_back(id);
                changed = true;
            }
        }
    }
}

// Which strip leg a crossing into `processor`'s input `channel` belongs on: a split-block or
// Dual-I/O right leg (rightAudioLegChannel — never assume ch1, Source/Modules/CLAUDE.md), or the
// follower leg of a collapsed stereo "Audio" jack. Everything else (a mono input) is Left.
bool isRightLegInput(juce::AudioProcessor* processor, int channel) {
    auto* module = dynamic_cast<ModuleBase*>(processor);
    if (module == nullptr)
        return channel == 1;
    const int right = module->rightAudioLegChannel();
    if (right >= 1 && channel == right)
        return true;
    const auto port = module->mapInputChannel(channel);
    if (port.isPolyGroupHead)
        return false;
    for (int raw = 0; raw < channel; ++raw) {
        const auto head = module->mapInputChannel(raw);
        if (head.visibleJackIndex == port.visibleJackIndex && head.isPolyGroupHead)
            return head.role == PortRole::Audio && head.polyVoiceSpan == 2 && channel - raw == 1;
    }
    // A contiguous stereo input exposed as two separate Audio jacks (ch0 = L, ch1 = R).
    return channel == 1 && module->mapInputChannel(0).role == PortRole::Audio &&
           module->mapInputChannel(1).role == PortRole::Audio;
}

// The side of a crossing landing on `dest`, read off the pin it ultimately reaches through any macro
// port nodes (resolveThroughPorts): an auto-created Mono port fronting ONE leg of a stereo input
// carries no side of its own — the module behind it does.
bool crossingIsRight(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections,
                     const NodeAndChannel& dest) {
    const auto pin = resolveThroughPorts(graph, connections, dest);
    return isRightLegInput(processorFor(graph, pin.nodeID), pin.channelIndex);
}

// True when `channel` is on one of a poly-mode module's multi-voice jacks (span > 1, read off the
// jack's head raw channel the same way GraphEditor::buildMacroPortCrossingPlan does). A poly VCA's
// output is a span-1 head (it self-sums), so it never qualifies — consistent with P9-3j.
bool isPolyFeed(juce::AudioProcessor* processor, int channel) {
    if (!isProcessorPoly(processor))
        return false;
    auto* module = dynamic_cast<ModuleBase*>(processor);
    if (module == nullptr)
        return false;
    const auto port = module->mapOutputChannel(channel);
    for (int raw = 0; raw < processor->getTotalNumOutputChannels(); ++raw) {
        const auto head = module->mapOutputChannel(raw);
        if (head.visibleJackIndex == port.visibleJackIndex && head.isPolyGroupHead)
            return head.polyVoiceSpan > 1;
    }
    return false;
}

bool exitIsRight(juce::AudioProcessorGraph& graph, const Connection& exit) {
    const int channel = exit.destination.channelIndex;
    if (dynamic_cast<MasterModule*>(processorFor(graph, exit.destination.nodeID)) != nullptr)
        return channel == MasterModule::kDirectRight;
    return channel == 1;
}

bool pinLess(const NodeAndChannel& a, const NodeAndChannel& b) {
    return a.nodeID.uid != b.nodeID.uid ? a.nodeID.uid < b.nodeID.uid : a.channelIndex < b.channelIndex;
}

struct BuiltChain {
    DefaultChannel channel;
    std::vector<juce::String> voiceMixerUuids;
};

// Rebuilds `exits` (onto Master's Mix) and `crossings` (onto their original input pins) through one
// new EQ -> Compressor -> Strip, with a Voice Mixer ahead of any poly feed. See buildMakeChannel.
BuiltChain buildChainFromEdges(juce::AudioProcessorGraph& graph, const std::vector<Connection>& exits,
                               const std::vector<Connection>& crossings, const ChannelLayoutFn& layoutRightOf) {
    BuiltChain built;
    std::vector<NodeAndChannel> left, right;
    ChannelSink sink;
    sink.toMaster = !exits.empty();
    const auto connections = graph.getConnections(); // before the rewiring below
    for (const auto& exit : exits)
        addUniquePin(exitIsRight(graph, exit) ? right : left, exit.source);
    for (const auto& crossing : crossings) {
        const bool isRight = crossingIsRight(graph, connections, crossing.destination);
        addUniquePin(isRight ? right : left, crossing.source);
        addUniquePin(isRight ? sink.rightDests : sink.leftDests, crossing.destination);
    }

    // Collect, then mutate — same reasoning as buildChannelForFeeds.
    for (const auto& exit : exits)
        graph.removeConnection(exit);
    for (const auto& crossing : crossings)
        graph.removeConnection(crossing);

    // docs/mixer.md §5.4/§5.8: a chain ending poly gets a Voice Mixer ahead of the strip — one per
    // poly source node, its ch0 standing in for every Left feed from that node and ch1 for Right.
    std::vector<std::pair<NodeID, juce::AudioProcessorGraph::Node*>> mixers;
    auto substitutePoly = [&](std::vector<NodeAndChannel>& feeds, int mixerChannel) {
        std::vector<NodeAndChannel> result;
        for (auto feed : feeds) {
            auto* node = graph.getNodeForId(feed.nodeID);
            if (node != nullptr && isPolyFeed(node->getProcessor(), feed.channelIndex)) {
                juce::AudioProcessorGraph::Node* mixer = nullptr;
                for (const auto& entry : mixers)
                    if (entry.first == feed.nodeID)
                        mixer = entry.second;
                if (mixer == nullptr) {
                    juce::String mixerUuid;
                    mixer = addVoiceMixerForPolyInstrument(graph, *node, layoutRightOf(*node).eq, mixerUuid);
                    if (mixer != nullptr) {
                        mixers.emplace_back(feed.nodeID, mixer);
                        built.voiceMixerUuids.push_back(mixerUuid);
                    }
                }
                if (mixer != nullptr)
                    feed = {mixer->nodeID, mixerChannel};
            }
            addUniquePin(result, feed);
        }
        feeds = result;
    };
    substitutePoly(left, 0);
    substitutePoly(right, 1);

    // The new row starts right of the rightmost feed (a Voice Mixer, when one was just added).
    juce::AudioProcessorGraph::Node* anchor = nullptr;
    for (const auto* feeds : {&left, &right})
        for (const auto& feed : *feeds)
            if (auto* node = graph.getNodeForId(feed.nodeID))
                if (anchor == nullptr || static_cast<int>(node->properties.getWithDefault("x", 0)) >
                                             static_cast<int>(anchor->properties.getWithDefault("x", 0)))
                    anchor = node;
    if (anchor == nullptr)
        return built;

    built.channel = buildChannelChain(graph, left, right, layoutRightOf(*anchor), sink);
    return built;
}

std::vector<juce::String> ensureUuids(juce::AudioProcessorGraph& graph, const std::vector<NodeID>& ids) {
    std::vector<juce::String> uuids;
    for (const auto id : ids)
        if (auto* node = graph.getNodeForId(id)) {
            const auto uuid = AIStateMapper::ensureNodeUuid(node);
            if (uuid.isNotEmpty())
                uuids.push_back(uuid);
        }
    return uuids;
}

} // namespace

// "Which modules does this track use" is answered against every OTHER such node in the graph,
// never against TimelineDoc (Core has no reference to it).
bool isTrackSourceNode(const juce::AudioProcessor* processor) {
    auto* module = dynamic_cast<const ModuleBase*>(processor);
    if (module == nullptr)
        return false;
    const auto type = module->getModuleType();
    return type == ModuleType::TimelineMidiSource || type == ModuleType::TimelineAudioSource;
}

// Signal reach: a forward walk from a node along every MIDI edge and every audio edge whose
// destination pin is not a modulation input (PortRole::ModCV) — so a plain CV cable into another
// track's cutoff, like an AudioEngine::addModRouting leg (an AttenuverterModule, never entered),
// never makes two chains "the same chain". The walk passes THROUGH Channel Strips and macro port
// nodes and stops at the terminals (Audio Output, Record Tap, Master).
//
//   - own region: nodes `source` reaches that no OTHER track source reaches (macro port nodes are
//     walked through but never own anything). These are the modules "used only by this track".
//   - shared region: nodes `source` reaches that another track also reaches — never moved into this
//     track's channel. Where this track's own region feeds into it is a MERGE: each such merge head
//     that still reaches the output without a strip becomes its OWN bus channel (`buses`), holding
//     the shared nodes downstream of it.
//   - side inputs: a node no track source reaches (an LFO, a free oscillator) whose every consumer
//     (looking through modulation attenuverters) is already a member is absorbed into the member
//     set, to a fixpoint (see absorbSideInputs above). A side input with a consumer anywhere else —
//     the shared-LFO case — stays outside, and the caller's auto-port pass fronts its cable with a
//     macro port.
//
// The new strip takes over `exits` (edges from the own region onto the output) and `stripCrossings`
// (audio edges from the own region into shared modules carrying the same signal the exits carry,
// or — with no exits at all — every audio edge into the shared region, provided each side feeds one
// consistent signal; otherwise `refusal`). An audio edge into the shared region that carries a
// DIFFERENT signal from the exits stays a pre-strip send. Channel Strip, bypassed EQ/Compressor
// and Master are unity at their defaults, so the rebuilt graph renders identically.
MakeChannelPlan planMakeChannel(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID source,
                                const MacroSet& macros) {
    MakeChannelPlan plan;
    if (processorFor(graph, source) == nullptr)
        return plan;
    const auto connections = graph.getConnections();

    const auto reach = reachFrom(graph, connections, source);
    if (!reach.reachesTerminal)
        return plan; // nothing downstream ever lands on an output — nothing to channel

    std::vector<NodeID> othersReach;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->nodeID != source && isTrackSourceNode(node->getProcessor()))
            for (const auto id : reachFrom(graph, connections, node->nodeID).nodes)
                addUniqueId(othersReach, id);
    if (containsId(othersReach, source))
        return plan; // `source` is itself downstream of another track: that chain is not its own

    std::vector<NodeID> own, shared;
    for (const auto id : reach.nodes) {
        if (containsId(othersReach, id))
            shared.push_back(id);
        else if (!isMacroPortNode(processorFor(graph, id)))
            own.push_back(id);
    }
    for (const auto id : own)
        if (isStrip(processorFor(graph, id)))
            return plan; // this track already has its own channel

    // The own region's outgoing edges: exits onto the output, and signal crossings into the rest.
    std::vector<Connection> exits, audioCrossings;
    std::vector<NodeID> crossingDests;
    for (const auto& c : connections) {
        if (!containsId(own, c.source.nodeID) || containsId(own, c.destination.nodeID))
            continue;
        auto* dst = processorFor(graph, c.destination.nodeID);
        if (dst == nullptr || isAttenuverter(dst))
            continue;
        if (isTerminal(dst)) {
            const int channel = c.destination.channelIndex;
            const bool isExit = dynamic_cast<MasterModule*>(dst) != nullptr
                                    ? (channel == MasterModule::kDirectLeft || channel == MasterModule::kDirectRight)
                                    : (channel == 0 || channel == 1);
            if (!c.source.isMIDI() && isExit)
                exits.push_back(c);
            continue;
        }
        if (!isSignalEdge(graph, connections, c))
            continue; // a CV cable into another chain stays a plain (auto-ported) edge
        addUniqueId(crossingDests, c.destination.nodeID);
        if (!c.source.isMIDI())
            audioCrossings.push_back(c);
    }

    // Which audio crossings the new strip takes over — see above. Grouped by destination pin, each
    // pin's feed set compared against the signal the strip will carry on that side.
    struct Pin {
        NodeAndChannel pin;
        bool isRight = false;
        std::vector<NodeAndChannel> feeds;
    };
    std::vector<Pin> pins;
    for (const auto& c : audioCrossings) {
        auto it = std::find_if(pins.begin(), pins.end(), [&](const Pin& p) { return p.pin == c.destination; });
        if (it == pins.end()) {
            pins.push_back({c.destination, crossingIsRight(graph, connections, c.destination), {}});
            it = pins.end() - 1;
        }
        addUniquePin(it->feeds, c.source);
    }
    std::vector<NodeAndChannel> wantLeft, wantRight;
    for (const auto& exit : exits)
        addUniquePin(exitIsRight(graph, exit) ? wantRight : wantLeft, exit.source);
    std::sort(wantLeft.begin(), wantLeft.end(), pinLess);
    std::sort(wantRight.begin(), wantRight.end(), pinLess);
    bool haveLeft = !exits.empty(), haveRight = !exits.empty();
    for (auto& pin : pins) {
        std::sort(pin.feeds.begin(), pin.feeds.end(), pinLess);
        auto& want = pin.isRight ? wantRight : wantLeft;
        bool& have = pin.isRight ? haveRight : haveLeft;
        if (!have) {
            want = pin.feeds; // no exits: the first pin on this side defines the strip's signal
            have = true;
        }
        if (pin.feeds == want) {
            for (const auto& c : audioCrossings)
                if (c.destination == pin.pin)
                    plan.stripCrossings.push_back(c);
        } else if (exits.empty()) {
            plan.refusal = "Can't make a channel: this track feeds shared modules from more than one point.";
        }
        // else: a different signal from the one reaching the output — stays a pre-strip send.
    }
    plan.exits = exits;

    // Merge heads: shared nodes this track's own region feeds (through any macro port nodes).
    std::vector<NodeID> heads;
    for (size_t i = 0; i < crossingDests.size(); ++i) {
        const auto id = crossingDests[i];
        if (isMacroPortNode(processorFor(graph, id))) {
            for (const auto& c : connections)
                if (c.source.nodeID == id && isSignalEdge(graph, connections, c))
                    addUniqueId(crossingDests, c.destination.nodeID);
        } else if (containsId(shared, id)) {
            addUniqueId(heads, id);
        }
    }

    // A bus per merge head that still reaches the output without a strip: the shared nodes from the
    // head down to (not including) the next strip or terminal.
    std::vector<NodeID> claimed;
    for (const auto head : heads) {
        if (containsId(claimed, head))
            continue;
        const auto busExits = findUnchanneledOutputFeeds(graph, head);
        if (busExits.empty())
            continue;
        MakeChannelPlan::Bus bus;
        bus.head = head;
        bus.members.push_back(head);
        for (size_t i = 0; i < bus.members.size(); ++i)
            for (const auto& c : connections) {
                if (c.source.nodeID != bus.members[i] || !isSignalEdge(graph, connections, c))
                    continue;
                auto* dst = processorFor(graph, c.destination.nodeID);
                if (isTerminal(dst) || isStrip(dst) || isMacroPortNode(dst) ||
                    !containsId(shared, c.destination.nodeID))
                    continue;
                addUniqueId(bus.members, c.destination.nodeID);
            }
        for (const auto& exit : busExits)
            if (!containsId(bus.members, exit.source.nodeID))
                plan.refusal = "Can't make a channel: the shared module's output runs through a macro.";
        for (const auto id : bus.members)
            claimed.push_back(id);
        plan.buses.push_back(bus);
    }

    const bool ownStrip = !plan.exits.empty() || !plan.stripCrossings.empty();
    plan.needsChannel = ownStrip || !plan.buses.empty();
    if (!plan.needsChannel)
        return plan;

    std::vector<NodeID> reachedByTracks = othersReach;
    for (const auto id : reach.nodes)
        addUniqueId(reachedByTracks, id);
    if (ownStrip) {
        for (const auto id : own)
            if (!isAudioInput(processorFor(graph, id)))
                plan.members.push_back(id);
        absorbSideInputs(graph, connections, plan.members, reachedByTracks, {});
    }
    for (auto& bus : plan.buses)
        absorbSideInputs(graph, connections, bus.members, reachedByTracks, plan.members);

    // Flat model (synth::Macro's class comment): a node can't be in two macros, so a node that would
    // move but is already grouped refuses the whole action rather than silently re-parenting it.
    auto inMacro = [&](NodeID id) {
        auto* node = graph.getNodeForId(id);
        const juce::String uuid = node != nullptr ? node->properties["uuid"].toString() : juce::String();
        return uuid.isNotEmpty() && macros.findByMember(uuid) != nullptr;
    };
    bool anyGrouped = std::any_of(plan.members.begin(), plan.members.end(), inMacro);
    for (const auto& bus : plan.buses)
        anyGrouped = anyGrouped || std::any_of(bus.members.begin(), bus.members.end(), inMacro);
    if (anyGrouped)
        plan.refusal = "Can't make a channel: part of this chain is already in a macro. Ungroup it first.";
    if (plan.refusal.isNotEmpty()) {
        // A refused plan carries only needsChannel + refusal — nothing a caller could half-build from.
        plan.members.clear();
        plan.exits.clear();
        plan.stripCrossings.clear();
        plan.buses.clear();
    }
    return plan;
}

// Rebuilds the own exits/crossings through EQ (bypassed) -> Compressor (bypassed) -> Channel Strip,
// whose output goes to Master's Mix for the exits (Master spliced exactly as buildChannelForFeeds
// does) and to the original shared input pins for the crossings — plain edges, never macro ports,
// see ChannelFlowsDefaultChannel.cpp's buildDefaultAudioChannel comment for why. Then builds each
// bus with buildChannelForFeeds' chain.
//
// A feed from a poly module's poly jack (isProcessorPoly, span > 1 — so a poly VCA, which self-sums
// to one channel, never qualifies) gets addVoiceMixerForPolyInstrument ahead of the strip instead
// (docs/mixer.md §5.4/§5.8). The one intended sound change: the channel then carries every voice,
// where a bare poly jack wired to a mono input carried voice 0 only.
//
// Assigns every member a uuid via AIStateMapper::ensureNodeUuid.
MadeChannel buildMakeChannel(juce::AudioProcessorGraph& graph, const MakeChannelPlan& plan,
                             const ChannelLayoutFn& layoutRightOf) {
    MadeChannel made;
    if (!plan.needsChannel || plan.refusal.isNotEmpty() || layoutRightOf == nullptr)
        return made;

    if (!plan.exits.empty() || !plan.stripCrossings.empty()) {
        auto members = ensureUuids(graph, plan.members);
        const auto built = buildChainFromEdges(graph, plan.exits, plan.stripCrossings, layoutRightOf);
        if (built.channel.stripUuid.isEmpty())
            return made; // a factory/addNode failure partway — same contract as buildDefaultAudioChannel
        members.insert(members.end(), built.voiceMixerUuids.begin(), built.voiceMixerUuids.end());
        members.push_back(built.channel.eqUuid);
        members.push_back(built.channel.compressorUuid);
        members.push_back(built.channel.stripUuid);
        made.channel = built.channel;
        made.memberUuids = members;
    }

    // Each bus AFTER the track's own chain: that chain may have just spliced Master, re-routing the
    // shared module's output onto Master's Direct input — which is exactly the exit the bus takes.
    for (const auto& bus : plan.buses) {
        const auto busExits = findUnchanneledOutputFeeds(graph, bus.head);
        if (busExits.empty())
            continue;
        auto members = ensureUuids(graph, bus.members);
        const auto built = buildChainFromEdges(graph, busExits, {}, layoutRightOf);
        if (built.channel.stripUuid.isEmpty())
            continue;
        members.insert(members.end(), built.voiceMixerUuids.begin(), built.voiceMixerUuids.end());
        members.push_back(built.channel.eqUuid);
        members.push_back(built.channel.compressorUuid);
        members.push_back(built.channel.stripUuid);
        // FRO15 (docs/mixer.md §5.15): a merge-point bus IS a bus — mark it so its mixer column
        // gets the BUS badge and a feeding-strips source line instead of a track chip.
        if (built.channel.strip != nullptr)
            if (auto* busStrip = dynamic_cast<ChannelStripModule*>(built.channel.strip->getProcessor()))
                busStrip->setIsBus(true);
        MadeChannel::Bus madeBus;
        madeBus.channel = built.channel;
        madeBus.headUuid = members.front();
        madeBus.memberUuids = members;
        made.buses.push_back(madeBus);
    }
    return made;
}

juce::AudioProcessorGraph::NodeID resolveChannelSource(juce::AudioProcessorGraph& graph,
                                                       const std::vector<juce::AudioProcessorGraph::NodeID>& nodes) {
    const auto connections = graph.getConnections();
    auto trackSourcesIn = [&](const std::vector<NodeID>& ids) {
        std::vector<NodeID> tracks;
        for (const auto id : ids)
            if (isTrackSourceNode(processorFor(graph, id)))
                addUniqueId(tracks, id);
        return tracks;
    };

    auto tracks = trackSourcesIn(nodes);
    if (!tracks.empty())
        return tracks.size() == 1 ? tracks.front() : NodeID{};

    // Upstream closure along signal edges (never out of Master/Rec Tap, never through modulation).
    std::vector<NodeID> upstream;
    for (const auto id : nodes)
        if (processorFor(graph, id) != nullptr)
            addUniqueId(upstream, id);
    auto signalPredecessors = [&](NodeID id) {
        std::vector<NodeID> preds;
        for (const auto& c : connections) {
            if (c.destination.nodeID != id || !isSignalEdge(graph, connections, c))
                continue;
            auto* src = processorFor(graph, c.source.nodeID);
            if (dynamic_cast<ModuleBase*>(src) == nullptr || isTerminal(src))
                continue;
            addUniqueId(preds, c.source.nodeID);
        }
        return preds;
    };
    for (size_t i = 0; i < upstream.size(); ++i)
        for (const auto pred : signalPredecessors(upstream[i]))
            addUniqueId(upstream, pred);

    tracks = trackSourcesIn(upstream);
    if (!tracks.empty())
        return tracks.size() == 1 ? tracks.front() : NodeID{};

    std::vector<NodeID> roots;
    for (const auto id : upstream) {
        auto* processor = processorFor(graph, id);
        if (dynamic_cast<ModuleBase*>(processor) == nullptr || isTerminal(processor) || isStrip(processor))
            continue;
        if (signalPredecessors(id).empty())
            roots.push_back(id);
    }
    return roots.size() == 1 ? roots.front() : NodeID{};
}

} // namespace synth
