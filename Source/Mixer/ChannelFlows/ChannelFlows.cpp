#include "ChannelFlows.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "MacroSet.h"
#include "Mixer/MasterSplice.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/RecordTapModule.h"
#include "Modules/VCAModule.h"
#include <algorithm>

namespace synth {

namespace {

// Creates one node through the factory (so it round-trips through graphToJSON/applyJSONToGraph,
// exactly like every other node-creation call site), assigns it a fresh uuid mirrored into the
// processor (ModuleBase::setNodeUuid), and records its canvas position. Returns nullptr on any
// factory/addNode failure, leaving `uuidOut` untouched.
juce::AudioProcessorGraph::Node* addChainNode(juce::AudioProcessorGraph& graph, const juce::String& moduleType,
                                              juce::Point<int> position, juce::String& uuidOut) {
    auto processor = AIStateMapper::createModule(moduleType);
    if (processor == nullptr)
        return nullptr;
    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return nullptr;

    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    node->properties.set("x", position.x);
    node->properties.set("y", position.y);

    uuidOut = uuid;
    return node.get();
}

// Sets a named juce::AudioParameterFloat's real-world value. Hand-rolled for the same reason
// isProcessorPoly is above: only needs to write one parameter by paramID, not AIStateMapper's full
// (private) param-matching idiom. No-op if the processor has no such float param.
void setFloatParam(juce::AudioProcessor& processor, const juce::String& paramID, float value) {
    for (auto* param : processor.getParameters())
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == paramID) {
                *floatParam = value;
                return;
            }
}

// Sets a named juce::AudioParameterBool. Same reasoning as setFloatParam above.
void setBoolParam(juce::AudioProcessor& processor, const juce::String& paramID, bool value) {
    for (auto* param : processor.getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == paramID) {
                *boolParam = value;
                return;
            }
}

// The shared builder behind both buildDefaultAudioChannel (one fixed stereo-pair source) and
// buildChannelForFeeds (T184: an arbitrary set of left/right feeds gathered from
// findUnchanneledOutputFeeds). Builds EQ(bypassed) -> Compressor(bypassed) -> Channel Strip
// (Stereo) -> Master (Mix) and wires every entry in `leftFeeds`/`rightFeeds` into the EQ's ch0/ch1
// respectively (AudioProcessorGraph sums multiple sources landing on the same input channel, so
// more than one feed a side is fine). Same ordering as buildDefaultAudioChannel's own contract:
// chain wired first, THEN spliceMasterNode, THEN Strip->Master as plain edges.
//
// FRO25 (P9-3d): `sink` says where the strip's output goes. The default (toMaster, no extra
// destinations) is every pre-FRO25 caller's behaviour. "Make channel" on a track that merges into a
// shared module sends the strip's L/R to that module's original input pins as well (or instead,
// toMaster=false, when the track has no output of its own) — plain edges, same reason as
// Strip->Master (see ChannelFlows.h).
struct ChannelSink {
    bool toMaster = true;
    std::vector<juce::AudioProcessorGraph::NodeAndChannel> leftDests, rightDests;
};

DefaultChannel buildChannelChain(juce::AudioProcessorGraph& graph,
                                 const std::vector<juce::AudioProcessorGraph::NodeAndChannel>& leftFeeds,
                                 const std::vector<juce::AudioProcessorGraph::NodeAndChannel>& rightFeeds,
                                 const DefaultChannelLayout& layout, const ChannelSink& sink = {}) {
    DefaultChannel result;

    juce::String eqUuid;
    auto* eq = addChainNode(graph, "Parametric EQ", layout.eq, eqUuid);
    if (eq == nullptr)
        return result;
    // Factory default: present but bypassed until the user opts in (docs/mixer.md §5.7/D3).
    if (auto* module = dynamic_cast<ModuleBase*>(eq->getProcessor()))
        module->setBypassed(true);

    juce::String compressorUuid;
    auto* compressor = addChainNode(graph, "Compressor", layout.compressor, compressorUuid);
    if (compressor == nullptr) {
        result.eqUuid = eqUuid;
        return result;
    }
    if (auto* module = dynamic_cast<ModuleBase*>(compressor->getProcessor()))
        module->setBypassed(true);

    // ChannelStripModule::setShape() must run BEFORE graph.addNode(): adding to a live graph can
    // prepareToPlay and lock the shape (see that method's own contract).
    auto stripProcessor = AIStateMapper::createModule("Channel Strip");
    if (stripProcessor == nullptr) {
        result.eqUuid = eqUuid;
        result.compressorUuid = compressorUuid;
        return result;
    }
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(stripProcessor.get()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    auto stripNode = graph.addNode(std::move(stripProcessor));
    if (stripNode == nullptr) {
        result.eqUuid = eqUuid;
        result.compressorUuid = compressorUuid;
        return result;
    }
    auto* strip = stripNode.get();
    const juce::String stripUuid = juce::Uuid().toDashedString();
    strip->properties.set("uuid", stripUuid);
    if (auto* module = dynamic_cast<ModuleBase*>(strip->getProcessor()))
        module->setNodeUuid(stripUuid);
    strip->properties.set("x", layout.strip.x);
    strip->properties.set("y", layout.strip.y);

    // feeds -> EQ. Stereo on raw ch0/ch1 throughout, except the strip's right leg, which is
    // ChannelStripModule::kRightBase — NEVER ch1 (Source/Modules/CLAUDE.md), see ChannelFlows.h's
    // own comment for why that's the one place the number jumps.
    for (const auto& feed : leftFeeds)
        graph.addConnection({feed, {eq->nodeID, 0}});
    for (const auto& feed : rightFeeds)
        graph.addConnection({feed, {eq->nodeID, 1}});
    graph.addConnection({{eq->nodeID, 0}, {compressor->nodeID, 0}});
    graph.addConnection({{eq->nodeID, 1}, {compressor->nodeID, 1}});
    graph.addConnection({{compressor->nodeID, 0}, {strip->nodeID, 0}});
    graph.addConnection({{compressor->nodeID, 1}, {strip->nodeID, ChannelStripModule::kRightBase}});

    // Master AFTER the chain above is wired, BEFORE the Strip->Master edges below: spliceMasterNode
    // re-routes whatever already feeds the audio output, and nothing of this channel's own should be
    // among that yet (see ChannelFlows.h's own comment on the ordering).
    auto* master = sink.toMaster ? spliceMasterNode(graph, layout.master) : findMasterNode(graph);
    if (master != nullptr && sink.toMaster) {
        // A PLAIN graph edge, never a macro port — see ChannelFlows.h's own comment for why.
        graph.addConnection({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}});
        graph.addConnection(
            {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}});
    }
    for (const auto& dest : sink.leftDests)
        graph.addConnection({{strip->nodeID, 0}, dest});
    for (const auto& dest : sink.rightDests)
        graph.addConnection({{strip->nodeID, ChannelStripModule::kRightBase}, dest});

