// GraphEditorDeleteHeal.cpp
//
// "Reconnect the chain": when deleteSelection()/requestDeleteModule() remove a run of
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
// heal", the same cycle/legality check AudioProcessorGraph::addConnection runs internally
// for a manual cable drag. Both call sites run this BEFORE the macro-port/attenuverter auto-delete
// sweeps, so a port a heal just gave a fresh cable to is no longer orphaned by the time those run.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "GraphEditorInternal.h" // GraphEditor::HealSplice's full definition
#include "Modules/ModuleBase.h"
#include "UI/Graph/PolyChain/PolyChainController.h"

#include <algorithm>
#include <functional>

namespace {

// One audio cable's identity from `nodeId`'s own side: which of nodeId's visible jacks it lands
// on, which node is on the other end, and which visible jack of THAT node it lands on. A stereo
// pair sharing both these jacks (two raw graph connections) is one leg, not two -- "count audio
// cables at the visible-jack/logical level".
struct AudioLeg {
    juce::AudioProcessorGraph::NodeID peerId;
    int peerJack = 0;
    int thisJack = 0;
    // Set by mergeStereoPairLegs: true means peerJack is the LEFT half of a genuine Dual-I/O L/R pair
    // of separate jacks on the peer, and healing this leg must wire its jack+1 too (Right), not
    // just fan jack alone the way a collapsed stereo jack's single visible index would
    // (resolvePolyLink has no notion of "two separate jacks" for one PolyLink).
    bool dualPair = false;
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

// A bare graph I/O node (Audio Input/Output) has no logical ports of its own, so audioVisibleJack
// gives each of its raw channels its OWN jack identity (matching resolvePolyLink's `identity`
// fallback and GraphEditorCables.cpp's cable-painting/hit-testing, which also treats each raw
// channel as its own distinct cable). That is the right notion for painting a cable, but wrong for
// THIS heal's eligibility count: a genuine stereo pair -- the deleted node's own L/R landing on the
// bare node's matching raw channels 0 and 1 -- is still one logical cable, same principle as any
// other collapsed stereo jack ("count audio cables at the visible-jack level"). Two legs
// merge only when they share BOTH the same peer node AND the same jack on THIS node's own side
// (`thisJack`) -- i.e. they really did come off one jack of the deleted node -- so two independent
// mono legs from a splitter/mixer that merely happen to land on raw 0 and 1 of the same bare I/O
// node (different `thisJack`s) are correctly left as two separate, heal-ineligible legs.
void mergeBareIoChannelPairs(std::vector<AudioLeg>& legs, juce::AudioProcessorGraph& graph) {
    for (size_t i = 0; i < legs.size(); ++i) {
        if (legs[i].peerJack != 0)
            continue;
        auto* peerNode = graph.getNodeForId(legs[i].peerId);
        if (peerNode == nullptr || dynamic_cast<ModuleBase*>(peerNode->getProcessor()) != nullptr)
            continue; // a ModuleBase peer's own mapInputChannel/mapOutputChannel already collapsed it
        // The peerJack==1 partner can land anywhere in the vector, not only after `i` (connection
        // order in the graph is not guaranteed) -- scan the whole vector, not just the tail.
        for (size_t j = 0; j < legs.size(); ++j) {
            if (j == i)
                continue;
            if (legs[j].peerId == legs[i].peerId && legs[j].thisJack == legs[i].thisJack && legs[j].peerJack == 1) {
                legs.erase(legs.begin() + static_cast<long>(j));
                if (j < i)
                    --i; // erasing an earlier element shifts `i`'s own slot back by one
                break;
            }
        }
    }
}

// A Dual-I/O module presents its Left and Right as two SEPARATE visible jacks (each genuinely mono:
// ModuleBase::mapStereoPairInput/Output's and every split-block module's own mapAudioLeg give each
// split leg polyVoiceSpan 1), a collapsed one as ONE jack covering both raw legs. A stereo cable
// between two such modules is therefore one logical leg however each end presents it, and shows up
// in the raw edges as two legs that agree on one side or on both:
//   * split on both ends (the default patch's Oscillator -> Filter -> VCA chain): Left to Left and
//     Right to Right -- `dualPair`;
//   * the peer split, this collapsed: the peer's Left and Right both land on this one jack --
//     `dualPair` (the peer is the one with two jacks), and
//   * this split, the peer collapsed: the peer's one jack lands on this module's Left and Right --
//     merged but not `dualPair`, since the peer has a single jack to heal onto.
// Requires the split side's first jack to be the Left of a labelled pair (a collapsed jack is just
// "Audio"; a module that is always split, the Channel Strip, labels Left/Right too), so a genuine
// two-independent-input module (a mixer or splitter whose jacks are not a labelled pair, a Voice
// Mixer's eight voice inputs) still counts as two real legs, matching TwoAudioInputsNeverHeals.
// Two different peers are a mix the user built by hand and are left unmerged ("no heal for that
// side") rather than guessed at.
void mergeStereoPairLegs(std::vector<AudioLeg>& legs, juce::AudioProcessorGraph& graph, const ModuleBase* thisMb,
                         bool thisIsInput) {
    if (thisMb == nullptr)
        return;
    for (size_t i = 0; i < legs.size(); ++i) {
        auto* peerNode = graph.getNodeForId(legs[i].peerId);
        auto* peerMb = peerNode != nullptr ? dynamic_cast<ModuleBase*>(peerNode->getProcessor()) : nullptr;
        if (peerMb == nullptr)
            continue;
        const bool peerSplit = detail::jackIsLeftOfSplitPair(*peerMb, legs[i].peerJack, !thisIsInput);
        const bool thisSplit = detail::jackIsLeftOfSplitPair(*thisMb, legs[i].thisJack, thisIsInput);
        if (!peerSplit && !thisSplit)
            continue;
        for (size_t j = 0; j < legs.size(); ++j) {
            if (j == i || legs[j].peerId != legs[i].peerId)
                continue;
            const bool peerNext = legs[j].peerJack == legs[i].peerJack + 1;
            const bool thisNext = legs[j].thisJack == legs[i].thisJack + 1;
            const bool peerSame = legs[j].peerJack == legs[i].peerJack;
            const bool thisSame = legs[j].thisJack == legs[i].thisJack;
            const bool bothSplit = peerSplit && thisSplit && peerNext && thisNext;
            const bool peerOnlySplit = peerSplit && peerNext && thisSame;
            const bool thisOnlySplit = thisSplit && !peerSplit && peerSame && thisNext;
            if (!bothSplit && !peerOnlySplit && !thisOnlySplit)
                continue;
            legs[i].dualPair = bothSplit || peerOnlySplit;
            legs.erase(legs.begin() + static_cast<long>(j));
            if (j < i)
                --i;
            break;
        }
    }
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
    mergeBareIoChannelPairs(legs.incoming, graph);
    mergeBareIoChannelPairs(legs.outgoing, graph);

    auto* thisNode = graph.getNodeForId(nodeId);
    auto* thisMb = thisNode != nullptr ? dynamic_cast<ModuleBase*>(thisNode->getProcessor()) : nullptr;
    mergeStereoPairLegs(legs.incoming, graph, thisMb, /*thisIsInput=*/true);
    mergeStereoPairLegs(legs.outgoing, graph, thisMb, /*thisIsInput=*/false);
    return legs;
}

// Walks from `leg`'s peer through consecutive heal-eligible deleted nodes (exactly one audio in,
// one audio out) to the surviving node at the far end, stepping via each hop's OTHER leg:
// `viaIncoming` reads each hop's incoming leg (walking upstream), false reads its outgoing leg
// (walking downstream). Returns nullopt the moment a hop is deleted but not heal-eligible (a
// branch/fan node) -- the whole run is then left unhealed (heal only single isolated
// deletions as a fallback). Also returns nullopt on revisiting a node already walked:
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
        // captureHealSplices' own comment on why (the macro-port delete preference owns that case).
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

// Validates one visible-jack pair's healed connection exactly like a user-drawn cable would be --
// isAnInputTo for a cycle (canConnect alone doesn't check one), then canConnect per leg (valid
// channel, not already connected) -- without touching the graph. Kept separate from the actual
// connect so a Dual-I/O pair's Left and Right jacks can both be validated before either is wired:
// "All-or-nothing... never Left-only", now applying across the two SEPARATE jacks a
// Dual-I/O pair needs, not just across one jack's own multi-voice fan.
bool validateHealJackPair(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID upId, ModuleBase* upMb,
                          int upJack, juce::AudioProcessorGraph::NodeID downId, ModuleBase* downMb, int downJack) {
    const auto link = GraphEditor::resolvePolyLink(upMb, upJack, downMb, downJack);
    if (link.voiceCount <= 0 || graph.isAnInputTo(downId, upId))
        return false;
    for (int v = 0; v < link.voiceCount; ++v) {
        const juce::AudioProcessorGraph::Connection candidate{{upId, link.sourceChannel(v)},
                                                              {downId, link.destChannel(v)}};
        if (!graph.canConnect(candidate))
            return false;
    }
    return true;
}

// The raw channel a visible jack's audio lands on (the head of its first audio target), or -1.
int audioHeadChannel(const ModuleBase& mb, int jack, bool isInput) {
    for (const auto& t : mb.getJackTargets(jack, isInput))
        if (t.role == PortRole::Audio || t.role == PortRole::Other)
            return t.rawHeadChannel;
    return -1;
}

// A split upstream pair (two jacks, each one leg) healed onto ONE collapsed downstream jack that
// owns both legs: Left to the jack's first raw channel and Right to its second. resolvePolyLink would
// broadcast the Left jack onto both (the established behaviour for a hand-dragged cable), which is
// not what a heal restoring a stereo cable wants. Returns false when the downstream jack is not a
// two-leg jack, so the caller falls back to the ordinary single-cable heal.
bool healSplitPairIntoCollapsedJack(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID upId,
                                    ModuleBase* upMb, int upJack, juce::AudioProcessorGraph::NodeID downId,
                                    ModuleBase* downMb, int downJack, bool& wired) {
    wired = false;
    if (upMb == nullptr || downMb == nullptr)
        return false;
    const ModuleBase::JackTarget* pair = nullptr;
    const auto targets = downMb->getJackTargets(downJack, /*isInput=*/true);
    for (const auto& t : targets)
        if (t.role == PortRole::Audio && t.voiceSpan == 2)
            pair = &t;
    if (pair == nullptr)
        return false;

    const int srcLeft = audioHeadChannel(*upMb, upJack, /*isInput=*/false);
    const int srcRight = audioHeadChannel(*upMb, upJack + 1, /*isInput=*/false);
    if (srcLeft < 0 || srcRight < 0 || graph.isAnInputTo(downId, upId))
        return true;

    const juce::AudioProcessorGraph::Connection left{{upId, srcLeft}, {downId, pair->rawHeadChannel}};
    const juce::AudioProcessorGraph::Connection right{{upId, srcRight}, {downId, pair->rawHeadChannel + 1}};
    if (!graph.canConnect(left) || !graph.canConnect(right))
        return true;
    graph.addConnection(left);
    graph.addConnection(right);
    wired = true;
    return true;
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
    // dedicated feature and preference (spliceCableOnMacroPortDelete, default OFF: the
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
            continue; // "more audio legs (mixer, splitters, 2 ins) deletes as today"

        const auto up = walkToSurvivor(graph, seedLegs.incoming.front(), isDeleted, isMacroPort, /*viaIncoming=*/true);
        const auto down =
            walkToSurvivor(graph, seedLegs.outgoing.front(), isDeleted, isMacroPort, /*viaIncoming=*/false);
        if (!up || !down || isDeleted(up->peerId) || isDeleted(down->peerId))
            continue;

        const HealSplice splice{up->peerId, down->peerId, up->peerJack, down->peerJack, up->dualPair, down->dualPair};
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
    // A heal restores wiring the survivors already had; it must not read as a first cable that makes a module poly.
    synth::ui::PolyChainController::SuppressJoin keepPolyAsIs(*polyChain_);
    for (const auto& splice : splices) {
        auto* upNode = graph.getNodeForId(splice.upstreamId);
        auto* downNode = graph.getNodeForId(splice.downstreamId);
        if (upNode == nullptr || downNode == nullptr)
            continue; // defensive -- captureHealSplices only ever names survivors

        auto* upMb = dynamic_cast<ModuleBase*>(upNode->getProcessor());
        auto* downMb = dynamic_cast<ModuleBase*>(downNode->getProcessor());

        // A genuine Dual-I/O L/R pair on BOTH ends (captureHealSplices' mergeStereoPairLegs)
        // heals as two SEPARATE jack pairs, Left (jack) then Right (jack+1) -- resolvePolyLink has
        // no notion of "two distinct jacks" within one PolyLink, unlike a single collapsed stereo
        // jack's own multi-voice fan. Both are validated before either is wired.
        if (splice.upstreamDualPair && !splice.downstreamDualPair) {
            bool wired = false;
            if (healSplitPairIntoCollapsedJack(graph, splice.upstreamId, upMb, splice.upstreamJack, splice.downstreamId,
                                               downMb, splice.downstreamJack, wired))
                continue;
        }
        if (!validateHealJackPair(graph, splice.upstreamId, upMb, splice.upstreamJack, splice.downstreamId, downMb,
                                  splice.downstreamJack))
            continue;
        const bool healBothLegs = splice.upstreamDualPair && splice.downstreamDualPair;
        if (healBothLegs && !validateHealJackPair(graph, splice.upstreamId, upMb, splice.upstreamJack + 1,
                                                  splice.downstreamId, downMb, splice.downstreamJack + 1))
            continue;

        connectPorts(splice.upstreamId, splice.upstreamJack, splice.downstreamId, splice.downstreamJack,
                     /*isMidi=*/false, /*recordUndo=*/false);
        if (healBothLegs)
            connectPorts(splice.upstreamId, splice.upstreamJack + 1, splice.downstreamId, splice.downstreamJack + 1,
                         /*isMidi=*/false, /*recordUndo=*/false);
    }
}
