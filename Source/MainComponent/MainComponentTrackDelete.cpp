// MainComponentTrackDelete.cpp — deleting a track: what leaves with it (its bound node, its macro, its mixer strip and
// the modules only it used), the one undo step that brings it all back, and the Cmd+Backspace confirm. MainComponent
// is declared in MainComponent.h; the add-track flows are in MainComponentTrackCreation.cpp.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"

#include "Mixer/ChannelMacroLookup.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Timeline/TrackRemovalSet.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include <functional>
#include <set>
#include <vector>

namespace {

// What deleting `track` takes from the mixer: its own strip and, while that strip's insert list is a straight line,
// the inserts between the track's source and the strip; plus every other strip's send slot that feeds the strip (the
// slot goes with the strip, so no "no target" send is left behind). Both empty when the track has no strip of its own:
// nothing reaches one, it is a bus, or other tracks feed the channel too (that one stays for them). Cables on the
// removed nodes go with them.
struct OwnStrip {
    std::vector<juce::AudioProcessorGraph::NodeID> nodes;
    std::vector<std::pair<juce::AudioProcessorGraph::NodeID, int>> incomingSends; // {source strip, slot}
};

OwnStrip ownStripOf(const synth::MixerSnapshot& snapshot, synth::TrackId track) {
    OwnStrip own;
    for (const auto& column : snapshot.columns) {
        if (column.kind != synth::MixerColumn::Kind::Strip || !column.linkedToTrack ||
            column.feedingTracks.size() != 1 || column.feedingTracks.front() != track)
            continue;
        own.nodes.push_back(column.nodeId);
        if (column.insertChainIsLinear)
            for (const auto& insert : column.inserts)
                own.nodes.push_back(insert.nodeId);
        for (const auto& other : snapshot.columns)
            if (other.nodeId != column.nodeId)
                for (const auto& send : other.sends)
                    if (!send.keyTarget && send.targetNodeId == column.nodeId)
                        own.incomingSends.emplace_back(other.nodeId, send.slot);
        break;
    }
    return own;
}

// Everything that goes with `track` besides its row, as node ids: its bound node, its own mixer strip, the macro that
// holds the bound node (every module in it, nested groups too) and the modules only these used (modulesOnlyUsedBy).
// The macro stays out of it, and the track leaves as it did before the macro was part of it, when the box is shared:
// another track is bound to a node in it, or a strip in it is fed by another track.
using NodeFinder = std::function<juce::AudioProcessorGraph::Node*(const juce::String&)>;

std::vector<juce::AudioProcessorGraph::NodeID>
nodesLeavingWithTrack(juce::AudioProcessorGraph& graph, const synth::TimelineDoc& timelineDoc, GraphEditor& graphEditor,
                      const AudioEngine& audioEngine, const NodeFinder& findNodeByUuid, synth::TrackId track,
                      const OwnStrip& own) {
    const auto* existing = timelineDoc.getTrack(track);
    const auto& macros = graphEditor.getMacros();
    std::set<std::uint32_t> seed;
    for (auto id : own.nodes)
        seed.insert(id.uid);
    const auto* bound = existing != nullptr ? findNodeByUuid(existing->bindingUuid) : nullptr;
    if (bound != nullptr)
        seed.insert(bound->nodeID.uid);

    // Nodes other tracks are bound to are never taken, nor is a macro that holds one.
    std::set<juce::String> otherTracksUuids;
    for (const auto& other : timelineDoc.getTracks())
        if (other.id != track && other.bindingUuid.isNotEmpty())
            otherTracksUuids.insert(other.bindingUuid);

    std::set<juce::String> macroUuids; // the track's macro and everything drawn inside it
    if (bound != nullptr) {
        if (const auto* channel = synth::nearestChannelMacro(graph, macros, existing->bindingUuid)) {
            macroUuids = macros.descendantMembers(channel->id);
            bool shared = false;
            for (const auto& uuid : macroUuids)
                shared = shared || otherTracksUuids.count(uuid) != 0;
            for (const auto& column : synth::buildMixerSnapshot(graph, timelineDoc, macros).columns)
                if (column.kind == synth::MixerColumn::Kind::Strip && macroUuids.count(column.uuid) != 0)
                    for (auto feeding : column.feedingTracks)
                        shared = shared || feeding != track;
            if (shared)
                macroUuids.clear();
        }
    }
    for (const auto& uuid : macroUuids)
        if (const auto* node = findNodeByUuid(uuid))
            seed.insert(node->nodeID.uid);

    std::set<std::uint32_t> keep;
    for (const auto* node : graph.getNodes())
        if (graphEditor.isOutputDockNode(node->nodeID))
            keep.insert(node->nodeID.uid);
    for (const auto& uuid : otherTracksUuids)
        if (const auto* node = findNodeByUuid(uuid))
            keep.insert(node->nodeID.uid);
    for (const auto& macro : macros.getAll()) // a module in a group that stays is that group's
        for (const auto& uuid : macro.members)
            if (macroUuids.count(uuid) == 0)
                if (const auto* node = findNodeByUuid(uuid))
                    keep.insert(node->nodeID.uid);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> cables;
    for (const auto& connection : graph.getConnections())
        cables.emplace_back(connection.source.nodeID.uid, connection.destination.nodeID.uid);
    std::set<std::uint32_t> relays; // the hidden Attenuverter that carries each modulation routing
    for (const auto& routing : audioEngine.getModulationRoutings())
        if (routing.kind == AudioEngine::RoutingKind::AttenuverterChain)
            relays.insert(routing.attenuverterNodeID.uid);
    const auto onlyUsed = synth::modulesOnlyUsedBy(cables, seed, keep, relays);
    seed.insert(onlyUsed.begin(), onlyUsed.end());

    std::vector<juce::AudioProcessorGraph::NodeID> ids;
    for (auto uid : seed)
        if (graph.getNodeForId(juce::AudioProcessorGraph::NodeID(uid)) != nullptr)
            ids.emplace_back(uid);
    return ids;
}

} // namespace