    result.eqUuid = eqUuid;
    result.compressorUuid = compressorUuid;
    result.stripUuid = stripUuid;
    result.strip = strip;
    result.master = master;
    return result;
}

} // namespace

// True when `processor` declares a "poly" AudioParameterBool and it's currently on. Hand-rolled
// rather than reusing AIStateMapper::applyParamsToProcessor's param-matching idiom: that method is
// private to AIStateMapper.cpp, and this only needs to READ one parameter, not set arbitrary ones.
// Exposed (ChannelFlows.h) so a caller can pick the right builder before calling any of them.
bool isProcessorPoly(juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return false;
    for (auto* param : processor->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                return boolParam->get();
    return false;
}

void setProcessorPoly(juce::AudioProcessor* processor, bool poly) {
    if (processor != nullptr)
        setBoolParam(*processor, "poly", poly);
}

DefaultChannel buildDefaultAudioChannel(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node& source,
                                        const DefaultChannelLayout& layout, int sourceRightChannel) {
    return buildChannelChain(graph, {{source.nodeID, 0}}, {{source.nodeID, sourceRightChannel}}, layout);
}

juce::AudioProcessorGraph::Node* addVoiceMixerForPolyInstrument(juce::AudioProcessorGraph& graph,
                                                                juce::AudioProcessorGraph::Node& instrument,
                                                                juce::Point<int> position, juce::String& uuidOut) {
    if (!isProcessorPoly(instrument.getProcessor()))
        return nullptr;

    auto* voiceMixer = addChainNode(graph, "Voice Mixer", position, uuidOut);
    if (voiceMixer == nullptr)
        return nullptr;

    // The instrument's L-octet (raw ch0-7, poly ON) is up to 8 simultaneous voices; Voice Mixer sums
    // them to a mono value duplicated onto its own ch0(L)/ch1(R) (docs/mixer.md §5.4/§5.8). The
    // instrument's R-octet is deliberately NOT summed in — same precedent, same known limitation.
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{instrument.nodeID, voice}, {voiceMixer->nodeID, voice}});

    return voiceMixer;
}

