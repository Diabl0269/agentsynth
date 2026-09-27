// GraphEditorDeleteHeal.cpp
//
// FRO23 "reconnect the chain": when deleteSelection()/requestDeleteModule() remove a run of
// modules that each have exactly one incoming and one outgoing audio cable, splice the surviving
// upstream/downstream endpoints together instead of leaving the chain broken. GraphEditor is
// declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of
// the class.
//
// Two-phase, the same shape as macroPortDeletionNeighbors()/autoDeleteOrphanedAttenuverter()
// (GraphEditorCommands.cpp/GraphEditorSelection.cpp call sites, MacroGroupControllerPorts.cpp):
// captureHealSplices() reads the graph BEFORE any node is removed (a deleted node's own
// connections are still there to classify), and healDeletedChain() connects the captured survivor
// endpoints AFTER removal, re-validating each with graph.canConnect() first -- "if invalid, don't
// heal" (FRO23), the same cycle/legality check AudioProcessorGraph::addConnection runs internally
// for a manual cable drag. Both call sites run this BEFORE the macro-port/attenuverter auto-delete
// sweeps, so a port a heal just gave a fresh cable to is no longer orphaned by the time those run.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "Modules/ModuleBase.h"

#include <algorithm>
#include <functional>

namespace {

// One audio cable's identity from `nodeId`'s own side: which of nodeId's visible jacks it lands
// on, which node is on the other end, and which visible jack of THAT node it lands on. A stereo
// pair sharing both these jacks (two raw graph connections) is one leg, not two -- "count audio
// cables at the visible-jack/logical level" (FRO23).
struct AudioLeg {
    juce::AudioProcessorGraph::NodeID peerId;
    int peerJack = 0;
    int thisJack = 0;
    bool operator==(const AudioLeg& o) const noexcept {
        return peerId == o.peerId && peerJack == o.peerJack && thisJack == o.thisJack;
    }
};

struct AudioLegs {
    std::vector<AudioLeg> incoming, outgoing;
};

// The visible jack a raw channel end lands on, but ONLY when that end classifies as audio --
// nullopt for MIDI, ModCV/Pitch/Gate (an attenuverter chain's own edges included), or an
// out-of-range channel on a bare graph I/O node. PortRole::Other counts as audio here too, same as
// GraphEditorInternal.h's collectSmartAudioLegs treats it ("role == PortRole::Audio ||
// PortRole::Other"): most plain modules never override mapInputChannel/mapOutputChannel to declare
// PortRole::Audio explicitly -- ModuleBase's own default leaves an unclassified channel at
// PortRole::Other, which is exactly what a simple module's one audio jack is. A ModuleBase side
// asks mapInputChannel/mapOutputChannel; a bare graph I/O node (Audio Input/Output, no ModuleBase,
// no logical ports) has no jacks of its own, so its raw channel IS its jack, same as
// resolvePolyLink's own `identity` fallback.
std::optional<int> audioVisibleJack(juce::AudioProcessorGraph::Node* node, int rawChannel, bool isInput) {
    if (node == nullptr)
        return std::nullopt;
    if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor())) {
        const LogicalPort p = isInput ? mb->mapInputChannel(rawChannel) : mb->mapOutputChannel(rawChannel);
        bool isAudio = p.role == PortRole::Audio || p.role == PortRole::Other;
        // A lazily-mapped input channel that a knob actually drives as mod CV (its own role left at
        // the PortRole::Other default rather than declared ModCV) is not an audio leg either --
        // same exclusion collectSmartAudioLegs applies via audioJackIsModCvDest.
        if (isAudio && isInput) {
            for (const auto& t : mb->getModulationTargets())
                if (t.channelIndex == rawChannel)
                    isAudio = false;
        }
        return isAudio ? std::optional<int>(p.visibleJackIndex) : std::nullopt;
    }
    const int channels =
        isInput ? node->getProcessor()->getTotalNumInputChannels() : node->getProcessor()->getTotalNumOutputChannels();
    return rawChannel < channels ? std::optional<int>(rawChannel) : std::nullopt;
}

