// MainComponentTrackDuplicate.cpp — "Duplicate Track" (Cmd+D on a focused track row, and the row's
// right-click menu): a copy of the track directly below it with its modules, clips and automation as
// ONE undo step. The modules travel through the same extract/insert path copy-paste uses
// (SnippetManager), so a macro and its ports come along; this unit adds what a paste does not do —
// re-creating the cables that left the copied set (to Master, a shared bus, a shared modulator)
// and copying the track's timeline half. MainComponent is declared in MainComponent.h; the rest of
// its implementation lives in the sibling MainComponent*.cpp units next to this one.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "MainComponentInternal.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/MasterModule.h"
#include "SnippetManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Timeline/TrackColour.h"
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// Every node at or downstream of a Master: the output dock (Master, Rec Tap, Audio Output). A lone
// track "owns" these by reachability, but they are shared singletons and must never be copied.
std::set<juce::uint32> outputDockNodes(const juce::AudioProcessorGraph& graph, const synth::ui::ConnectionIndex& cables) {
    std::set<juce::uint32> dock;
    std::vector<NodeID> pending;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<MasterModule*>(node->getProcessor()) != nullptr)
            pending.push_back(node->nodeID);
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!dock.insert(id.uid).second)
            continue;
        for (const auto& c : cables.outOf(id))
            pending.push_back(c.destination.nodeID);
    }
    return dock;
}

bool isAttenuverter(const juce::AudioProcessorGraph& graph, NodeID id) {
    const auto* node = graph.getNodeForId(id);
    return node != nullptr && dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr;
}

juce::RangedAudioParameter* amountParameter(juce::AudioProcessorGraph::Node* node) {
    if (node == nullptr || node->getProcessor() == nullptr)
        return nullptr;
    for (auto* param : node->getProcessor()->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
            ranged && ranged->getParameterID() == "amount")
            return ranged;
    return nullptr;
}

// The owned nodes cabled to the track's own chain without going through the output dock. The ownership rule also
// adopts a free-standing patch that feeds Audio Output as a "feeder" of the lone track that reaches the dock, so
// ownership alone would copy it; a node only joins when a cable path through owned nodes (or a hidden
// attenuverter) links it to the track's start, never through Master or what follows it.
std::vector<NodeID> attachedToTrack(const juce::AudioProcessorGraph& graph, const synth::ui::ConnectionIndex& cables,
                                    NodeID start, const std::set<juce::uint32>& owned,
                                    const std::set<juce::uint32>& dock) {
    std::set<juce::uint32> seen;
    std::vector<NodeID> pending{start};
    std::vector<NodeID> attached;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (dock.count(id.uid) != 0 || !seen.insert(id.uid).second)
            continue;
        if (owned.count(id.uid) == 0 && !isAttenuverter(graph, id))
            continue;
        if (owned.count(id.uid) != 0)
            attached.push_back(id);
        for (const auto& c : cables.touching(id)) {
            if (c.source.nodeID == id)
                pending.push_back(c.destination.nodeID);
            else
                pending.push_back(c.source.nodeID);
        }
    }
    return attached;
}

// Where the copy's cards land: the original set's left edge, one gap below its lowest card.
juce::Point<int> placementBelow(juce::AudioProcessorGraph& graph, const std::vector<NodeID>& originals) {
    const auto origin = synth::SnippetManager::selectionOrigin(graph, originals);
    int bottom = origin.y;
    for (const auto id : originals) {
        const auto* node = graph.getNodeForId(id);
        if (node == nullptr || node->getProcessor() == nullptr)
            continue;
        const int y = static_cast<int>(node->properties.getWithDefault("y", 0));
        bottom = juce::jmax(bottom, y + GraphEditor::estimateModuleSize(node->getProcessor()->getName()).y);
    }
    return synth::LayoutUtil::snap({origin.x, bottom + synth::LayoutUtil::kGridSize * 5});
}

// Re-creates every cable between a copied node and the rest of the graph, pointing at the copy: the
// copy is wired into Master / a shared bus like the original, and hears the same shared sources. A
// modulation routing into a copied node from outside the set is re-created through the engine (its own
// hidden attenuverter), amount carried over; routings between copied nodes already came with the snippet.
void rewireBoundary(AudioEngine& engine, const std::map<int, NodeID>& copies) {
    auto& graph = engine.getGraph();
    const auto copyOf = [&copies](NodeID id) -> std::optional<NodeID> {
        const auto it = copies.find(static_cast<int>(id.uid));
        return it != copies.end() ? std::optional<NodeID>(it->second) : std::nullopt;
    };
    const auto connections = graph.getConnections(); // before any rewiring below
    for (const auto& c : connections) {
        const auto from = copyOf(c.source.nodeID);
        const auto to = copyOf(c.destination.nodeID);
        if (from.has_value() == to.has_value())
            continue; // wholly inside (already copied) or wholly outside (not ours)
        const auto outside = from ? c.destination.nodeID : c.source.nodeID;
        if (isAttenuverter(graph, outside))
            continue; // modulation legs are handled below
        graph.addConnection({{from.value_or(c.source.nodeID), c.source.channelIndex},
                             {to.value_or(c.destination.nodeID), c.destination.channelIndex}});
    }
    for (const auto& into : connections) {
        const auto to = copyOf(into.destination.nodeID);
        if (!to || copyOf(into.source.nodeID) || !isAttenuverter(graph, into.source.nodeID))
            continue;
        for (const auto& feed : connections) {
            if (feed.destination.nodeID != into.source.nodeID || feed.destination.channelIndex != 0 ||
                copyOf(feed.source.nodeID))
                continue;
            const auto atten =
                engine.addModRouting(feed.source.nodeID, feed.source.channelIndex, *to, into.destination.channelIndex);
            auto* fromAmount = amountParameter(graph.getNodeForId(into.source.nodeID));
            auto* toAmount = amountParameter(graph.getNodeForId(atten));
            if (fromAmount != nullptr && toAmount != nullptr)
                toAmount->setValueNotifyingHost(fromAmount->getValue());
        }
    }
}

} // namespace