EnvelopeAndVCA addEnvelopeAndVCAForRawInstrument(juce::AudioProcessorGraph& graph,
                                                 juce::AudioProcessorGraph::Node& trackIn,
                                                 juce::AudioProcessorGraph::Node& chainSource,
                                                 int chainSourceRightChannel, juce::Point<int> adsrPosition,
                                                 juce::Point<int> vcaPosition) {
    EnvelopeAndVCA result;

    juce::String adsrUuid;
    auto* adsr = addChainNode(graph, "ADSR", adsrPosition, adsrUuid);
    if (adsr == nullptr)
        return result;
    // Forced non-poly regardless of the instrument's own poly flag — see this function's header
    // comment for why a poly ADSR fed only Track In's MIDI would never fire.
    setBoolParam(*adsr->getProcessor(), "poly", false);
    // Overrides ADSR's stock sustain default (0.0) so a held note sustains instead of
    // plucking-and-dying after ~0.25s — see the header comment.
    setFloatParam(*adsr->getProcessor(), "sustain", 0.7f);

    juce::String vcaUuid;
    auto* vca = addChainNode(graph, "VCA", vcaPosition, vcaUuid);
    if (vca == nullptr) {
        result.adsrUuid = adsrUuid;
        return result;
    }
    setBoolParam(*vca->getProcessor(), "poly", false);
    // Overrides VCA's stock gain default (0.5) so the envelope alone governs level — see the
    // header comment.
    setFloatParam(*vca->getProcessor(), "gain", 1.0f);

    // Track In's MIDI, fanned alongside its existing wire to the instrument, drives the ADSR's gate.
    graph.addConnection({{trackIn.nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {adsr->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    // chainSource L/R -> VCA Audio L/R (VCAModule::kRightBase — never ch1, that's the Gain CV).
    graph.addConnection({{chainSource.nodeID, 0}, {vca->nodeID, 0}});
    graph.addConnection({{chainSource.nodeID, chainSourceRightChannel}, {vca->nodeID, VCAModule::kRightBase}});

    // ADSR Env (ch0) -> VCA's mono Gain CV (ch1).
    graph.addConnection({{adsr->nodeID, 0}, {vca->nodeID, 1}});

    result.adsrUuid = adsrUuid;
    result.vcaUuid = vcaUuid;
    result.vca = vca;
    return result;
}

PolyEnvelopeAndVCA addPolyEnvelopeAndVCAForInstrument(juce::AudioProcessorGraph& graph,
                                                      juce::AudioProcessorGraph::Node& trackIn,
                                                      juce::AudioProcessorGraph::Node& instrument,
                                                      juce::Point<int> polyMidiPosition, juce::Point<int> adsrPosition,
                                                      juce::Point<int> vcaPosition) {
    PolyEnvelopeAndVCA result;

    juce::String polyMidiUuid;
    auto* polyMidi = addChainNode(graph, "Poly MIDI", polyMidiPosition, polyMidiUuid);
    if (polyMidi == nullptr)
        return result;

    juce::String adsrUuid;
    auto* adsr = addChainNode(graph, "ADSR", adsrPosition, adsrUuid);
    if (adsr == nullptr) {
        result.polyMidiUuid = polyMidiUuid;
        return result;
    }
    // Poly, unlike addEnvelopeAndVCAForRawInstrument's forced-mono ADSR — its gate comes from Poly
    // MIDI's per-voice CV below, not raw MIDI, so the poly branch actually fires. See this
    // function's header comment for why the non-poly path can't do this.
    setBoolParam(*adsr->getProcessor(), "poly", true);
    // Same override as the non-poly path, same reason: a held note should sustain.
    setFloatParam(*adsr->getProcessor(), "sustain", 0.7f);

    juce::String vcaUuid;
    auto* vca = addChainNode(graph, "VCA", vcaPosition, vcaUuid);
    if (vca == nullptr) {
        result.polyMidiUuid = polyMidiUuid;
        result.adsrUuid = adsrUuid;
        return result;
    }
    setBoolParam(*vca->getProcessor(), "poly", true);
    // Same override as the non-poly path, same reason: the envelope alone should govern level.
    setFloatParam(*vca->getProcessor(), "gain", 1.0f);

    // Track In's MIDI drives Poly MIDI, not a mono ADSR gate fallback.
    graph.addConnection({{trackIn.nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {polyMidi->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    // Poly MIDI's Pitch fan (ch0-7) -> instrument's poly Pitch CV in (ch0-7).
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{polyMidi->nodeID, voice}, {instrument.nodeID, voice}});

    // Poly MIDI's Gate fan (ch8-15) -> ADSR's poly Gate CV in (ch0-7).
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{polyMidi->nodeID, 8 + voice}, {adsr->nodeID, voice}});

    // ADSR's poly Env out (ch0-7) -> VCA's poly Gain CV in (ch8-15, VCAModule::kPolyCVBase).
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{adsr->nodeID, voice}, {vca->nodeID, VCAModule::kPolyCVBase + voice}});

    // instrument's poly Audio L (ch0-7) -> VCA's poly Audio L in (ch0-7). The R-octet is
    // deliberately not wired — see this function's header comment ("known limitation").
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{instrument.nodeID, voice}, {vca->nodeID, voice}});

    result.polyMidiUuid = polyMidiUuid;
    result.adsrUuid = adsrUuid;
    result.vcaUuid = vcaUuid;
    result.vca = vca;
    return result;
}

