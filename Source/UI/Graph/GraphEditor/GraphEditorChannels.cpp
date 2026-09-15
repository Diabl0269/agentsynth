// GraphEditorChannels.cpp
//
// Auto-create-channel-on-connect, and "Make channel" / "Duplicate into this channel" (FRO25).
// GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory
// hold the rest of the class.
//
// MacroPortCrossingEdge/MacroPortCrossingGroup (GraphEditor.h's "Auto-create-ports-on-group"
// section) are aliased from MacroGroupController, which now owns the crossing-plan math
// (buildMacroPortCrossingPlan and its add/remove-member variants, spliceMacroPorts/
// spliceOutMacroPort) outright. Five of them keep a private one-line forwarder on GraphEditor
// because this file's duplicate-channel-strip path (buildChannelStripMacroBox /
// duplicateChannelStrip) calls them by their original name — autoMacroPortName/
// mintMacroPortForAutoCreate have no such caller and moved with none.

#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "SnippetManager.h"

using namespace detail;

// ---- Auto-create-channel-on-connect (T184, P9-3c, docs/mixer.md §5.2 "main workflow") ----------

bool GraphEditor::nodeIsTimelineMidiSource(juce::AudioProcessorGraph::NodeID nodeId) const {
    auto* node = audioEngine.getGraph().getNodeForId(nodeId);
    if (node == nullptr)
        return false;
    auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
    return mb != nullptr && mb->getModuleType() == ModuleType::TimelineMidiSource;
}

namespace {
// Mirrors MainComponent.cpp's kChannelCardGapX — the real-card-width gap
// MainComponent::addAudioTrack/addInstrumentTrack lay their own chain cards out with. Duplicated
// here rather than shared: that constant lives in MainComponent.cpp, which GraphEditor cannot
// reach into (the dependency runs the other way).
constexpr int kAutoChannelCardGapX = 40;
} // namespace

