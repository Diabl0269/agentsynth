// GraphEditorSmartConnections.cpp
//
// Smart-connection naming and eligibility: SmartConnectionMode string conversion, module
// display-name/title/rename-commit, and the jack-eligibility helpers (estimatePortCenter,
// isInputJackFree/isOutputJackFree, areJacksAlreadyConnected, findSingleUpstreamAudioLink,
// disconnectAudioLink) plus their file-local scoring helpers. Sibling
// GraphEditorSmartConnectionsApply.cpp holds refreshSmartSuggestions/applySmartSuggestions.
// GraphEditor is declared in GraphEditor.h.

#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include "../../AI/AIStateMapper.h"
#include "../../Modules/AttenuverterModule.h"
#include "../../Modules/MacroControlModule.h"
#include "../ModuleComponent/ModuleComponent.h"

using namespace detail;

GraphEditor::SmartConnectionMode GraphEditor::smartConnectionModeFromString(const juce::String& s) {
    if (s == "Off")
        return SmartConnectionMode::Off;
    if (s == "NewOnly")
        return SmartConnectionMode::NewOnly;
    if (s == "AllMoves")
        return SmartConnectionMode::AllMoves;
    return SmartConnectionMode::NewAndUnwired;
}

juce::String GraphEditor::smartConnectionModeToString(SmartConnectionMode mode) {
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

juce::String GraphEditor::getModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId) const {
    if (auto* node = audioEngine.getGraph().getNodeForId(nodeId))
        return node->properties["displayName"].toString();
    return {};
}

void GraphEditor::setModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& name) {
    auto& graph = audioEngine.getGraph();
    auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr)
        return;

    // Blank or whitespace-only reverts to the auto-numbered default rather than showing an empty
    // header. Capped at the same length the untrusted patch path caps at, so a title typed here and
    // a title loaded from a file can never disagree about what is storable.
    const auto trimmed = name.trim().substring(0, synth::kMaxModuleDisplayNameChars);
    if (trimmed == getModuleDisplayName(nodeId))
        return; // no-op rename: do not burn an undo step on it

    auto apply = [this, nodeId, trimmed] {
        if (auto* n = audioEngine.getGraph().getNodeForId(nodeId)) {
            if (trimmed.isEmpty())
                n->properties.remove("displayName");
            else
                n->properties.set("displayName", trimmed);
        }
        for (auto* comp : content.getModules())
            if (comp != nullptr && comp->getNodeId() == nodeId)
                comp->repaint();
    };

    if (undoManager)
        undoManager->recordStructuralChange(graph, apply);
    else
        apply();
}

juce::String GraphEditor::getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId,
                                         juce::AudioProcessor* processor) const {
    const auto custom = getModuleDisplayName(nodeId);
    if (custom.isNotEmpty())
        return custom;
    return processor != nullptr ? processor->getName() : juce::String();
}

void GraphEditor::commitAnyOpenTitleRename() {
    // Copy the card list first: committing mutates the graph (and pushes an undo snapshot), and
    // nothing may be iterating content.getModules() across that.
    std::vector<ModuleComponent*> renaming;
    for (auto* comp : content.getModules())
        if (comp != nullptr && comp->isRenamingTitle())
            renaming.push_back(comp);

    for (auto* comp : renaming) {
        juce::Component::SafePointer<ModuleComponent> safe(comp);
        if (safe != nullptr)
            safe->finishTitleRename(true);
    }
}

void GraphEditor::refreshSuggestionsIfInsertModifierChanged() {
    if (!dragPreviewActive)
        return;
    const bool insertNow = isInsertModifierDown();
    if (insertNow == lastSampledInsertModifier)
        return; // the common case: one bool compare per drag tick
    lastSampledInsertModifier = insertNow;
    refreshSmartSuggestions();
}

void GraphEditor::clearSmartSuggestions() { smartSuggestions.clear(); }

bool GraphEditor::nodeHasCables(juce::AudioProcessorGraph::NodeID nodeId) const {
    auto& graph = audioEngine.getGraph();
    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID == nodeId || c.destination.nodeID == nodeId)
            return true;
    }
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if ((r.hasSource && r.sourceNodeID == nodeId) || (r.hasDest && r.destNodeID == nodeId))
            return true;
    }
    return false;
}