std::vector<juce::AudioProcessorGraph::Connection> findUnchanneledOutputFeeds(juce::AudioProcessorGraph& graph,
                                                                              juce::AudioProcessorGraph::NodeID start) {
    std::vector<juce::AudioProcessorGraph::Connection> exits;

    std::vector<juce::AudioProcessorGraph::NodeID> visited{start};
    std::vector<juce::AudioProcessorGraph::NodeID> queue{start};

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        auto* node = graph.getNodeForId(nodeId);
        if (node == nullptr)
            continue;
        auto* processor = node->getProcessor();
        if (processor == nullptr)
            continue;

        // Never expand PAST a terminal, an already-channeled branch, or a hidden modulation hop —
        // see the header comment for why each of these stops traversal here.
        if (dynamic_cast<RecordTapModule*>(processor) != nullptr || dynamic_cast<MasterModule*>(processor) != nullptr ||
            dynamic_cast<ChannelStripModule*>(processor) != nullptr ||
            dynamic_cast<AttenuverterModule*>(processor) != nullptr || processor->getName() == "Audio Output")
            continue;

        for (const auto& conn : graph.getConnections()) {
            if (conn.source.nodeID != nodeId)
                continue;

            auto* destNode = graph.getNodeForId(conn.destination.nodeID);
            if (destNode == nullptr)
                continue;
            auto* destProcessor = destNode->getProcessor();
            if (destProcessor == nullptr)
                continue;

            // A hidden modulation hop: never traversed, never an exit (see header comment).
            if (dynamic_cast<AttenuverterModule*>(destProcessor) != nullptr)
                continue;

            const int channel = conn.destination.channelIndex;

            if (destProcessor->getName() == "Audio Output") {
                if (channel == 0 || channel == 1)
                    exits.push_back(conn);
                continue; // terminal — never expand past Audio Output
            }
            if (dynamic_cast<RecordTapModule*>(destProcessor) != nullptr) {
                if (channel == 0 || channel == 1)
                    exits.push_back(conn);
                continue; // terminal — never expand past Rec Tap
            }
            if (dynamic_cast<MasterModule*>(destProcessor) != nullptr) {
                // kMixLeft/kMixRight are NOT an exit — only a strip's own output can land there,
                // and this BFS never reaches one (it stops at a ChannelStripModule below).
                if (channel == MasterModule::kDirectLeft || channel == MasterModule::kDirectRight)
                    exits.push_back(conn);
                continue; // terminal — never expand past Master
            }
            if (dynamic_cast<ChannelStripModule*>(destProcessor) != nullptr)
                continue; // already channeled — do not expand past it, and not an exit itself

            if (std::find(visited.begin(), visited.end(), conn.destination.nodeID) == visited.end()) {
                visited.push_back(conn.destination.nodeID);
                queue.push_back(conn.destination.nodeID);
            }
        }
    }

    return exits;
}