// Every distinct audio leg touching `nodeId`, read straight from the raw graph connections -- the
// same source of truth macroPortDeletionNeighbors()/autoDeleteOrphanedAttenuverter()
// (MacroGroupControllerPorts.cpp) read, not buildVisibleCables() (paint-oriented, keyed off live
// component geometry this needs to run without -- capture happens mid-delete transaction).
AudioLegs classifyAudioLegs(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId) {
    AudioLegs legs;
    for (const auto& c : graph.getConnections()) {
        if (c.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex)
            continue;

        if (c.destination.nodeID == nodeId) {
            const auto thisJack = audioVisibleJack(graph.getNodeForId(nodeId), c.destination.channelIndex, true);
            const auto peerJack = audioVisibleJack(graph.getNodeForId(c.source.nodeID), c.source.channelIndex, false);
            if (!thisJack || !peerJack)
                continue;
            const AudioLeg leg{c.source.nodeID, *peerJack, *thisJack};
            if (std::find(legs.incoming.begin(), legs.incoming.end(), leg) == legs.incoming.end())
                legs.incoming.push_back(leg);
        } else if (c.source.nodeID == nodeId) {
            const auto thisJack = audioVisibleJack(graph.getNodeForId(nodeId), c.source.channelIndex, false);
            const auto peerJack =
                audioVisibleJack(graph.getNodeForId(c.destination.nodeID), c.destination.channelIndex, true);
            if (!thisJack || !peerJack)
                continue;
            const AudioLeg leg{c.destination.nodeID, *peerJack, *thisJack};
            if (std::find(legs.outgoing.begin(), legs.outgoing.end(), leg) == legs.outgoing.end())
                legs.outgoing.push_back(leg);
        }
    }
    return legs;
}

// Walks from `leg`'s peer through consecutive heal-eligible deleted nodes (exactly one audio in,
// one audio out) to the surviving node at the far end, stepping via each hop's OTHER leg:
// `viaIncoming` reads each hop's incoming leg (walking upstream), false reads its outgoing leg
// (walking downstream). Returns nullopt the moment a hop is deleted but not heal-eligible (a
// branch/fan node) -- the whole run is then left unhealed, FRO23's "if that's complex, heal only
// single isolated deletions" fallback. Also returns nullopt on revisiting a node already walked:
// a deleted 1-in/1-out RUN can itself be wired into a cycle (e.g. two deleted nodes wired
// X->Y->X), which would otherwise spin this loop forever on the message thread -- a cycle among
// the deleted nodes is exactly as unhealable as one involving a survivor.
std::optional<AudioLeg> walkToSurvivor(juce::AudioProcessorGraph& graph, AudioLeg leg,
                                       const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isDeleted,
                                       const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isMacroPort,
                                       bool viaIncoming) {
    std::vector<juce::AudioProcessorGraph::NodeID> visited;
    while (isDeleted(leg.peerId)) {
        // A macro port anywhere along the run is not this heal's to splice through -- see
        // captureHealSplices' own comment on why (FRO235 owns that case).
        if (isMacroPort(leg.peerId))
            return std::nullopt;
        if (std::find(visited.begin(), visited.end(), leg.peerId) != visited.end())
            return std::nullopt;
        visited.push_back(leg.peerId);

        const auto hopLegs = classifyAudioLegs(graph, leg.peerId);
        if (hopLegs.incoming.size() != 1 || hopLegs.outgoing.size() != 1)
            return std::nullopt;
        leg = viaIncoming ? hopLegs.incoming.front() : hopLegs.outgoing.front();
    }
    return leg;
}

} // namespace

