// Concern: modulation routing — the attenuverter-chain/DirectCV/PolyBus routing model exposed to the Mod Matrix and
// cable rendering, plus module auto-naming.

#include "AudioEngine.h"
#include "Modules/AttenuverterModule.h"
#include <algorithm>
#include <map>
#include <set>

std::vector<AudioEngine::ModulationRouting> AudioEngine::getModulationRoutings() const {
    std::vector<ModulationRouting> routings;

    // --- Pass 1: AttenuverterChain routings (unchanged) ---
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* atten = dynamic_cast<AttenuverterModule*>(node->getProcessor())) {
            ModulationRouting r;
            r.kind = RoutingKind::AttenuverterChain;
            r.attenuverterNodeID = node->nodeID;
            r.voiceCount = 1;
            r.role = PortRole::ModCV;

            // Find source: first connection whose destination is (node, channel 0)
            for (auto& conn : mainProcessorGraph.getConnections()) {
                if (conn.destination.nodeID == node->nodeID && conn.destination.channelIndex == 0) {
                    r.sourceNodeID = conn.source.nodeID;
                    r.sourceChannelIndex = conn.source.channelIndex;
                    r.hasSource = true;
                    break;
                }
            }

            // Find dest: first connection whose source is (node, channel 0)
            for (auto& conn : mainProcessorGraph.getConnections()) {
                if (conn.source.nodeID == node->nodeID && conn.source.channelIndex == 0) {
                    r.destNodeID = conn.destination.nodeID;
                    r.destChannelIndex = conn.destination.channelIndex;
                    r.hasDest = true;
                    break;
                }
            }

            // Bypass state
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                r.isBypassed = module->isBypassed();

            // Signal visualization values
            r.modSignalValue = atten->getLastModValue();
            r.modSignalPeak = atten->getLastOutputPeak();

            // sourceVisibleJack / destVisibleJack: left as 0 (not consumed yet)

            routings.push_back(r);
        }
    }

    // --- Pass 2: DirectCV / PolyBus routings ---
    // Helper: read a peak value from a VisualBuffer by scanning its contents
    auto readVisualPeak = [](VisualBuffer* vb) -> float {
        if (!vb)
            return 0.0f;
        std::vector<float> buf(static_cast<size_t>(vb->getSize()));
        vb->copyTo(buf);
        float peak = 0.0f;
        for (float s : buf)
            peak = std::max(peak, std::abs(s));
        return peak;
    };

    // Collect candidate direct edges: connections where neither endpoint is an AttenuverterModule
    // and the destination channel maps to role == ModCV on the dest module.
    struct CandidateEdge {
        juce::AudioProcessorGraph::NodeID srcNodeID;
        int srcChannel;
        juce::AudioProcessorGraph::NodeID dstNodeID;
        int dstChannel;
    };
    std::vector<CandidateEdge> candidates;

    for (auto& conn : mainProcessorGraph.getConnections()) {
        if (conn.source.isMIDI())
            continue;

        auto* srcNode = mainProcessorGraph.getNodeForId(conn.source.nodeID);
        auto* dstNode = mainProcessorGraph.getNodeForId(conn.destination.nodeID);
        if (!srcNode || !dstNode)
            continue;

        // Skip if either endpoint is an attenuverter
        if (dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()))
            continue;
        if (dynamic_cast<AttenuverterModule*>(dstNode->getProcessor()))
            continue;

        auto* dstModule = dynamic_cast<ModuleBase*>(dstNode->getProcessor());
        if (!dstModule)
            continue;

        LogicalPort dstPort = dstModule->mapInputChannel(conn.destination.channelIndex);
        // Accept ModCV (parameter modulation), Pitch (poly pitch fan), and Gate (poly gate fan).
        // Exclude Audio (osc->filter / filter->VCA audio fans) and Other/Midi.
        if (dstPort.role != PortRole::ModCV && dstPort.role != PortRole::Pitch && dstPort.role != PortRole::Gate)
            continue;

        candidates.push_back(
            {conn.source.nodeID, conn.source.channelIndex, conn.destination.nodeID, conn.destination.channelIndex});
    }

    // Group candidates by (srcNodeID, dstNodeID)
    using NodeIDPair = std::pair<juce::AudioProcessorGraph::NodeID, juce::AudioProcessorGraph::NodeID>;
    std::map<NodeIDPair, std::vector<CandidateEdge>> groups;
    for (auto& e : candidates) {
        groups[{e.srcNodeID, e.dstNodeID}].push_back(e);
    }

    for (auto& [pair, edges] : groups) {
        auto [srcNodeID, dstNodeID] = pair;
        auto* srcNode = mainProcessorGraph.getNodeForId(srcNodeID);
        auto* dstNode = mainProcessorGraph.getNodeForId(dstNodeID);
        if (!srcNode || !dstNode)
            continue;

        auto* srcModule = dynamic_cast<ModuleBase*>(srcNode->getProcessor());
        auto* dstModule = dynamic_cast<ModuleBase*>(dstNode->getProcessor());
        if (!srcModule || !dstModule)
            continue;

        // Build a quick set of (srcCh, dstCh) edges in this group for collapse check
        std::set<std::pair<int, int>> edgeSet;
        for (auto& e : edges)
            edgeSet.insert({e.srcChannel, e.dstChannel});

        // Track which edges have been consumed by poly-bus collapse
        std::set<std::pair<int, int>> consumed;

        // Attempt poly-bus collapse: find source head channels
        for (auto& e : edges) {
            if (consumed.count({e.srcChannel, e.dstChannel}))
                continue;

            LogicalPort srcPort = srcModule->mapOutputChannel(e.srcChannel);
            LogicalPort dstPort = dstModule->mapInputChannel(e.dstChannel);

            if (srcPort.isPolyGroupHead && dstPort.isPolyGroupHead) {
                int Ns = srcPort.polyVoiceSpan;
                int Nd = dstPort.polyVoiceSpan;

                // A mono modulator broadcast across a per-voice mod-CV fan leaves one source channel
                // for all N destinations, so its edges are (Hs, Hd+i) rather than (Hs+i, Hd+i).
                // Collapse it too, otherwise it renders as N wires stacked on the same two jacks.
                const bool isBroadcast = (Ns == 1 && Nd > 1 && dstPort.role == PortRole::ModCV);
                const int srcStride = isBroadcast ? 0 : 1;

                if (isBroadcast || (Ns == Nd && Ns > 1)) {
                    int N = isBroadcast ? Nd : Ns;
                    int Hs = e.srcChannel;
                    int Hd = e.dstChannel;
                    // Check all N edges (Hs + i*srcStride, Hd+i) are present
                    bool complete = true;
                    for (int i = 0; i < N; ++i) {
                        if (!edgeSet.count({Hs + i * srcStride, Hd + i})) {
                            complete = false;
                            break;
                        }
                    }
                    if (complete) {
                        // Emit one PolyBus routing and consume all N edges
                        ModulationRouting r;
                        r.kind = RoutingKind::PolyBus;
                        r.sourceNodeID = srcNodeID;
                        r.sourceChannelIndex = Hs;
                        r.sourceVisibleJack = srcPort.visibleJackIndex;
                        r.destNodeID = dstNodeID;
                        r.destChannelIndex = Hd;
                        r.destVisibleJack = dstPort.visibleJackIndex;
                        r.voiceCount = N;
                        r.hasSource = true;
                        r.hasDest = true;
                        r.amount = 1.0f;
                        r.isBypassed = false;
                        r.role = dstPort.role; // Pitch, Gate, or ModCV depending on dest fan type
                        float peak = readVisualPeak(srcModule->getVisualBuffer());
                        r.modSignalPeak = peak;
                        r.modSignalValue = peak;
                        routings.push_back(r);
                        for (int i = 0; i < N; ++i)
                            consumed.insert({Hs + i * srcStride, Hd + i});
                    }
                }
            }
        }

        // Remaining (non-collapsed) edges become DirectCV routings
        for (auto& e : edges) {
            if (consumed.count({e.srcChannel, e.dstChannel}))
                continue;

            LogicalPort srcPort = srcModule->mapOutputChannel(e.srcChannel);
            LogicalPort dstPort = dstModule->mapInputChannel(e.dstChannel);

            ModulationRouting r;
            r.kind = RoutingKind::DirectCV;
            r.sourceNodeID = srcNodeID;
            r.sourceChannelIndex = e.srcChannel;
            r.sourceVisibleJack = srcPort.visibleJackIndex;
            r.destNodeID = dstNodeID;
            r.destChannelIndex = e.dstChannel;
            r.destVisibleJack = dstPort.visibleJackIndex;
            r.voiceCount = 1;
            r.hasSource = true;
            r.hasDest = true;
            r.amount = 1.0f;
            r.isBypassed = false;
            r.role = dstPort.role; // Pitch, Gate, or ModCV depending on dest port type
            float peak = readVisualPeak(srcModule->getVisualBuffer());
            r.modSignalPeak = peak;
            r.modSignalValue = peak;
            routings.push_back(r);
        }
    }

    return routings;
}