DefaultChannel buildChannelForFeeds(juce::AudioProcessorGraph& graph,
                                    const std::vector<juce::AudioProcessorGraph::Connection>& exits,
                                    const DefaultChannelLayout& layout) {
    if (exits.empty())
        return {};

    // Classify by DESTINATION channel before anything is removed — ch0/kDirectLeft -> Left,
    // ch1/kDirectRight -> Right (the only two channel numbers findUnchanneledOutputFeeds ever
    // returns an exit for).
    std::vector<juce::AudioProcessorGraph::NodeAndChannel> leftFeeds, rightFeeds;
    for (const auto& exit : exits) {
        const int channel = exit.destination.channelIndex;
        const bool isRight = (channel == 1) || (channel == MasterModule::kDirectRight);
        (isRight ? rightFeeds : leftFeeds).push_back(exit.source);
    }

    // REMOVE FIRST, then build — same "collect, then mutate" reasoning spliceMasterNode's own
    // splice uses (removeConnection while iterating the list it came from would invalidate it).
    for (const auto& exit : exits)
        graph.removeConnection(exit);

    return buildChannelChain(graph, leftFeeds, rightFeeds, layout);
}

// ---- FRO25 (P9-3d): "Make channel" --------------------------------------------------------------

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

juce::AudioProcessor* processorFor(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? node->getProcessor() : nullptr;
}

bool isAttenuverter(const juce::AudioProcessor* p) { return dynamic_cast<const AttenuverterModule*>(p) != nullptr; }

bool isStrip(const juce::AudioProcessor* p) { return dynamic_cast<const ChannelStripModule*>(p) != nullptr; }

// findUnchanneledOutputFeeds' own terminal set: Audio Output, Record Tap, Master.
bool isTerminal(const juce::AudioProcessor* p) {
    return p != nullptr && (dynamic_cast<const RecordTapModule*>(p) != nullptr ||
                            dynamic_cast<const MasterModule*>(p) != nullptr || p->getName() == "Audio Output");
}

bool isMacroPortNode(const juce::AudioProcessor* p) {
    auto* module = dynamic_cast<const ModuleBase*>(p);
    if (module == nullptr)
        return false;
    const auto type = module->getModuleType();
    return type == ModuleType::MacroInlet || type == ModuleType::MacroOutlet || type == ModuleType::MacroMidiInlet ||
           type == ModuleType::MacroMidiOutlet;
}

// The device-input singleton (GraphEditor::isSingletonIOModule): walked through like any other hop
// so its exits are still found, but never boxed into a channel macro.
bool isAudioInput(const juce::AudioProcessor* p) { return p != nullptr && p->getName() == "Audio Input"; }

// planMakeChannel's signal-edge rule (ChannelFlows.h): every MIDI edge, and every audio edge that
// neither touches a hidden modulation attenuverter nor lands on a PortRole::ModCV pin.
// The pin a cable into `pin` ultimately lands on, looking through macro port nodes (each a pure
// per-channel pass-through). An auto-created port reports a plain Audio role and no stereo side of
// its own, so both the ModCV check below and crossingIsRight's L/R read must use the module behind
// it — a Cutoff CV cable into a bus through its port is still a CV cable.
NodeAndChannel resolveThroughPorts(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections,
                                   NodeAndChannel pin) {
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

bool isSignalEdge(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections, const Connection& c) {
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

// Side-input absorption (ChannelFlows.h), to a fixpoint: a node no track reaches whose every
// consumer is already in `members` joins it. `excluded` is a set already claimed by another channel.
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

bool isTrackSourceNode(const juce::AudioProcessor* processor) {
    auto* module = dynamic_cast<const ModuleBase*>(processor);
    if (module == nullptr)
        return false;
    const auto type = module->getModuleType();
    return type == ModuleType::TimelineMidiSource || type == ModuleType::TimelineAudioSource;
}

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

    // Which audio crossings the new strip takes over — see ChannelFlows.h. Grouped by destination
    // pin, each pin's feed set compared against the signal the strip will carry on that side.
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
