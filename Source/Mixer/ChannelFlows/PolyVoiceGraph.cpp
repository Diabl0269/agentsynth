// Concern: the whole-voice-graph Poly switch -- which modules a Poly click on one module switches, and the model-side
// change (parameter flips plus the Poly MIDI node that makes a poly Oscillator/ADSR playable from a MIDI source).
// The click, the confirmation and the undo step are UI (UI/Graph/PolyChain).
#include "PolyVoiceGraph.h"

#include "AudioEngine/ConnectionIndex.h"
#include "ChannelFlowsInternal.h"
#include "MacroSet.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/RecordTapModule.h"
#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace synth {

namespace {

using Graph = juce::AudioProcessorGraph;
using NodeID = Graph::NodeID;
using Connection = Graph::Connection;

juce::AudioParameterBool* polyParameter(juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return nullptr;
    for (auto* param : processor->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                return boolParam;
    return nullptr;
}

bool isType(const juce::AudioProcessor* processor, ModuleType type) {
    auto* module = dynamic_cast<const ModuleBase*>(processor);
    return module != nullptr && module->getModuleType() == type;
}

// Where a voice graph (and a track's reach) ends: the node is reached, never expanded. A Gate/EQ/Compressor is the
// start of a channel's insert chain, a strip/Master/tap/output the end of one, a track source the start of a track.
bool isBoundary(const juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return true;
    if (isStrip(processor) || isTrackSourceNode(processor))
        return true;
    if (dynamic_cast<const MasterModule*>(processor) != nullptr ||
        dynamic_cast<const RecordTapModule*>(processor) != nullptr)
        return true;
    // The audio sink is the end of a chain; the graph's MIDI input is a source feeding any number of chains, like MIDI
    // In.
    if (auto* io = dynamic_cast<const Graph::AudioGraphIOProcessor*>(processor))
        return io->getType() != Graph::AudioGraphIOProcessor::midiInputNode;
    return isType(processor, ModuleType::Gate) || isType(processor, ModuleType::ParametricEQ) ||
           isType(processor, ModuleType::Compressor);
}

bool isSidechainEdge(Graph& graph, const Connection& c) {
    if (c.source.isMIDI())
        return false;
    auto* module = dynamic_cast<ModuleBase*>(processorFor(graph, c.destination.nodeID));
    return module != nullptr && module->mapInputChannel(c.destination.channelIndex).role == PortRole::Sidechain;
}

bool isPitchModule(const juce::AudioProcessor* processor) {
    return isType(processor, ModuleType::Oscillator) || isType(processor, ModuleType::Wavetable);
}

bool isPolyMidi(const juce::AudioProcessor* processor) { return isType(processor, ModuleType::PolyMidi); }

juce::String uuidOfNode(Graph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

// Where a track's forward reach ends: a Channel Strip (another channel's terminus), Master, a Record Tap, an output.
bool endsTrackReach(const juce::AudioProcessor* processor) {
    return processor == nullptr || isStrip(processor) || dynamic_cast<const MasterModule*>(processor) != nullptr ||
           dynamic_cast<const RecordTapModule*>(processor) != nullptr ||
           dynamic_cast<const Graph::AudioGraphIOProcessor*>(processor) != nullptr;
}

// The nodes a track's source reaches going forward over every cable (its own Gate/EQ/Compressor included), the source
// itself included, stopping at a strip without expanding it.
std::set<NodeID> reachFrom(Graph& graph, const ConnectionIndex& index, NodeID source) {
    std::set<NodeID> reached{source};
    std::deque<NodeID> queue{source};
    while (!queue.empty()) {
        const auto id = queue.front();
        queue.pop_front();
        for (const auto& c : index.outOf(id)) {
            if (isSidechainEdge(graph, c) || !reached.insert(c.destination.nodeID).second)
                continue;
            if (!endsTrackReach(processorFor(graph, c.destination.nodeID)))
                queue.push_back(c.destination.nodeID);
        }
    }
    return reached;
}

} // namespace

int& polyParameterReadsForTest() noexcept {
    static int reads = 0;
    return reads;
}

bool hasPolyParameter(const juce::AudioProcessor* processor) {
    ++polyParameterReadsForTest();
    return polyParameter(const_cast<juce::AudioProcessor*>(processor)) != nullptr;
}

int& polyTrackOwnerPassesForTest() noexcept {
    static int passes = 0;
    return passes;
}

PolyTrackOwners polyTrackOwners(Graph& graph, const ConnectionIndex& index, const std::vector<PolyTrackRef>& tracks) {
    ++polyTrackOwnerPassesForTest();
    std::map<juce::String, NodeID> byUuid; // the first node carrying each uuid, as a scan in node order finds it
    for (auto* node : graph.getNodes())
        if (const auto uuid = node->properties["uuid"].toString(); uuid.isNotEmpty())
            byUuid.emplace(uuid, node->nodeID);
    PolyTrackOwners owners;
    for (int t = 0; t < (int)tracks.size(); ++t)
        if (const auto found = byUuid.find(tracks[(size_t)t].sourceUuid); found != byUuid.end())
            for (const auto id : reachFrom(graph, index, found->second))
                owners[id].insert(t);
    return owners;
}

PolyVoiceGraphPlan planPolyVoiceGraph(Graph& graph, NodeID clicked, const std::vector<PolyTrackRef>& tracks) {
    if (graph.getNodeForId(clicked) == nullptr)
        return {};
    const ConnectionIndex index(graph);
    return planPolyVoiceGraph(graph, clicked, tracks, index, polyTrackOwners(graph, index, tracks));
}

// Every track header plans its own voice graph on each reconcile, and a voice graph that reaches a shared modulator
// spans most of the project: asking each node's parameters for a Poly switch (a cast per parameter) per plan made
// the reconcile grow with tracks x nodes. The pass's facts answer each node once.
const PolyNodeFacts::Facts& PolyNodeFacts::of(Graph& graph, NodeID id) {
    if (const auto found = facts_.find(id); found != facts_.end())
        return found->second;
    auto* processor = processorFor(graph, id);
    return facts_[id] = Facts{hasPolyParameter(processor), isBoundary(processor)};
}

bool PolyNodeFacts::hasPoly(Graph& graph, NodeID id) { return of(graph, id).hasPoly; }

bool PolyNodeFacts::endsVoiceGraph(Graph& graph, NodeID id) { return of(graph, id).boundary; }

PolyVoiceGraphPlan planPolyVoiceGraph(Graph& graph, NodeID clicked, const std::vector<PolyTrackRef>& tracks,
                                      const ConnectionIndex& index, const PolyTrackOwners& owners,
                                      PolyNodeFacts* facts) {
    PolyVoiceGraphPlan plan;
    if (graph.getNodeForId(clicked) == nullptr)
        return plan;
    PolyNodeFacts ownFacts;
    auto& known = facts != nullptr ? *facts : ownFacts;

    // The voice graph: undirected, reaching but not expanding boundaries.
    std::vector<NodeID> voiceGraph{clicked};
    std::set<NodeID> seen{clicked};
    for (size_t head = 0; head < voiceGraph.size(); ++head) {
        const auto id = voiceGraph[head];
        if (id != clicked && known.endsVoiceGraph(graph, id))
            continue;
        for (const auto& c : index.touching(id)) {
            if (isSidechainEdge(graph, c))
                continue;
            const auto other = c.source.nodeID == id ? c.destination.nodeID : c.source.nodeID;
            if (seen.insert(other).second)
                voiceGraph.push_back(other);
        }
    }

    // Track ownership (`owners`): node -> the tracks (by index) whose source reaches it.
    const auto clickedOwners = owners.find(clicked);
    const std::set<int> clickedTracks = clickedOwners != owners.end() ? clickedOwners->second : std::set<int>{};
    std::set<int> otherTracks;
    for (const auto id : voiceGraph) {
        if (!known.hasPoly(graph, id))
            continue;
        const auto found = owners.find(id);
        const bool ownedElsewhere =
            found != owners.end() && !found->second.empty() &&
            std::none_of(found->second.begin(), found->second.end(), [&](int t) { return clickedTracks.count(t) > 0; });
        if (id == clicked || !ownedElsewhere) {
            plan.thisTrackNodes.push_back(id);
            continue;
        }
        plan.otherTrackNodes.push_back(id);
        otherTracks.insert(found->second.begin(), found->second.end());
    }
    for (const int t : otherTracks)
        plan.otherTrackNames.push_back(tracks[(size_t)t].name.isNotEmpty() ? tracks[(size_t)t].name
                                                                           : "Track " + juce::String(t + 1));
    return plan;
}

juce::String describeTrackNames(const std::vector<juce::String>& names) {
    if (names.empty())
        return {};
    if (names.size() == 1)
        return names.front();
    juce::StringArray head;
    for (size_t i = 0; i + 1 < names.size(); ++i)
        head.add(names[i]);
    return head.joinIntoString(", ") + " and " + names.back();
}

namespace {

// One MIDI source feeding raw MIDI straight into modules that just went poly.
struct SourceGroup {
    NodeID source;
    std::vector<NodeID> modules;
};

std::vector<SourceGroup> midiSourcesFeeding(const ConnectionIndex& index, const std::vector<NodeID>& modules) {
    std::vector<SourceGroup> groups;
    for (const auto module : modules)
        for (const auto& c : index.into(module)) {
            if (!c.source.isMIDI())
                continue;
            auto found = std::find_if(groups.begin(), groups.end(),
                                      [&](const SourceGroup& g) { return g.source == c.source.nodeID; });
            if (found == groups.end())
                groups.push_back({c.source.nodeID, {}});
            found = std::find_if(groups.begin(), groups.end(),
                                 [&](const SourceGroup& g) { return g.source == c.source.nodeID; });
            if (std::find(found->modules.begin(), found->modules.end(), module) == found->modules.end())
                found->modules.push_back(module);
        }
    return groups;
}

// Modules that share voices and every MIDI source feeding any of them.
struct VoiceGroup {
    std::vector<NodeID> sources;
    std::vector<NodeID> modules;
};

// Source groups that feed a common module become one group: two Poly MIDI nodes fanning into the same voice jacks
// would run two voice allocators onto one set of voices (pitch CVs summing, gates fighting), so a module fed by a
// sequencer and a track's input gets ONE Poly MIDI that both sources feed.
std::vector<VoiceGroup> sharedVoiceGroups(const std::vector<SourceGroup>& groups) {
    std::vector<VoiceGroup> merged;
    for (const auto& group : groups) {
        VoiceGroup next{{group.source}, group.modules};
        for (auto it = merged.begin(); it != merged.end();) {
            const bool shares = std::any_of(it->modules.begin(), it->modules.end(), [&](NodeID m) {
                return std::find(next.modules.begin(), next.modules.end(), m) != next.modules.end();
            });
            if (!shares) {
                ++it;
                continue;
            }
            next.sources.insert(next.sources.begin(), it->sources.begin(), it->sources.end());
            for (const auto m : it->modules)
                if (std::find(next.modules.begin(), next.modules.end(), m) == next.modules.end())
                    next.modules.push_back(m);
            it = merged.erase(it);
        }
        merged.push_back(std::move(next));
    }
    return merged;
}

// The Poly MIDI node `source`'s MIDI already feeds, or an invalid id.
NodeID existingPolyMidiFedBy(Graph& graph, const ConnectionIndex& index, NodeID source) {
    for (const auto& c : index.outOf(source))
        if (c.source.isMIDI() && isPolyMidi(processorFor(graph, c.destination.nodeID)))
            return c.destination.nodeID;
    return {};
}

NodeID addPolyMidiNode(Graph& graph, NodeID source, NodeID firstFed, const PolyVoiceGraphOptions& options) {
    juce::Point<int> position;
    if (auto* fed = graph.getNodeForId(firstFed))
        position = {(int)fed->properties.getWithDefault("x", 0) - 320, (int)fed->properties.getWithDefault("y", 0)};
    juce::String uuid;
    auto* node =
        addChainNode(graph, "Poly MIDI", {juce::jmax(0, position.x & ~7), juce::jmax(0, position.y & ~7)}, uuid);
    if (node == nullptr)
        return {};
    if (options.leavePolyMidiUnplaced) {
        node->properties.remove("x");
        node->properties.remove("y");
    }
    graph.addConnection({{source, Graph::midiChannelIndex}, {node->nodeID, Graph::midiChannelIndex}});
    return node->nodeID;
}

void wirePolyMidi(Graph& graph, NodeID polyMidi, const std::vector<NodeID>& modules) {
    for (const auto module : modules) {
        auto* processor = processorFor(graph, module);
        const bool isAdsr = isType(processor, ModuleType::ADSR);
        for (int voice = 0; voice < 8; ++voice)
            graph.addConnection({{polyMidi, (isAdsr ? 8 : 0) + voice}, {module, voice}});
        // An oscillator also takes the Gate fan, so a new note restarts its phase even when the pitch
        // never drops between two notes on a voice.
        const auto* moduleBase = dynamic_cast<ModuleBase*>(processor);
        const int gateBase = moduleBase != nullptr ? moduleBase->polyGateInputChannel() : -1;
        if (gateBase >= 0)
            for (int voice = 0; voice < 8; ++voice)
                graph.addConnection({{polyMidi, 8 + voice}, {module, gateBase + voice}});
    }
}

// A poly ADSR takes its gates from the Poly MIDI fan and never reads the raw MIDI at its MIDI input
// (ADSRModule::processModuleBlock scans midiMessages only in its mono branch), so the direct `source -> ADSR` MIDI
// cable is redundant once Poly MIDI feeds it and goes. Only that exact cable, only for an ADSR, only when Poly MIDI
// really is wired in: the same source's cable to a mono module is another cable and stays. An Oscillator or Wavetable
// is NOT dropped here: poly voice 0 still restarts on a raw note-on and falls back to the raw note's pitch
// (OscillatorModule::scanMidi, WavetableOscillatorModule::scanMidi), so its raw cable is not provably redundant.
// restoreRawMidi puts the cable back when the module goes mono again.
void dropRedundantRawMidi(Graph& graph, NodeID source, NodeID polyMidi, const std::vector<NodeID>& modules) {
    for (const auto module : modules) {
        if (!isType(processorFor(graph, module), ModuleType::ADSR))
            continue;
        if (!graph.isConnected({{polyMidi, 8}, {module, 0}}))
            continue;
        graph.removeConnection({{source, Graph::midiChannelIndex}, {module, Graph::midiChannelIndex}});
    }
}

void insertPolyMidi(Graph& graph, const std::vector<NodeID>& flipped, const PolyVoiceGraphOptions& options,
                    PolyVoiceGraphResult& result) {
    std::vector<NodeID> wantsMidi;
    for (const auto id : flipped) {
        auto* processor = processorFor(graph, id);
        if (isPitchModule(processor) || isType(processor, ModuleType::ADSR))
            wantsMidi.push_back(id);
    }
    const ConnectionIndex index(graph);
    for (const auto& voices : sharedVoiceGroups(midiSourcesFeeding(index, wantsMidi))) {
        NodeID polyMidi;
        for (const auto source : voices.sources)
            if (polyMidi.uid == 0)
                polyMidi = existingPolyMidiFedBy(graph, index, source);
        if (polyMidi.uid == 0) {
            polyMidi = addPolyMidiNode(graph, voices.sources.front(), voices.modules.front(), options);
            if (polyMidi.uid == 0)
                continue;
            result.addedPolyMidi.push_back(polyMidi);
        }
        for (const auto source : voices.sources) // MIDI merges at Poly MIDI's input: one voice allocator for all
            graph.addConnection({{source, Graph::midiChannelIndex}, {polyMidi, Graph::midiChannelIndex}});
        wirePolyMidi(graph, polyMidi, voices.modules);
        for (const auto source : voices.sources)
            dropRedundantRawMidi(graph, source, polyMidi, voices.modules);
    }
}

// Poly OFF, step one (before the parameters flip): drops every Poly MIDI -> `modules` cable. Returns, per Poly MIDI,
// the modules it fed, so step two can give them raw MIDI back.
std::map<NodeID, std::vector<NodeID>> detachPolyMidi(Graph& graph, const std::vector<NodeID>& modules) {
    std::map<NodeID, std::vector<NodeID>> fed;
    for (const auto& c : graph.getConnections()) {
        if (!isPolyMidi(processorFor(graph, c.source.nodeID)) ||
            std::find(modules.begin(), modules.end(), c.destination.nodeID) == modules.end())
            continue;
        graph.removeConnection(c);
        auto& list = fed[c.source.nodeID];
        if (std::find(list.begin(), list.end(), c.destination.nodeID) == list.end())
            list.push_back(c.destination.nodeID);
    }
    return fed;
}

void restoreRawMidi(Graph& graph, const std::map<NodeID, std::vector<NodeID>>& fed,
                    const PolyVoiceGraphOptions& options, PolyVoiceGraphResult& result) {
    std::vector<NodeID> orphans;
    for (const auto& [polyMidi, modules] : fed) {
        for (const auto& c : graph.getConnections())
            if (c.destination.nodeID == polyMidi && c.source.isMIDI())
                for (const auto module : modules) {
                    auto* processor = processorFor(graph, module);
                    if (isPitchModule(processor) || isType(processor, ModuleType::ADSR))
                        graph.addConnection(
                            {{c.source.nodeID, Graph::midiChannelIndex}, {module, Graph::midiChannelIndex}});
                }
        const auto all = graph.getConnections();
        if (std::none_of(all.begin(), all.end(), [&](const Connection& c) { return c.source.nodeID == polyMidi; }))
            orphans.push_back(polyMidi);
    }
    if (orphans.empty())
        return;
    result.removedPolyMidi = orphans;
    if (options.removeNodes) {
        options.removeNodes(orphans);
        return;
    }
    for (const auto id : orphans) {
        if (options.macros != nullptr)
            options.macros->removeMemberEverywhere(uuidOfNode(graph, id));
        graph.removeNode(id);
    }
}

} // namespace

PolyVoiceGraphResult applyPolyVoiceGraph(Graph& graph, const std::vector<NodeID>& nodes, bool poly,
                                         const PolyVoiceGraphOptions& options) {
    PolyVoiceGraphResult result;
    std::vector<NodeID> changing;
    for (const auto id : nodes)
        if (auto* param = polyParameter(processorFor(graph, id)); param != nullptr && param->get() != poly)
            changing.push_back(id);

    std::map<NodeID, std::vector<NodeID>> detached;
    if (!poly)
        detached = detachPolyMidi(graph, changing);

    for (const auto id : changing) {
        *polyParameter(processorFor(graph, id)) = poly;
        result.flipped.push_back(id);
    }

    if (poly)
        insertPolyMidi(graph, result.flipped, options, result);
    else
        restoreRawMidi(graph, detached, options, result);
    return result;
}

} // namespace synth
