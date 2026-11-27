// AIStateMapper — saved cables on the wrong raw channel, repaired on the APPLY side: two-leg output jacks, a right leg
// summed onto the far left beside its own right cable, then channels no jack heads.
//
// Two-leg output jacks.
// A joined Oscillator or Wavetable output in mono mode is one Audio jack that owns both legs (raw 0 and kRightBase),
// the way a joined Channel Strip output does. Projects saved while that jack carried the left leg alone hold its one
// cable as the left leg fanned onto both legs of the far pair (raw 0 -> far Left and raw 0 -> far Right). Loaded
// as-is, the jack would claim both legs while its raw edges played the left one twice, so the module's Width and Pan
// could never reach the far Right. The fan is re-pointed: raw 0 -> far Left stays, the jack's right leg takes far
// Right. With Width and Pan at rest both legs are the same samples, so the patch sounds as it did; a spread stack
// now arrives in stereo, which is the point.

#include "AIStateMapperInternal.h"
#include <vector>

namespace synth {
namespace detail {

namespace {

using Graph = juce::AudioProcessorGraph;

bool jackCoversRaw(const ModuleBase& mb, int jack, int raw, bool isInput) {
    for (const auto& t : mb.getJackTargets(jack, isInput))
        for (int v = 0; v < t.voiceSpan; ++v)
            if (t.rawHeadChannel + v * t.rawStride == raw)
                return true;
    return false;
}

// The right leg a joined jack 0 owns on `mb`'s output, or -1 when that jack is a single leg (split, poly, an FX pair
// whose legs are raw 0 and 1 and never fanned).
int twoLegJackRightLeg(const ModuleBase& mb) {
    if (mb.isDualIO() || !mb.hasSplitBlockStereo())
        return -1;
    const int right = mb.rightAudioLegChannel();
    return right > 1 && jackCoversRaw(mb, 0, right, /*isInput*/ false) ? right : -1;
}

// The far end's right input leg as a visible jack exposes it, or -1.
int farRightInputLeg(const Graph::Node& node) {
    auto* proc = node.getProcessor();
    auto* mb = dynamic_cast<ModuleBase*>(proc);
    if (mb == nullptr)
        return proc->getTotalNumInputChannels() >= 2 ? 1 : -1; // graph I/O: a contiguous pair
    const int right = mb->rightAudioLegChannel();
    if (right <= 0 || right >= proc->getTotalNumInputChannels() || mb->mapInputChannel(right).role != PortRole::Audio)
        return -1;
    const int visible = mb->getVisibleInputPortCount();
    for (int jack = 0; jack < visible; ++jack)
        if (jackCoversRaw(*mb, jack, right, /*isInput*/ true))
            return right;
    return -1;
}

} // namespace

void repointFannedTwoLegJackEdges(Graph& graph) {
    for (auto* node : graph.getNodes()) {
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        const int ownRight = mb != nullptr ? twoLegJackRightLeg(*mb) : -1;
        if (ownRight < 0)
            continue;
        std::vector<Graph::Connection> fanned;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID != node->nodeID || c.source.channelIndex != 0 || c.destination.isMIDI())
                continue;
            auto* far = graph.getNodeForId(c.destination.nodeID);
            if (far == nullptr || farRightInputLeg(*far) != c.destination.channelIndex)
                continue;
            // A fan, not a lone cable on far Right: the same leg also feeds a lower input of that node (its Left).
            bool alsoFeedsLeft = false;
            for (const auto& other : graph.getConnections())
                if (other.source == c.source && other.destination.nodeID == c.destination.nodeID &&
                    other.destination.channelIndex < c.destination.channelIndex && !other.destination.isMIDI())
                    alsoFeedsLeft = true;
            if (alsoFeedsLeft && !graph.isConnected({{node->nodeID, ownRight}, c.destination}))
                fanned.push_back(c);
        }
        for (const auto& c : fanned) {
            graph.removeConnection(c, Graph::UpdateKind::none);
            graph.addConnection({{node->nodeID, ownRight}, c.destination}, Graph::UpdateKind::none);
        }
    }
}

// ---- Channels no jack heads ----
// A module may keep raw input channels that no jack heads (the mono Oscillator's 7-12 and the mono envelopes' 1-7:
// poly's voice channels). While such a channel still headed a jack beside the real target, a cable arriving from an
// audio-role jack (a track's Macro In) could land on it: the card drew it on the right jack, but it moved nothing (a
// Glide cable with no ring). Projects saved then hold that edge. It is re-pointed onto the one channel that jack really
// heads, so the cable the person sees is the one that plays.

