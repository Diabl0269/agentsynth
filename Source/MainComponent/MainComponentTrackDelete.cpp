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

using NodeFinder = std::function<juce::AudioProcessorGraph::Node*(const juce::String&)>;

// The uuids of the macro that goes with `track`: the one holding its bound node (every module in it, nested groups
// too), or empty when the track has none or the box is shared. Shared means a track outside `together` is bound to a
// node in it, or a strip in it is fed by a track outside `together`; `together` is the set deleted in the same gesture,
// so two tracks that only share a macro with each other still count as taking it. The question "remove the macro too?"
// is asked exactly when this is not empty.
std::set<juce::String> macroLeavingWith(juce::AudioProcessorGraph& graph, const synth::TimelineDoc& timelineDoc,
                                        GraphEditor& graphEditor, const NodeFinder& findNodeByUuid,
                                        synth::TrackId track, const std::set<synth::TrackId>& together) {
    const auto* existing = timelineDoc.getTrack(track);
    if (existing == nullptr || findNodeByUuid(existing->bindingUuid) == nullptr)
        return {};
    const auto& macros = graphEditor.getMacros();
    const auto* channel = synth::nearestChannelMacro(graph, macros, existing->bindingUuid);
    if (channel == nullptr)
        return {};
    std::set<juce::String> outsideUuids;
    for (const auto& other : timelineDoc.getTracks())
        if (together.count(other.id) == 0 && other.bindingUuid.isNotEmpty())
            outsideUuids.insert(other.bindingUuid);
    auto macroUuids = macros.descendantMembers(channel->id);
    bool shared = false;
    for (const auto& uuid : macroUuids)
        shared = shared || outsideUuids.count(uuid) != 0;
    for (const auto& column : synth::buildMixerSnapshot(graph, timelineDoc, macros).columns)
        if (column.kind == synth::MixerColumn::Kind::Strip && macroUuids.count(column.uuid) != 0)
            for (auto feeding : column.feedingTracks)
                shared = shared || together.count(feeding) == 0;
    return shared ? std::set<juce::String>{} : macroUuids;
}

// The tracks of `tracks` whose macro would leave with them (macroLeavingWith, the set counted as leaving together).
std::set<synth::TrackId> tracksTakingAMacro(juce::AudioProcessorGraph& graph, const synth::TimelineDoc& timelineDoc,
                                            GraphEditor& graphEditor, const NodeFinder& findNodeByUuid,
                                            const std::vector<synth::TrackId>& tracks) {
    const std::set<synth::TrackId> together(tracks.begin(), tracks.end());
    std::set<synth::TrackId> taking;
    for (const auto id : tracks)
        if (!macroLeavingWith(graph, timelineDoc, graphEditor, findNodeByUuid, id, together).empty())
            taking.insert(id);
    return taking;
}

// Everything that goes with `track` besides its row, as node ids: its bound node, its own mixer strip, the macro that
// holds the bound node (macroLeavingWith) and the modules only these used (modulesOnlyUsedBy). The macro stays out of
// it, and the track leaves as it did before the macro was part of it, when the box is shared.
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

    const auto macroUuids = macroLeavingWith(graph, timelineDoc, graphEditor, findNodeByUuid, track, {track});
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

void MainComponent::deleteTrack(synth::TrackId track) { deleteTrackKeepingMacro(track, false); }