// Runs synth::findUnchanneledOutputFeeds() from `searchFrom` (the real destination instrument for
// a direct module-jack drop, or the MacroMidiInlet port node itself for the collapsed-macro-card
// existing-jack drop — a port is a plain pass-through, so the BFS reaches the interior instrument
// through it on its own) and, if it finds any exit, builds a channel there via
// synth::buildChannelForFeeds. Lays EQ/Compressor/Strip (and Master, if newly spliced) to the
// right of the first exit's source node, using estimateModuleSize() widths and the same
// real-card-width stride MainComponent::addAudioTrack uses (mirrored here as
// kAutoChannelCardGapX — the gap constant lives in MainComponent.cpp, which GraphEditor can't
// reach into). If every exit's source node is an ordinary (non-port) member of the SAME macro, the
// three new chain nodes join that macro (macros.addMember) — never boxes when a source is itself a
// macro port (that would insert between an inner node and its outlet) or when sources span more
// than one macro.
void GraphEditor::maybeAutoCreateChannelAfterConnect(juce::AudioProcessorGraph::NodeID searchFrom) {
    auto& graph = audioEngine.getGraph();
    const auto exits = synth::findUnchanneledOutputFeeds(graph, searchFrom);
    if (exits.empty())
        return;

    auto* firstSourceNode = graph.getNodeForId(exits.front().source.nodeID);
    if (firstSourceNode == nullptr)
        return;

    const auto originPos = juce::Point<int>(static_cast<int>(firstSourceNode->properties.getWithDefault("x", 0)),
                                            static_cast<int>(firstSourceNode->properties.getWithDefault("y", 0)));
    const juce::String originType = synth::AIStateMapper::getFactoryTypeName(firstSourceNode->getProcessor());

    // Lay EQ/Compressor/Strip (and Master, if it's newly spliced) out left-to-right from the real
    // card widths, the same reasoning MainComponent::addAudioTrack's own comment gives.
    const int eqX = originPos.x + estimateModuleSize(originType).x + kAutoChannelCardGapX;
    const int compressorX = eqX + estimateModuleSize("Parametric EQ").x + kAutoChannelCardGapX;
    const int stripX = compressorX + estimateModuleSize("Compressor").x + kAutoChannelCardGapX;
    const int masterX = stripX + estimateModuleSize("Channel Strip").x + kAutoChannelCardGapX;
    const synth::DefaultChannelLayout layout{
        /*eq=*/{eqX, originPos.y},
        /*compressor=*/{compressorX, originPos.y},
        /*strip=*/{stripX, originPos.y},
        /*master=*/{masterX, originPos.y},
    };

    // Known BEFORE the build below, the same reason T187's own relocation check needs it: whether
    // this call is the one that splices Master for the first time.
    const bool masterExistedBefore = synth::findMasterNode(graph) != nullptr;

    // Every exit's distinct source node, gathered before buildChannelForFeeds removes the exit
    // edges — used for the macro-boxing decision below.
    std::vector<juce::AudioProcessorGraph::NodeID> sourceNodeIds;
    for (const auto& exit : exits)
        if (std::find(sourceNodeIds.begin(), sourceNodeIds.end(), exit.source.nodeID) == sourceNodeIds.end())
            sourceNodeIds.push_back(exit.source.nodeID);

    const auto channel = synth::buildChannelForFeeds(graph, exits, layout);
    if (channel.stripUuid.isEmpty())
        return; // a factory/addNode failure partway — same contract as buildDefaultAudioChannel

    // T187 mirror (MainComponent::addAudioTrack's own comment): on the very first channel, relocate
    // a bare Audio Output to terminate the row instead of leaving the finished chain cabling back
    // across the whole canvas to reach wherever it already sat.
    if (!masterExistedBefore && channel.master != nullptr) {
        const int outputX = masterX + estimateModuleSize("Master").x + kAutoChannelCardGapX;
        for (auto* node : graph.getNodes())
            if (node != nullptr && node->getProcessor() != nullptr &&
                node->getProcessor()->getName() == "Audio Output") {
                node->properties.set("x", outputX);
                node->properties.set("y", originPos.y);
            }
    }

    // Boxing: only when EVERY exit source is an ORDINARY (non-port) member of the SAME macro —
    // never insert between an inner node and its outlet (a MacroOutlet source refuses), and never
    // guess when sources span more than one macro or none at all.
    juce::String commonMacroId;
    bool boxable = true;
    for (const auto& sourceId : sourceNodeIds) {
        const juce::String uuid = nodeUuidFor(sourceId);
        auto* macro = uuid.isNotEmpty() ? macros.findByMember(uuid) : nullptr;
        if (macro == nullptr || macro->memberIsPort(uuid)) {
            boxable = false;
            break;
        }
        if (commonMacroId.isEmpty())
            commonMacroId = macro->id;
        else if (commonMacroId != macro->id) {
            boxable = false;
            break;
        }
    }
    if (boxable && commonMacroId.isNotEmpty()) {
        macros.addMember(commonMacroId, channel.eqUuid);
        macros.addMember(commonMacroId, channel.compressorUuid);
        macros.addMember(commonMacroId, channel.stripUuid);
    }
}

// FRO26 (P9-3e, docs/mixer.md §5.13): "Create channels" for existing projects — runs the exact
// same per-node channel-creation maybeAutoCreateChannelAfterConnect() already does for T184's
// connect-triggered case, once per entry in `trackSourceNodeIds` (each track's own bound node — a
// "Track Audio" or "Track In" — as MainComponent resolves from TimelineDoc, which GraphEditor
// deliberately owns no reference to). A track that already reaches the output through an existing
// ChannelStripModule (or reaches nothing at all — an unbound/orphaned entry the caller should not
// have passed) is silently skipped, same as maybeAutoCreateChannelAfterConnect's own
// "exits.empty()" early return — a caller can safely pass every track's source node without
// pre-filtering.
//
// NO UNDO OF ITS OWN and no updateComponents() call, same contract as
// maybeAutoCreateChannelAfterConnect — the caller (MainComponent::createChannelsForExistingTracks)
// wraps the whole call in ONE recordGraphTimelineAndMacroChange transaction covering every
// track, and calls updateComponents() itself afterward, so the whole multi-track sweep is a
// SINGLE undo step.
void GraphEditor::createChannelsForUnchanneledTracks(
    const std::vector<juce::AudioProcessorGraph::NodeID>& trackSourceNodeIds) {
    for (const auto& nodeId : trackSourceNodeIds)
        maybeAutoCreateChannelAfterConnect(nodeId);
}

// ---- FRO25 (P9-3d, docs/mixer.md §5.8): "Make channel" / "Duplicate into this channel" -----------