bool GraphEditor::shouldOfferSmartConnections() const {
    if (smartConnectionMode == SmartConnectionMode::Off)
        return false;
    if (selectionDragActive && selection.size() > 1)
        return false;
    if (dragPreviewIsSnippet)
        return false;

    const bool isNewDrop = dragPreviewSelfId.uid == 0;
    if (isNewDrop)
        return true; // NewOnly / NewAndUnwired / AllMoves all allow library drops

    switch (smartConnectionMode) {
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

juce::Point<int> GraphEditor::estimatePortCenter(juce::AudioProcessor* proc, juce::Rectangle<int> bounds, int jack,
                                                 bool isInput, bool isMidi) {
    if (proc == nullptr)
        return bounds.getCentre();

    if (isMidi) {
        if (isInput)
            return {bounds.getX() + 10, bounds.getY() + ModuleComponent::kPortGutterHeaderHeight};
        return {bounds.getRight() - 10, bounds.getY() + ModuleComponent::kPortGutterHeaderHeight};
    }

    const int yStep = 20;
    // MUST equal ModuleComponent::getPortCenter's headerHeight. It read 30 while the real card used
    // 38, so every ghost preview cable terminated 8px ABOVE the jack dot it claimed to land on —
    // visibly floating over the jack's label row. Pinned by
    // GraphEditorTest.GhostPortEstimateMatchesTheRealJackCentre.
    const int headerHeight = ModuleComponent::kPortGutterHeaderHeight;
    int portOffset = 0;
    if (proc->producesMidi())
        portOffset = 20;

    int visible = 0;
    if (auto* mb = dynamic_cast<ModuleBase*>(proc))
        visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
    else
        visible = isInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();

    const int clamped = (visible > 0) ? juce::jlimit(0, visible - 1, jack) : 0;

    if (auto* macro = dynamic_cast<MacroControlModule*>(proc)) {
        if (!isInput) {
            return {bounds.getRight() - 10, bounds.getY() + synth::LayoutUtil::macroRowCentreY(clamped)};
        }
    }

    if (isInput) {
        int columns = 1;
        if (auto* mb = dynamic_cast<ModuleBase*>(proc))
            if (mb->getVisibleInputPortCount() > 10 && bounds.getWidth() >= synth::LayoutUtil::kDoubleWidth)
                columns = 2;
        if (columns > 1 && visible > 0) {
            const int rows = (visible + columns - 1) / columns;
            const int col = clamped / rows;
            const int row = clamped % rows;
            return {bounds.getX() + 10 + col * 100, bounds.getY() + headerHeight + portOffset + row * yStep + 20};
        }
        return {bounds.getX() + 10, bounds.getY() + headerHeight + portOffset + clamped * yStep + 20};
    }
    return {bounds.getRight() - 10, bounds.getY() + headerHeight + portOffset + clamped * yStep + 20};
}

bool GraphEditor::isInputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const {
    auto& graph = audioEngine.getGraph();
    if (isMidi) {
        const int channel = juce::AudioProcessorGraph::midiChannelIndex;
        for (const auto& c : graph.getConnections()) {
            if (c.destination.nodeID == nodeId && c.destination.channelIndex == channel)
                return false;
        }
        return true;
    }

    // Visible jack → raw channel(s), same path as areJacksAlreadyConnected / connectPorts.
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
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (!r.hasDest || r.destNodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (r.destChannelIndex == ch)
                return false;
        }
    }
    return true;
}

bool GraphEditor::isOutputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const {
    auto& graph = audioEngine.getGraph();
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
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (!r.hasSource || r.sourceNodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (r.sourceChannelIndex == ch)
                return false;
        }
    }
    return true;
}

bool GraphEditor::areJacksAlreadyConnected(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                           juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi) const {
    auto& graph = audioEngine.getGraph();
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
    const auto link = resolvePolyLink(srcMb, srcJack, dstMb, dstJack);

    for (int v = 0; v < link.voiceCount; ++v) {
        const int sCh = link.sourceRawChannel + v * link.sourceStride;
        const int dCh = link.destRawChannel + v;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID == srcId && c.destination.nodeID == dstId && c.source.channelIndex == sCh &&
                c.destination.channelIndex == dCh)
                return true;
        }
    }
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (r.hasSource && r.hasDest && r.sourceNodeID == srcId && r.destNodeID == dstId &&
            r.sourceChannelIndex == link.sourceRawChannel && r.destChannelIndex == link.destRawChannel)
            return true;
    }
    return false;
}

std::optional<GraphEditor::UpstreamLink>
GraphEditor::findSingleUpstreamAudioLink(juce::AudioProcessorGraph::NodeID dstId, int dstJack) const {
    auto& graph = audioEngine.getGraph();
    auto* dstNode = graph.getNodeForId(dstId);
    if (dstNode == nullptr)
        return std::nullopt;

    // Visible jack → raw channel(s), the same expansion isInputJackFree uses.
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
    for (const auto& r : audioEngine.getModulationRoutings()) {
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

void GraphEditor::disconnectAudioLink(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                      juce::AudioProcessorGraph::NodeID dstId, int dstJack) {
    auto& graph = audioEngine.getGraph();
    auto* srcNode = graph.getNodeForId(srcId);
    auto* dstNode = graph.getNodeForId(dstId);
    if (srcNode == nullptr || dstNode == nullptr)
        return;

    // Same fan expansion connectPorts and disconnectCable use, so a collapsed stereo wire takes
    // both raw legs with it rather than leaving a half-connected pair behind.
    const auto link = resolvePolyLink(dynamic_cast<ModuleBase*>(srcNode->getProcessor()), srcJack,
                                      dynamic_cast<ModuleBase*>(dstNode->getProcessor()), dstJack);
    for (int v = 0; v < link.voiceCount; ++v)
        graph.removeConnection(
            {{srcId, link.sourceRawChannel + v * link.sourceStride}, {dstId, link.destRawChannel + v}});
}