void repointDeadChannelEdges(Graph& graph) {
    std::vector<std::pair<Graph::Connection, int>> moves;
    for (const auto& c : graph.getConnections()) {
        if (c.destination.isMIDI())
            continue;
        auto* far = graph.getNodeForId(c.destination.nodeID);
        auto* mb = far != nullptr ? dynamic_cast<ModuleBase*>(far->getProcessor()) : nullptr;
        if (mb == nullptr)
            continue;
        const int raw = c.destination.channelIndex;
        if (mb->isReservedInputChannel(raw) || mb->isAutoPromotableModTarget(raw))
            continue; // a channel kept for a later cable, or a knob's own: never dead
        const auto port = mb->mapInputChannel(raw);
        if (port.isPolyGroupHead || port.role != PortRole::Other ||
            port.visibleJackIndex >= mb->getVisibleInputPortCount())
            continue;
        const auto targets = mb->getJackTargets(port.visibleJackIndex, /*isInput*/ true);
        if (targets.size() != 1 || targets.front().voiceSpan != 1 || targets.front().role == PortRole::Other ||
            targets.front().rawHeadChannel == raw)
            continue;
        moves.emplace_back(c, targets.front().rawHeadChannel);
    }
    for (const auto& [c, raw] : moves) {
        graph.removeConnection(c, Graph::UpdateKind::none);
        const Graph::Connection moved{c.source, {c.destination.nodeID, raw}};
        if (!graph.isConnected(moved))
            graph.addConnection(moved, Graph::UpdateKind::none);
    }
}

// ---- A right leg summed into the far left beside its own right cable ----
// Splitting a source whose far end was a joined two-leg jack (a mono VCA) used to wire the source's right leg onto the
// far Left as a "summed second cable" while the same right leg already fed the far Right. The far Left then played
// Left plus Right and the far Right only Right: louder on the left, thin on the right, and the extra cable drew as the
// same line. A source whose left leg feeds the far Left and whose right leg feeds the far Right has no reason to also
// send the right leg to the far Left, so that cable goes.

namespace {
// The raw right input leg a visible jack of `node` covers (a split right jack or a joined jack's second leg), or -1.
int visibleRightInputLeg(const Graph::Node& node) {
    auto* mb = dynamic_cast<ModuleBase*>(node.getProcessor());
    if (mb == nullptr)
        return -1;
    const int right = mb->rightAudioLegChannel();
    if (right <= 0 || right >= node.getProcessor()->getTotalNumInputChannels())
        return -1;
    const int visible = mb->getVisibleInputPortCount();
    for (int jack = 0; jack < visible; ++jack)
        if (jackCoversRaw(*mb, jack, right, /*isInput*/ true))
            return right;
    return -1;
}

void dropRightLegSummedBesideRightPair(Graph& graph) {
    std::vector<Graph::Connection> summed;
    for (const auto& c : graph.getConnections()) {
        if (c.source.isMIDI() || c.destination.isMIDI() || c.destination.channelIndex != 0 ||
            c.source.channelIndex <= 0)
            continue;
        auto* from = graph.getNodeForId(c.source.nodeID);
        auto* far = graph.getNodeForId(c.destination.nodeID);
        auto* mb = from != nullptr ? dynamic_cast<ModuleBase*>(from->getProcessor()) : nullptr;
        if (mb == nullptr || far == nullptr || mb->rightAudioLegChannel() != c.source.channelIndex)
            continue;
        const int farRight = visibleRightInputLeg(*far);
        if (farRight > 0 && graph.isConnected({{c.source.nodeID, 0}, c.destination}) &&
            graph.isConnected({c.source, {c.destination.nodeID, farRight}}))
            summed.push_back(c);
    }
    for (const auto& c : summed)
        graph.removeConnection(c, Graph::UpdateKind::none);
}
} // namespace

// The two-leg repair runs first: it moves a leg onto a channel this pass then leaves alone.
void repairSavedJackEdges(juce::AudioProcessorGraph& graph) {
    repointFannedTwoLegJackEdges(graph);
    dropRightLegSummedBesideRightPair(graph);
    repointDeadChannelEdges(graph);
}

} // namespace detail
} // namespace synth