namespace {

// The same left-to-right card row maybeAutoCreateChannelAfterConnect lays out, starting right of
// `node` — synth::ChannelLayoutFn for buildMakeChannel (Core cannot size cards itself).
synth::DefaultChannelLayout channelLayoutRightOf(juce::AudioProcessorGraph::Node& node) {
    const int originX = static_cast<int>(node.properties.getWithDefault("x", 0));
    const int originY = static_cast<int>(node.properties.getWithDefault("y", 0));
    const juce::String originType = synth::AIStateMapper::getFactoryTypeName(node.getProcessor());
    const int eqX = originX + GraphEditor::estimateModuleSize(originType).x + kAutoChannelCardGapX;
    const int compressorX = eqX + GraphEditor::estimateModuleSize("Parametric EQ").x + kAutoChannelCardGapX;
    const int stripX = compressorX + GraphEditor::estimateModuleSize("Compressor").x + kAutoChannelCardGapX;
    const int masterX = stripX + GraphEditor::estimateModuleSize("Channel Strip").x + kAutoChannelCardGapX;
    return {{eqX, originY}, {compressorX, originY}, {stripX, originY}, {masterX, originY}};
}

bool isAttenuverterNode(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr && dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr;
}

} // namespace

// Ports come from the same group-time crossing plan groupSelectionIntoMacro(true) uses
// (buildMacroPortCrossingPlan + spliceMacroPorts) — always created, regardless of the auto-port
// preference, since a shared LFO reaching into the channel is exactly what the port is for —
// except the new strip's own outputs, which stay plain edges (Strip -> Master must never be a
// port; see ChannelFlows.h). Master stays outside every macro; a first-ever Master relocates
// Audio Output (T187 mirror).
bool GraphEditor::makeChannelFromNode(juce::AudioProcessorGraph::NodeID source, const juce::String& channelName) {
    auto& graph = audioEngine.getGraph();
    const auto plan = synth::planMakeChannel(graph, source, macros);
    if (!plan.needsChannel)
        return false;
    if (plan.refusal.isNotEmpty()) {
        if (onStatusMessage)
            onStatusMessage(plan.refusal);
        return false;
    }

    const bool masterExistedBefore = synth::findMasterNode(graph) != nullptr;
    const auto made = synth::buildMakeChannel(
        graph, plan, [](juce::AudioProcessorGraph::Node& node) { return channelLayoutRightOf(node); });
    if (made.memberUuids.empty() && made.buses.empty())
        return false;

    // T187 mirror (maybeAutoCreateChannelAfterConnect's own comment): the first-ever Master pulls a
    // bare Audio Output in to terminate the row.
    if (!masterExistedBefore)
        if (auto* master = synth::findMasterNode(graph)) {
            const int outputX = static_cast<int>(master->properties.getWithDefault("x", 0)) +
                                estimateModuleSize("Master").x + kAutoChannelCardGapX;
            const int outputY = static_cast<int>(master->properties.getWithDefault("y", 0));
            for (auto* node : graph.getNodes())
                if (node != nullptr && node->getProcessor() != nullptr &&
                    node->getProcessor()->getName() == "Audio Output") {
                    node->properties.set("x", outputX);
                    node->properties.set("y", outputY);
                }
        }

    // One collapsed macro per channel, ports from the group-time crossing plan (computed before the
    // macro exists, exactly as groupSelectionIntoMacro does) minus the strip's own outlets.
    auto box = [this](const std::vector<juce::String>& memberUuids, const juce::String& name,
                      const juce::String& stripUuid) {
        std::vector<juce::AudioProcessorGraph::NodeID> ids;
        for (const auto& uuid : memberUuids) {
            const auto id = resolveMemberNodeId(uuid);
            if (id.uid != 0)
                ids.push_back(id);
        }
        auto portPlan = buildMacroPortCrossingPlan(ids);
        const auto stripId = resolveMemberNodeId(stripUuid);
        portPlan.erase(
            std::remove_if(portPlan.begin(), portPlan.end(),
                           [&](const MacroPortCrossingGroup& g) { return g.internalNodeId == stripId && !g.isInput; }),
            portPlan.end());

        juce::Point<int> origin;
        if (auto* first = ids.empty() ? nullptr : audioEngine.getGraph().getNodeForId(ids.front()))
            origin = {static_cast<int>(first->properties.getWithDefault("x", 0)),
                      static_cast<int>(first->properties.getWithDefault("y", 0))};
        synth::Macro macro;
        macro.name = name;
        macro.members = memberUuids;
        macro.collapsed = true;
        macro.bounds = juce::Rectangle<int>(origin.x, origin.y, synth::LayoutUtil::kSingleWidth, kMacroCardHeight);
        const auto macroId = macros.add(macro).id;
        if (!portPlan.empty())
            spliceMacroPorts(macroId, portPlan);
    };

    if (!made.memberUuids.empty())
        box(made.memberUuids, channelName, made.channel.stripUuid);
    for (const auto& bus : made.buses) {
        const auto headId = resolveMemberNodeId(bus.headUuid);
        auto* head = audioEngine.getGraph().getNodeForId(headId);
        const juce::String headName = head != nullptr && head->getProcessor() != nullptr
                                          ? head->getProcessor()->getName()
                                          : juce::String("Shared");
        box(bus.memberUuids, headName + " Bus", bus.channel.stripUuid);
    }
    return true;
}

