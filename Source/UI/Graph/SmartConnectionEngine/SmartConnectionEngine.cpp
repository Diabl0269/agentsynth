// SmartConnectionEngine.cpp
//
// Smart-connection mode persistence, the insert-modifier drag-tick re-sample, and the
// jack-eligibility helpers (isInputJackFree/isOutputJackFree, areJacksAlreadyConnected,
// findSingleUpstreamAudioLink, disconnectAudioLink) plus shouldOfferSmartConnections. Sibling
// SmartConnectionEngineApply.cpp holds refreshSmartSuggestions/applySmartSuggestions, which this
// depends on (refreshSuggestionsIfInsertModifierChanged calls back into it). SmartConnectionEngine
// is declared in SmartConnectionEngine.h; this TU includes the real GraphEditor.h (for
// GraphEditor::resolvePolyLink and the shared detail:: helpers in GraphEditorInternal.h) — a .cpp
// doing that is not a cycle, only the engine's own header must stay narrow.

#include "SmartConnectionEngine.h"
#include "AudioEngine/AudioEngine.h"

#include "Modules/AttenuverterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

using namespace detail;

SmartConnectionEngine::SmartConnectionMode SmartConnectionEngine::smartConnectionModeFromString(const juce::String& s) {
    if (s == "Off")
        return SmartConnectionMode::Off;
    if (s == "NewOnly")
        return SmartConnectionMode::NewOnly;
    if (s == "AllMoves")
        return SmartConnectionMode::AllMoves;
    return SmartConnectionMode::NewAndUnwired;
}

juce::String SmartConnectionEngine::smartConnectionModeToString(SmartConnectionMode mode) {
    switch (mode) {
    case SmartConnectionMode::Off:
        return "Off";
    case SmartConnectionMode::NewOnly:
        return "NewOnly";
    case SmartConnectionMode::AllMoves:
        return "AllMoves";
    case SmartConnectionMode::NewAndUnwired:
    default:
        return "NewAndUnwired";
    }
}

bool SmartConnectionEngine::isInsertModifierDown() const {
    return insertModifierOverride_.has_value() ? *insertModifierOverride_
                                               : juce::ModifierKeys::getCurrentModifiersRealtime().isCtrlDown();
}

void SmartConnectionEngine::refreshSuggestionsIfInsertModifierChanged(const DragPreviewState& drag) {
    if (!drag.active)
        return;
    const bool insertNow = isInsertModifierDown();
    if (insertNow == lastSampledInsertModifier_)
        return; // the common case: one bool compare per drag tick
    lastSampledInsertModifier_ = insertNow;
    refreshSmartSuggestions(drag);
}

bool SmartConnectionEngine::shouldOfferSmartConnections(const DragPreviewState& drag) const {
    if (smartConnectionMode_ == SmartConnectionMode::Off)
        return false;
    if (drag.selectionDragBlocksSuggestions)
        return false;
    if (drag.isSnippet)
        return false;

    const bool isNewDrop = drag.selfId.uid == 0;
    if (isNewDrop)
        return true; // NewOnly / NewAndUnwired / AllMoves all allow library drops

    switch (smartConnectionMode_) {
    case SmartConnectionMode::NewOnly:
        return false;
    case SmartConnectionMode::AllMoves:
    case SmartConnectionMode::NewAndUnwired:
        // NewAndUnwired still applies on every single-module move; "unwired" is the
        // per-jack free check in refreshSmartSuggestions (main I/O not already patched).
        return true;
    case SmartConnectionMode::Off:
        return false;
    }
    return false;
}

bool SmartConnectionEngine::isInputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const {
    auto& graph = host_.graph();
    if (isMidi) {
        const int channel = juce::AudioProcessorGraph::midiChannelIndex;
        for (const auto& c : graph.getConnections()) {
            if (c.destination.nodeID == nodeId && c.destination.channelIndex == channel)
                return false;
        }
        return true;
    }

    // Visible jack -> raw channel(s), same path as areJacksAlreadyConnected / connectPorts.
    auto* node = graph.getNodeForId(nodeId);
    auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    std::vector<int> rawChannels;
    if (mb != nullptr) {
        for (const auto& t : mb->getJackTargets(jack, true)) {
            for (int v = 0; v < t.voiceSpan; ++v)
                rawChannels.push_back(t.rawHeadChannel + v);
        }
    } else {
        rawChannels.push_back(jack); // Audio I/O identity mapping
    }

    for (const auto& c : graph.getConnections()) {
        if (c.destination.nodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (c.destination.channelIndex == ch)
                return false;
        }
    }
    for (const auto& r : host_.engine().getModulationRoutings()) {
        if (!r.hasDest || r.destNodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (r.destChannelIndex == ch)
                return false;
        }
    }
    return true;
}

bool SmartConnectionEngine::isOutputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const {
    auto& graph = host_.graph();
    if (isMidi) {
        const int channel = juce::AudioProcessorGraph::midiChannelIndex;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID == nodeId && c.source.channelIndex == channel)
                return false;
        }
        return true;
    }

    auto* node = graph.getNodeForId(nodeId);
    auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    std::vector<int> rawChannels;
    if (mb != nullptr) {
        for (const auto& t : mb->getJackTargets(jack, false)) {
            for (int v = 0; v < t.voiceSpan; ++v)
                rawChannels.push_back(t.rawHeadChannel + v);
        }
        if (rawChannels.empty())
            rawChannels.push_back(jack);
    } else {
        rawChannels.push_back(jack);
    }

    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (c.source.channelIndex == ch)
                return false;
        }
    }
    for (const auto& r : host_.engine().getModulationRoutings()) {
        if (!r.hasSource || r.sourceNodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (r.sourceChannelIndex == ch)
                return false;
        }
    }
    return true;
}