std::vector<GraphEditor::HealSplice>
GraphEditor::captureHealSplices(const std::vector<juce::AudioProcessorGraph::NodeID>& deletedIds) const {
    std::vector<HealSplice> splices;
    if (!reconnectChainOnDeleteEnabled)
        return splices;

    auto& graph = audioEngine.getGraph();
    const auto isDeleted = [&](juce::AudioProcessorGraph::NodeID id) {
        return std::find(deletedIds.begin(), deletedIds.end(), id) != deletedIds.end();
    };
    // Deleting a macro port NODE ITSELF is not this heal's call -- that already has its own
    // dedicated feature and preference (FRO235's spliceCableOnMacroPortDelete, default OFF: the
    // cable is dropped unless the user opts in). Letting this generic, default-ON heal splice
    // through a deleted port too would silently override that default for every macro port
    // deletion. A port anywhere in a deleted RUN leaves the whole run unhealed past it, same as any
    // other branching/ineligible hop -- checked both at the seed and inside walkToSurvivor.
    const auto isMacroPort = [&](juce::AudioProcessorGraph::NodeID id) { return macroController_.nodeIsMacroPort(id); };

    for (const auto seed : deletedIds) {
        if (isMacroPort(seed))
            continue;
        const auto seedLegs = classifyAudioLegs(graph, seed);
        if (seedLegs.incoming.size() != 1 || seedLegs.outgoing.size() != 1)
            continue; // "more audio legs (mixer, splitters, 2 ins) deletes as today" (FRO23)

        const auto up = walkToSurvivor(graph, seedLegs.incoming.front(), isDeleted, isMacroPort, /*viaIncoming=*/true);
        const auto down =
            walkToSurvivor(graph, seedLegs.outgoing.front(), isDeleted, isMacroPort, /*viaIncoming=*/false);
        if (!up || !down || isDeleted(up->peerId) || isDeleted(down->peerId))
            continue;

        const HealSplice splice{up->peerId, down->peerId, up->peerJack, down->peerJack};
        // Every heal-eligible node in the same deleted run resolves to the identical splice
        // (walking off either end lands on the same two survivors) -- dedupe rather than
        // special-case "am I the run's first node".
        if (std::find(splices.begin(), splices.end(), splice) == splices.end())
            splices.push_back(splice);
    }
    return splices;
}

void GraphEditor::healDeletedChain(const std::vector<HealSplice>& splices) {
    auto& graph = audioEngine.getGraph();
    for (const auto& splice : splices) {
        auto* upNode = graph.getNodeForId(splice.upstreamId);
        auto* downNode = graph.getNodeForId(splice.downstreamId);
        if (upNode == nullptr || downNode == nullptr)
            continue; // defensive -- captureHealSplices only ever names survivors

        auto* upMb = dynamic_cast<ModuleBase*>(upNode->getProcessor());
        auto* downMb = dynamic_cast<ModuleBase*>(downNode->getProcessor());
        const auto link = resolvePolyLink(upMb, splice.upstreamJack, downMb, splice.downstreamJack);

        // Same validation a user-drawn cable is subject to -- "if invalid, don't heal" (FRO23).
        // AudioProcessorGraph::canConnect only checks per-leg legality (valid channel, not
        // already connected, MIDI-ness matches); it does NOT reject a cycle -- that is
        // isAnInputTo's job, which a manual cable drag relies on the same way (JUCE's own
        // addConnection tolerates the resulting cycle at the storage level, but the graph can no
        // longer be topologically sorted for rendering). A cycle only depends on the two NODES,
        // not the leg, so it is checked once per splice, before the per-leg legality loop.
        // All-or-nothing: a stereo pair heals as a whole cable or not at all, never Left-only.
        bool everyLegValid = link.voiceCount > 0 && !graph.isAnInputTo(splice.downstreamId, splice.upstreamId);
        for (int v = 0; v < link.voiceCount && everyLegValid; ++v) {
            const juce::AudioProcessorGraph::Connection candidate{
                {splice.upstreamId, link.sourceRawChannel + v * link.sourceStride},
                {splice.downstreamId, link.destRawChannel + v}};
            everyLegValid = graph.canConnect(candidate);
        }
        if (!everyLegValid)
            continue;

        connectPorts(splice.upstreamId, splice.upstreamJack, splice.downstreamId, splice.downstreamJack,
                     /*isMidi=*/false, /*recordUndo=*/false);
    }
}