bool GraphEditor::nodeNeedsChannel(juce::AudioProcessorGraph::NodeID source) const {
    return synth::planMakeChannel(audioEngine.getGraph(), source, macros).needsChannel;
}

juce::AudioProcessorGraph::NodeID GraphEditor::channelSourceForSelection() const {
    return synth::resolveChannelSource(audioEngine.getGraph(), selection.getSelected());
}

void GraphEditor::requestMakeChannel(juce::AudioProcessorGraph::NodeID source) {
    if (onMakeChannelRequested) {
        onMakeChannelRequested(source);
        return;
    }

    // Standalone fallback (no MainComponent): same pre-checks MainComponent::makeChannelForNode
    // runs, so a refused or already-channeled chain never pushes an undo step.
    auto& graph = audioEngine.getGraph();
    const auto plan = synth::planMakeChannel(graph, source, macros);
    if (!plan.needsChannel || plan.refusal.isNotEmpty()) {
        if (onStatusMessage)
            onStatusMessage(plan.refusal.isNotEmpty() ? plan.refusal
                                                      : juce::String("This chain already has a channel"));
        return;
    }
    auto* node = graph.getNodeForId(source);
    const juce::String name =
        node != nullptr && node->getProcessor() != nullptr ? node->getProcessor()->getName() : juce::String("Channel");
    auto doMake = [this, source, name] {
        makeChannelFromNode(source, name);
        updateComponents();
    };
    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doMake);
    else
        doMake();
    repaint();
}

void GraphEditor::addMakeChannelMenuItem(juce::PopupMenu& menu) {
    const auto source = channelSourceForSelection();
    if (source.uid == 0)
        return;
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    // Disabled rather than hidden once the chain has a channel — the "Locate Master" idiom.
    juce::PopupMenu::Item item("Make Channel");
    item.setEnabled(nodeNeedsChannel(source));
    item.action = [safeThis, source] {
        if (safeThis != nullptr)
            safeThis->requestMakeChannel(source);
    };
    menu.addItem(item);
}

std::vector<juce::String> GraphEditor::duplicateIntoChannelTargets(juce::AudioProcessorGraph::NodeID nodeId) const {
    auto& graph = audioEngine.getGraph();
    auto* node = graph.getNodeForId(nodeId);
    auto* processor = node != nullptr ? node->getProcessor() : nullptr;
    if (dynamic_cast<ModuleBase*>(processor) == nullptr || dynamic_cast<AttenuverterModule*>(processor) != nullptr ||
        dynamic_cast<ChannelStripModule*>(processor) != nullptr || dynamic_cast<MasterModule*>(processor) != nullptr ||
        synth::isTrackSourceNode(processor) || isSingletonIOModule(processor->getName()))
        return {};
    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isNotEmpty() && macros.findByMember(uuid) != nullptr)
        return {}; // already inside a macro — not "shared from outside"

    auto isChannelMacro = [&](const synth::Macro& macro) {
        for (const auto& member : macro.members)
            if (auto* memberNode = graph.getNodeForId(resolveMemberNodeId(member)))
                if (dynamic_cast<ChannelStripModule*>(memberNode->getProcessor()) != nullptr)
                    return true;
        return false;
    };

    // Every consumer, looking through a modulation attenuverter to what it modulates.
    const auto connections = graph.getConnections();
    std::vector<juce::AudioProcessorGraph::NodeID> consumers;
    auto addConsumer = [&](juce::AudioProcessorGraph::NodeID id) {
        if (std::find(consumers.begin(), consumers.end(), id) == consumers.end())
            consumers.push_back(id);
    };
    for (const auto& c : connections) {
        if (c.source.nodeID != nodeId)
            continue;
        if (!isAttenuverterNode(graph, c.destination.nodeID)) {
            addConsumer(c.destination.nodeID);
            continue;
        }
        for (const auto& out : connections)
            if (out.source.nodeID == c.destination.nodeID)
                addConsumer(out.destination.nodeID);
    }

    std::vector<juce::String> targets;
    bool feedsElsewhere = false;
    for (const auto id : consumers) {
        const juce::String consumerUuid = nodeUuidFor(id);
        const auto* macro = consumerUuid.isNotEmpty() ? macros.findByMember(consumerUuid) : nullptr;
        if (macro != nullptr && isChannelMacro(*macro)) {
            if (std::find(targets.begin(), targets.end(), macro->id) == targets.end())
                targets.push_back(macro->id);
        } else {
            feedsElsewhere = true;
        }
    }
    if (targets.empty() || (targets.size() == 1 && !feedsElsewhere))
        return {}; // not shared: it only feeds this one channel (or none)
    return targets;
}