bool SmartConnectionEngine::areJacksAlreadyConnected(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                                     juce::AudioProcessorGraph::NodeID dstId, int dstJack,
                                                     bool isMidi) const {
    auto& graph = host_.graph();
    if (isMidi) {
        const int ch = juce::AudioProcessorGraph::midiChannelIndex;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID == srcId && c.destination.nodeID == dstId && c.source.channelIndex == ch &&
                c.destination.channelIndex == ch)
                return true;
        }
        return false;
    }

    auto* srcNode = graph.getNodeForId(srcId);
    auto* dstNode = graph.getNodeForId(dstId);
    auto* srcMb = srcNode ? dynamic_cast<ModuleBase*>(srcNode->getProcessor()) : nullptr;
    auto* dstMb = dstNode ? dynamic_cast<ModuleBase*>(dstNode->getProcessor()) : nullptr;
    const auto link = GraphEditor::resolvePolyLink(srcMb, srcJack, dstMb, dstJack);

    for (int v = 0; v < link.voiceCount; ++v) {
        const int sCh = link.sourceRawChannel + v * link.sourceStride;
        const int dCh = link.destRawChannel + v;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID == srcId && c.destination.nodeID == dstId && c.source.channelIndex == sCh &&
                c.destination.channelIndex == dCh)
                return true;
        }
    }
    for (const auto& r : host_.engine().getModulationRoutings()) {
        if (r.hasSource && r.hasDest && r.sourceNodeID == srcId && r.destNodeID == dstId &&
            r.sourceChannelIndex == link.sourceRawChannel && r.destChannelIndex == link.destRawChannel)
            return true;
    }
    return false;
}

std::optional<SmartConnectionEngine::UpstreamLink>
SmartConnectionEngine::findSingleUpstreamAudioLink(juce::AudioProcessorGraph::NodeID dstId, int dstJack) const {
    auto& graph = host_.graph();
    auto* dstNode = graph.getNodeForId(dstId);
    if (dstNode == nullptr)
        return std::nullopt;

    // Visible jack -> raw channel(s), the same expansion isInputJackFree uses.
    auto* dstMb = dynamic_cast<ModuleBase*>(dstNode->getProcessor());
    std::vector<int> rawChannels;
    if (dstMb != nullptr) {
        for (const auto& t : dstMb->getJackTargets(dstJack, true))
            for (int v = 0; v < t.voiceSpan; ++v)
                rawChannels.push_back(t.rawHeadChannel + v);
    } else {
        rawChannels.push_back(dstJack); // Audio I/O identity mapping
    }
    const auto coversRaw = [&rawChannels](int ch) {
        return std::find(rawChannels.begin(), rawChannels.end(), ch) != rawChannels.end();
    };

    // A mod routing is a cable with a hidden attenuverter node in it; splicing an FX into that is
    // not what the user asked for, so the jack is treated as un-reroutable.
    for (const auto& r : host_.engine().getModulationRoutings()) {
        if (r.hasDest && r.destNodeID == dstId && coversRaw(r.destChannelIndex))
            return std::nullopt;
    }

    std::optional<UpstreamLink> found;
    for (const auto& c : graph.getConnections()) {
        if (c.destination.nodeID != dstId || c.destination.isMIDI() || !coversRaw(c.destination.channelIndex))
            continue;

        auto* srcNode = graph.getNodeForId(c.source.nodeID);
        if (srcNode == nullptr || dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()) != nullptr)
            return std::nullopt;

        auto* srcMb = dynamic_cast<ModuleBase*>(srcNode->getProcessor());
        const int srcJack =
            srcMb != nullptr ? srcMb->mapOutputChannel(c.source.channelIndex).visibleJackIndex : c.source.channelIndex;

        if (!found.has_value())
            found = UpstreamLink{c.source.nodeID, {}};
        else if (found->nodeId != c.source.nodeID)
            return std::nullopt; // a genuine hand-built mix: rerouting it would change what sums

        // Several legs of the SAME node is our own dual-to-mono wiring, not a mix — collect them all
        // and let the planner doom every one of them.
        if (std::find(found->jacks.begin(), found->jacks.end(), srcJack) == found->jacks.end())
            found->jacks.push_back(srcJack);
    }
    if (found.has_value())
        std::sort(found->jacks.begin(), found->jacks.end()); // Left before Right, deterministically
    return found;
}

void SmartConnectionEngine::disconnectAudioLink(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                                juce::AudioProcessorGraph::NodeID dstId, int dstJack) {
    auto& graph = host_.graph();
    auto* srcNode = graph.getNodeForId(srcId);
    auto* dstNode = graph.getNodeForId(dstId);
    if (srcNode == nullptr || dstNode == nullptr)
        return;

    // Same fan expansion connectPorts and disconnectCable use, so a collapsed stereo wire takes
    // both raw legs with it rather than leaving a half-connected pair behind.
    const auto link = GraphEditor::resolvePolyLink(dynamic_cast<ModuleBase*>(srcNode->getProcessor()), srcJack,
                                                   dynamic_cast<ModuleBase*>(dstNode->getProcessor()), dstJack);
    for (int v = 0; v < link.voiceCount; ++v)
        graph.removeConnection(
            {{srcId, link.sourceRawChannel + v * link.sourceStride}, {dstId, link.destRawChannel + v}});
}
