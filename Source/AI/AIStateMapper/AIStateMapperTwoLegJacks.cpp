// AIStateMapper — saved cables from a joined two-leg output jack, on the APPLY side.
//
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

} // namespace detail
} // namespace synth