// ...and joins the macro — a port only that cable used is spliced back out, and the copy's
// remaining boundary crossings get ports (addSelectionToMacro's T138 passes).
bool GraphEditor::duplicateIntoChannel(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& macroId) {
    const auto targets = duplicateIntoChannelTargets(nodeId);
    if (std::find(targets.begin(), targets.end(), macroId) == targets.end())
        return false;

    auto& graph = audioEngine.getGraph();
    auto* original = graph.getNodeForId(nodeId);
    if (original == nullptr)
        return false;

    // The duplicateSelection path: parameters and extra state (a Sampler's file, a wavetable) ride.
    const auto payload = synth::SnippetManager::extractSnippet(graph, {nodeId}, "Duplicate",
                                                               /*includeExtraState=*/true, macros);
    if (synth::SnippetManager::getModuleCount(payload) <= 0)
        return false;
    const int step = synth::ui::ModuleClipboard::kOffsetStep;
    const juce::Point<int> position(static_cast<int>(original->properties.getWithDefault("x", 0)) + step,
                                    static_cast<int>(original->properties.getWithDefault("y", 0)) + step);
    const auto added = synth::SnippetManager::insertSnippet(payload, graph, position, /*includeExtraState=*/true);
    if (added.empty())
        return false;
    const auto copyId = added.front();

    auto inThisChannel = [this, &macroId](juce::AudioProcessorGraph::NodeID id) {
        const juce::String uuid = nodeUuidFor(id);
        const auto* macro = uuid.isNotEmpty() ? macros.findByMember(uuid) : nullptr;
        return macro != nullptr && macro->id == macroId;
    };
    const auto connections = graph.getConnections(); // before any rewiring below

    // 1. The copy hears what the original hears. A modulation routing INTO the original is
    //    re-created through AudioEngine::addModRouting (its own hidden attenuverter), amount copied.
    for (const auto& c : connections) {
        if (c.destination.nodeID != nodeId)
            continue;
        if (!isAttenuverterNode(graph, c.source.nodeID)) {
            graph.addConnection({c.source, {copyId, c.destination.channelIndex}});
            continue;
        }
        for (const auto& in : connections) {
            if (in.destination.nodeID != c.source.nodeID || in.destination.channelIndex != 0)
                continue;
            const auto newAtten =
                audioEngine.addModRouting(in.source.nodeID, in.source.channelIndex, copyId, c.destination.channelIndex);
            auto* from = graph.getNodeForId(c.source.nodeID);
            auto* to = graph.getNodeForId(newAtten);
            if (from != nullptr && to != nullptr) {
                // Carry the amount across by paramID, so the copy's modulation depth matches.
                auto findAmount = [](juce::AudioProcessor* p) -> juce::RangedAudioParameter* {
                    for (auto* param : p->getParameters())
                        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                            if (ranged->getParameterID() == "amount")
                                return ranged;
                    return nullptr;
                };
                auto* fromAmount = findAmount(from->getProcessor());
                auto* toAmount = findAmount(to->getProcessor());
                if (fromAmount != nullptr && toAmount != nullptr)
                    toAmount->setValueNotifyingHost(fromAmount->getValue());
            }
        }
    }

    // 2. Every cable the original sends into this channel (directly, through a port, or through its
    //    modulation attenuverter) now comes from the copy instead. Everything else stays put.
    for (const auto& c : connections) {
        if (c.source.nodeID != nodeId)
            continue;
        bool intoChannel = inThisChannel(c.destination.nodeID);
        if (isAttenuverterNode(graph, c.destination.nodeID))
            for (const auto& out : connections)
                if (out.source.nodeID == c.destination.nodeID && inThisChannel(out.destination.nodeID))
                    intoChannel = true;
        if (!intoChannel)
            continue;
        graph.removeConnection(c);
        graph.addConnection({{copyId, c.source.channelIndex}, c.destination});
    }

    // 3. An inlet port of this channel now fed ONLY by the copy (directly or through an attenuverter
    //    the copy alone drives) is about to become wholly interior — splice it back out first, so the
    //    copy reaches its consumer straight, and a modulation leg stays one un-ported
    //    source -> attenuverter -> destination chain (buildMacroPortCrossingPlan's own rule for a mod
    //    routing with both real endpoints inside).
    if (auto* macro = macros.find(macroId)) {
        const auto after = graph.getConnections();
        std::vector<juce::String> interiorPorts;
        for (const auto& port : macro->ports) {
            if (!port.isInput)
                continue;
            const auto portId = resolveMemberNodeId(port.nodeUuid);
            bool anyIn = false, onlyCopy = true;
            for (const auto& c : after) {
                if (c.destination.nodeID != portId)
                    continue;
                anyIn = true;
                if (!isAttenuverterNode(graph, c.source.nodeID)) {
                    onlyCopy = onlyCopy && c.source.nodeID == copyId;
                    continue;
                }
                bool attenFed = false;
                for (const auto& in : after)
                    if (in.destination.nodeID == c.source.nodeID && in.destination.channelIndex == 0) {
                        attenFed = true;
                        onlyCopy = onlyCopy && in.source.nodeID == copyId;
                    }
                onlyCopy = onlyCopy && attenFed;
            }
            if (anyIn && onlyCopy)
                interiorPorts.push_back(port.nodeUuid);
        }
        for (const auto& portUuid : interiorPorts)
            if (auto* live = macros.find(macroId))
                spliceOutMacroPort(*live, portUuid);
    }

    // 4. Join the macro with addSelectionToMacro's T138 passes: a port for each of the copy's
    //    remaining boundary crossings (the inputs it inherited), and any port the join makes interior.
    const juce::String copyUuid = synth::AIStateMapper::ensureNodeUuid(graph.getNodeForId(copyId));
    const auto addPlan = buildMacroPortCrossingPlanForNewMembers(macroId, {copyUuid});
    const auto portsToSpliceOut = macroPortsThatBecomeInteriorOnAdd(macroId, {copyUuid});
    macros.addMember(macroId, copyUuid);
    if (!addPlan.empty())
        spliceMacroPorts(macroId, addPlan);
    for (const auto& portUuid : portsToSpliceOut)
        if (auto* live = macros.find(macroId))
            spliceOutMacroPort(*live, portUuid);
    return true;
}