// The graph half, then the timeline half, inside the caller's ONE undo transaction. Returns the new
// track, or an invalid id when the copy failed (the caller checked kMaxTracks, so only a snippet
// the insert path refuses can fail).
synth::TrackId MainComponent::duplicateTrackBody(synth::TrackId trackId, const juce::String& copyName) {
    auto& graph = audioEngine.getGraph();
    const auto* source = timelineDoc.getTrack(trackId);
    if (source == nullptr)
        return {};

    // The nodes this track plays and no other track does (the ownership rule automation lanes use),
    // minus the shared output dock.
    const auto owners = resolveAutomationOwners();
    const synth::ui::ConnectionIndex cables(graph); // read-only walks below; the insert and rewire come after
    const auto dock = outputDockNodes(graph, cables);
    std::set<juce::uint32> owned;
    for (auto* node : graph.getNodes()) {
        const auto owner = owners.find(detail::ownershipKey(*node));
        if (owner != owners.end() && owner->second == trackId && dock.count(node->nodeID.uid) == 0)
            owned.insert(node->nodeID.uid);
    }
    std::vector<NodeID> originals;
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == source->bindingUuid && source->bindingUuid.isNotEmpty())
            originals = attachedToTrack(graph, cables, node->nodeID, owned, dock);

    std::map<juce::String, juce::String> uuidRemap;
    if (!originals.empty()) {
        const auto payload = synth::SnippetManager::extractSnippet(graph, originals, "Duplicate Track",
                                                                   /*includeExtraState=*/true, graphEditor.getMacros());
        const auto dropPos = placementBelow(graph, originals);
        std::vector<synth::Macro> copiedMacros;
        std::map<int, NodeID> copies;
        const auto added = synth::SnippetManager::insertSnippet(payload, graph, dropPos, /*includeExtraState=*/true,
                                                                &copiedMacros, /*trustedPayload=*/false, &copies);
        if (added.empty())
            return {};

        for (const auto& [oldUid, newId] : copies) {
            const auto* original = graph.getNodeForId(NodeID(static_cast<juce::uint32>(oldUid)));
            const juce::String oldUuid = original != nullptr ? original->properties["uuid"].toString() : juce::String();
            const juce::String newUuid = synth::AIStateMapper::ensureNodeUuid(graph.getNodeForId(newId));
            if (oldUuid.isNotEmpty() && newUuid.isNotEmpty())
                uuidRemap[oldUuid] = newUuid;
        }

        // A channel macro is named after its track; the copy takes the copy's name so two mixer columns never
        // read the same.
        for (auto& macro : copiedMacros) {
            if (macro.name == source->name)
                macro.name = copyName;
            graphEditor.getMacros().add(macro);
        }
        rewireBoundary(audioEngine, copies);
        graphEditor.updateComponents();
    }

    const int index = static_cast<int>(timelineDoc.getTracks().size());
    return timelineDoc.duplicateTrack(trackId, copyName, synth::ui::trackPaletteColour(index).getARGB(), uuidRemap);
}

void MainComponent::duplicateTrack(synth::TrackId trackId) {
    const auto* source = timelineDoc.getTrack(trackId);
    if (source == nullptr || source->kind == synth::TrackKind::Automation)
        return;
    if (static_cast<int>(timelineDoc.getTracks().size()) >= synth::TimelineDoc::kMaxTracks) {
        statusBar.showMessage("Could not duplicate the track - the timeline is full");
        return;
    }
    const juce::String copyName = source->name + " copy";

    synth::TrackId copy;
    timelinePanel.armTrackDuplicateGlide(trackId);
    const bool pushed = undoManager.recordGraphTimelineAndMacroChange(
        audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(),
        [this, trackId, &copyName, &copy] { copy = duplicateTrackBody(trackId, copyName); });

    reconcileTimelineAfterGraphChange();
    if (copy.isValid() && pushed) {
        timelinePanel.focusTrackRow(copy); // the copy is what the user will want to act on next
        statusBar.showMessage("Duplicated track: " + copyName);
    } else {
        timelinePanel.armTrackDuplicateGlide({});
        statusBar.showMessage("Could not duplicate the track");
    }
}