void MainComponent::deleteTrack(synth::TrackId track) {
    const auto* existing = timelineDoc.getTrack(track);
    if (existing == nullptr)
        return;

    // The rows and columns are pictured while the track is still there, so they can shrink away.
    timelinePanel.noteTracksLeaving();
    bottomDock.getMixerPanel().noteColumnsLeaving();

    auto& graph = audioEngine.getGraph();
    const auto own = ownStripOf(synth::buildMixerSnapshot(graph, timelineDoc, graphEditor.getMacros()), track);
    const auto doomed = nodesLeavingWithTrack(
        graph, timelineDoc, graphEditor, audioEngine, [this](const juce::String& uuid) { return findNodeByUuid(uuid); },
        track, own);

    // ONE undo step covering all three domains: the track, its node, its macro and the modules only it used, and its
    // mixer strip (with the inserts and the sends into it) disappear together, and come back together; the macro half
    // returns the box the chain sat in.
    const auto removeTrackAndNodes = [this, &graph, track, own, &doomed] {
        if (!doomed.empty()) {
            graphEditor.getModMatrix().clearRows();
            for (const auto& [source, slot] : own.incomingSends)
                synth::removeSend(graph, source, slot);
            graphEditor.removeNodesNow(doomed, /*healChain=*/false, /*narrowDetach=*/true);
        }
        timelineDoc.removeTrack(track);
    };
    {
        CardGlideAnimator::Scope glideScope(graphEditor.getCardGlide()); // the cards and the macro shrink away
        undoManager.recordGraphTimelineAndMacroChange(graph, timelineDoc, graphEditor.getMacros(), removeTrackAndNodes);
        reconcileTimelineAfterGraphChange();
    }
    timelinePanel.finishTrackListChange();
    bottomDock.getMixerPanel().finishColumnChange();
}

// Cmd+Backspace on a focused row. Asks first unless the person switched the question off; the deletion itself is
// deleteTrack, so the menu's one undo step is unchanged. "Don't ask again" counts only when they confirm.
void MainComponent::deleteTrackAfterConfirm(synth::TrackId track) {
    const auto* existing = timelineDoc.getTrack(track);
    if (existing == nullptr)
        return;
    const auto* settings = appProperties.getUserSettings();
    if (settings != nullptr && !settings->getBoolValue(synth::ui::kAskBeforeDeletingTrackKey, true)) {
        deleteTrack(track);
        return;
    }
    juce::Component::SafePointer<MainComponent> safeThis(this);
    synth::ui::confirmDeleteTrack(synth::ui::deleteTrackConfirmText(existing->name),
                                  [safeThis, track](bool confirmed, bool dontAskAgain) {
                                      auto* self = safeThis.getComponent();
                                      if (self == nullptr || !confirmed)
                                          return;
                                      if (dontAskAgain)
                                          if (auto* userSettings = self->appProperties.getUserSettings()) {
                                              userSettings->setValue(synth::ui::kAskBeforeDeletingTrackKey, "0");
                                              userSettings->saveIfNeeded();
                                          }
                                      self->deleteTrack(track);
                                  });
}