void GraphEditor::requestDuplicateIntoChannel(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& macroId) {
    if (onDuplicateIntoChannelRequested) {
        onDuplicateIntoChannelRequested(nodeId, macroId);
        return;
    }
    const auto targets = duplicateIntoChannelTargets(nodeId);
    if (std::find(targets.begin(), targets.end(), macroId) == targets.end())
        return;
    auto doDuplicate = [this, nodeId, macroId] {
        duplicateIntoChannel(nodeId, macroId);
        updateComponents();
    };
    if (undoManager)
        undoManager->recordGraphAndMacroChange(audioEngine.getGraph(), macros, doDuplicate);
    else
        doDuplicate();
    repaint();
}

void GraphEditor::addDuplicateIntoChannelMenuItems(juce::PopupMenu& menu, juce::AudioProcessorGraph::NodeID nodeId) {
    const auto targets = duplicateIntoChannelTargets(nodeId);
    if (targets.empty())
        return;
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    auto actionFor = [safeThis, nodeId](const juce::String& macroId) {
        return [safeThis, nodeId, macroId] {
            if (safeThis != nullptr)
                safeThis->requestDuplicateIntoChannel(nodeId, macroId);
        };
    };
    auto nameFor = [this](const juce::String& macroId) {
        const auto* macro = macros.find(macroId);
        return macro != nullptr ? macro->name : juce::String("Channel");
    };
    if (targets.size() == 1) {
        menu.addItem("Duplicate into Channel: " + nameFor(targets.front()), actionFor(targets.front()));
        return;
    }
    juce::PopupMenu channels;
    for (const auto& macroId : targets)
        channels.addItem(nameFor(macroId), actionFor(macroId));
    menu.addSubMenu("Duplicate into Channel", channels);
}