std::vector<AudioEngine::ModRoutingInfo> AudioEngine::getActiveModRoutings() const {
    std::vector<ModRoutingInfo> routings;
    for (const auto& r : getModulationRoutings()) {
        // Only AttenuverterChain routings are exposed to the ModMatrix
        if (r.kind != RoutingKind::AttenuverterChain)
            continue;
        ModRoutingInfo info;
        info.attenuverterNodeID = r.attenuverterNodeID;
        info.sourceNodeID = r.sourceNodeID;
        info.sourceChannelIndex = r.hasSource ? r.sourceChannelIndex : 0;
        info.destNodeID = r.destNodeID;
        info.destChannelIndex = r.destChannelIndex;
        info.isBypassed = r.isBypassed;
        routings.push_back(info);
    }
    return routings;
}

std::vector<AudioEngine::ModulationDisplayInfo> AudioEngine::getModulationDisplayInfo() const {
    return getModulationDisplayInfo(getModulationRoutings());
}

std::vector<AudioEngine::ModulationDisplayInfo>
AudioEngine::getModulationDisplayInfo(const std::vector<ModulationRouting>& allRoutings) const {
    std::vector<ModulationDisplayInfo> result;

    // FRO287: whether the routing's source swings both sides of centre (LFO) or only rises from
    // rest (an envelope) -- read straight off the live source module, cheap enough for the 30 Hz
    // message-thread poll this feeds (two params at most, this call plus the attenuverter's own).
    // Defaults true (unresolved source, e.g. a dangling routing) so a missing source still bands
    // like the historically-assumed bipolar case.
    auto sourceBipolarFor = [this](const ModulationRouting& r) -> bool {
        if (!r.hasSource)
            return true;
        auto* node = mainProcessorGraph.getNodeForId(r.sourceNodeID);
        if (auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr)
            return mb->isModSourceBipolar();
        return true;
    };

    // The attenuverter's own "amount" (-1..1); read here rather than in getModulationRoutings
    // (whose loop is at the function-size ratchet's ceiling) since this is the only caller that
    // needs it as a display value.
    auto attenuverterAmount = [this](juce::AudioProcessorGraph::NodeID attenuverterNodeID) -> float {
        auto* node = mainProcessorGraph.getNodeForId(attenuverterNodeID);
        if (node == nullptr)
            return 1.0f;
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount")))
            return p->get();
        return 1.0f;
    };

    // Attenuverter routings first (as before)
    for (const auto& r : allRoutings) {
        if (r.kind != RoutingKind::AttenuverterChain)
            continue;
        if (!r.hasDest)
            continue;
        ModulationDisplayInfo info;
        info.attenuverterNodeID = r.attenuverterNodeID;
        info.destNodeID = r.destNodeID;
        info.destChannelIndex = r.destChannelIndex;
        info.modSignalValue = r.modSignalValue;
        info.modSignalPeak = r.modSignalPeak;
        info.isBypassed = r.isBypassed;
        info.amount = attenuverterAmount(r.attenuverterNodeID);
        info.sourceBipolar = sourceBipolarFor(r);
        result.push_back(info);
    }

    // DirectCV and PolyBus routings after — only emit display info for ModCV role
    // (Pitch/Gate poly-bus routings are signal distribution, not parameter modulation,
    //  so they must not produce knob-ring display info).
    for (const auto& r : allRoutings) {
        if (r.kind != RoutingKind::DirectCV && r.kind != RoutingKind::PolyBus)
            continue;
        if (!r.hasDest)
            continue;
        if (r.role != PortRole::ModCV)
            continue;
        ModulationDisplayInfo info;
        // attenuverterNodeID left default-constructed (invalid) for direct/poly routings
        info.destNodeID = r.destNodeID;
        info.destChannelIndex = r.destChannelIndex;
        info.modSignalValue = r.modSignalValue;
        info.modSignalPeak = r.modSignalPeak;
        info.isBypassed = false;
        info.amount = r.amount; // 1.0 -- DirectCV/PolyBus have no attenuverter to attenuate with
        info.sourceBipolar = sourceBipolarFor(r);
        result.push_back(info);
    }

    return result;
}