// `keepMacro` drops only the track row (its clips and lanes): the bound node, the macro, every module, the mixer strip
// and the cables stay as they are, so the macro is a plain macro whose Track In is unclaimed (a track's binding chip
// can pick it again) and whose strip is a channel no track feeds (an orphan strip in the mixer).
void MainComponent::deleteTrackKeepingMacro(synth::TrackId track, bool keepMacro) {
    const auto* existing = timelineDoc.getTrack(track);
    if (existing == nullptr)
        return;

    // The rows and columns are pictured while the track is still there, so they can shrink away.
    timelinePanel.noteTracksLeaving();
    bottomDock.getMixerPanel().noteColumnsLeaving();

    auto& graph = audioEngine.getGraph();
    OwnStrip own;
    std::vector<juce::AudioProcessorGraph::NodeID> doomed;
    if (!keepMacro) {
        own = ownStripOf(synth::buildMixerSnapshot(graph, timelineDoc, graphEditor.getMacros()), track);
        doomed = nodesLeavingWithTrack(
            graph, timelineDoc, graphEditor, audioEngine,
            [this](const juce::String& uuid) { return findNodeByUuid(uuid); }, track, own);
    }

    // ONE undo step covering all three domains: the track, its node, its macro and the modules only it used, and its
    // mixer strip (with the inserts and the sends into it) disappear together, and come back together; the macro half
    // returns the box the chain sat in. Keeping the macro, only the track row goes, in the same single step.
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

// Every route that deletes tracks from a row (the menu, Cmd+Backspace, several selected rows) ends here.
// - A track with a macro that would go with it asks "Remove macro / Keep macro / Cancel" (Remember my choice), on the
//   same shared window as deleting a macro. With the question off (kAskBeforeDeletingTrackKey) the stored answer
//   applies, "remove" when none was (what a delete did before the question).
// - Without such a macro, `alwaysAsk` (the keyboard and the several-rows menu) asks the plain "Delete?" and the menu's
//   single item deletes at once, both as before.
// "Don't ask again" / "Remember my choice" count only when the person confirms.
void MainComponent::deleteTracksAsking(const std::vector<synth::TrackId>& tracks, bool alwaysAsk) {
    if (tracks.empty())
        return;
    const auto taking = tracksTakingAMacro(
        audioEngine.getGraph(), timelineDoc, graphEditor,
        [this](const juce::String& uuid) { return findNodeByUuid(uuid); }, tracks);
    const auto* settings = appProperties.getUserSettings();
    const bool asking = settings == nullptr || settings->getBoolValue(synth::ui::kAskBeforeDeletingTrackKey, true);
    juce::Component::SafePointer<MainComponent> safeThis(this);
    // All `tracks` as ONE undo step; the ones in `keepMacroOf` keep their macro, the others leave whole.
    const auto deleteNow = [safeThis](const std::vector<synth::TrackId>& doomed,
                                      const std::set<synth::TrackId>& keepMacroOf) {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;
        const AppUndoManager::ScopedUndoGroup group(self->undoManager);
        for (const auto id : doomed)
            self->deleteTrackKeepingMacro(id, keepMacroOf.count(id) != 0);
    };

    if (!taking.empty()) {
        if (!asking) {
            const auto choice = synth::ui::rememberedDeleteTrackMacroChoice(*settings);
            deleteNow(tracks,
                      choice == synth::ui::DeleteTrackMacroChoice::keepMacro ? taking : std::set<synth::TrackId>{});
            return;
        }
        const auto* first = timelineDoc.getTrack(tracks.front());
        const auto text = tracks.size() == 1 && first != nullptr
                              ? synth::ui::deleteTrackMacroConfirmText(first->name)
                              : synth::ui::deleteTrackMacroConfirmText(static_cast<int>(tracks.size()),
                                                                       static_cast<int>(taking.size()));
        synth::ui::confirmDeleteTrackMacro(
            text, [safeThis, deleteNow, tracks, taking](synth::ui::DeleteTrackMacroChoice choice, bool remember) {
                auto* self = safeThis.getComponent();
                if (self == nullptr || choice == synth::ui::DeleteTrackMacroChoice::cancel)
                    return;
                if (remember)
                    if (auto* userSettings = self->appProperties.getUserSettings())
                        synth::ui::rememberDeleteTrackMacroChoice(*userSettings, choice);
                deleteNow(tracks,
                          choice == synth::ui::DeleteTrackMacroChoice::keepMacro ? taking : std::set<synth::TrackId>{});
            });
        return;
    }

    if (!alwaysAsk || !asking) {
        deleteNow(tracks, {});
        return;
    }
    const auto* first = timelineDoc.getTrack(tracks.front());
    const auto text = tracks.size() == 1 && first != nullptr
                          ? synth::ui::deleteTrackConfirmText(first->name)
                          : synth::ui::deleteTrackConfirmText(static_cast<int>(tracks.size()));
    synth::ui::confirmDeleteTrack(text, [safeThis, deleteNow, tracks](bool confirmed, bool dontAskAgain) {
        auto* self = safeThis.getComponent();
        if (self == nullptr || !confirmed)
            return;
        if (dontAskAgain)
            if (auto* userSettings = self->appProperties.getUserSettings()) {
                userSettings->setValue(synth::ui::kAskBeforeDeletingTrackKey, "0");
                userSettings->saveIfNeeded();
            }
        deleteNow(tracks, {});
    });
}

// The row menu's single Delete item: no question unless a macro would go with the track.
void MainComponent::deleteTrackFromMenu(synth::TrackId track) { deleteTracksAsking({track}, false); }

// Cmd+Backspace on a focused row: the whole selection when the row is one of several selected, else the row.
void MainComponent::deleteTrackAfterConfirm(synth::TrackId track) { deleteTracksAsking(tracksActedOnBy(track), true); }