juce::AudioProcessorGraph::NodeID AudioEngine::addModRouting(juce::AudioProcessorGraph::NodeID sourceNodeID,
                                                             int sourceChannelIndex,
                                                             juce::AudioProcessorGraph::NodeID destNodeID,
                                                             int destChannelIndex) {
    auto attenuverterNode = mainProcessorGraph.addNode(std::make_unique<AttenuverterModule>());
    if (attenuverterNode == nullptr)
        return {};
    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(attenuverterNode->getProcessor()->getParameters()[1]))
        param->setValueNotifyingHost(param->convertTo0to1(1.0f));
    mainProcessorGraph.addConnection({{sourceNodeID, sourceChannelIndex}, {attenuverterNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{attenuverterNode->nodeID, 0}, {destNodeID, destChannelIndex}});
    return attenuverterNode->nodeID;
}

void AudioEngine::addEmptyModRouting() { mainProcessorGraph.addNode(std::make_unique<AttenuverterModule>()); }

void AudioEngine::removeModRouting(juce::AudioProcessorGraph::NodeID attenuverterNodeID) {
    mainProcessorGraph.removeNode(attenuverterNodeID);
}

void AudioEngine::toggleModBypass(juce::AudioProcessorGraph::NodeID attenuverterNodeID) {
    if (auto* node = mainProcessorGraph.getNodeForId(attenuverterNodeID)) {
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor())) {
            module->setBypassed(!module->isBypassed());
        }
    }
}

bool AudioEngine::isModBypassed(juce::AudioProcessorGraph::NodeID attenuverterNodeID) const {
    if (auto* node = mainProcessorGraph.getNodeForId(attenuverterNodeID)) {
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor())) {
            return module->isBypassed();
        }
    }
    return false;
}

void AudioEngine::updateModuleNames() {
    std::map<juce::String, int> typeCounts;
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor())) {
            if (module->getModuleType() == ModuleType::ExternalMidi)
                continue; // Do not rename External MIDI modules as their name matches the device name

            juce::String baseName = module->getName();
            int lastSpace = baseName.lastIndexOf(" ");
            if (lastSpace != -1 && baseName.substring(lastSpace + 1).containsOnly("0123456789"))
                baseName = baseName.substring(0, lastSpace);
            if (baseName.startsWith("Attenuverter"))
                baseName = "Mod Slot";
            int index = ++typeCounts[baseName];
            module->setModuleName(baseName + " " + juce::String(index));
        }
    }
}
