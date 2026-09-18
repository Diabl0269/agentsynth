// Concern: transport/timeline publish — handing a TimelineDoc to the audio thread (publishTimeline) and the mixer solo
// gate it keeps honest.

#include "AudioEngine.h"
#include "Mixer/SoloAudibleSet.h"
#include "Modules/ChannelStripModule.h"
#include "Timeline/AutomationBinding.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <map>

void AudioEngine::publishTimeline(const synth::TimelineDoc& doc) {
    // Every graph change already has to reach this call
    // (docs/architecture/app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step), which
    // makes it the one place the mixer's soloed-strip count can be kept honest: a deleted, replaced or undone soloed
    // strip must never leave the whole mix gated silent.
    refreshSoloGate();

    auto snapshot = synth::TimelineSnapshot::buildFrom(doc);

    // The snapshot's address is stable across the move into publish() (unique_ptr moves the
    // pointer, not the object), so the binding table can point at it before it is handed over.
    const synth::TimelineSnapshot* snapshotPtr = snapshot.get();

    auto table = std::make_unique<synth::AutomationBindingTable>();
    table->snapshot = snapshotPtr;

    if (snapshotPtr != nullptr && !snapshotPtr->lanes.empty()) {
        // uuid -> node, built once: resolving lane-by-lane against getNodes() would be
        // O(lanes * nodes) on a doc that can carry hundreds of lanes.
        std::map<juce::String, juce::AudioProcessorGraph::Node*> nodesByUuid;
        for (auto* node : mainProcessorGraph.getNodes()) {
            if (node == nullptr)
                continue;
            const juce::String uuid = node->properties["uuid"].toString();
            if (uuid.isNotEmpty())
                nodesByUuid.emplace(uuid, node);
        }

        table->bindings.reserve(snapshotPtr->lanes.size());

        for (std::size_t laneIndex = 0; laneIndex < snapshotPtr->lanes.size(); ++laneIndex) {
            const auto& lane = snapshotPtr->lanes[laneIndex];
            if (lane.nodeUuid[0] == '\0' || lane.paramId[0] == '\0')
                continue; // never bound to anything — not orphaned, just unbound

            const auto found = nodesByUuid.find(juce::String(lane.nodeUuid));
            if (found == nodesByUuid.end())
                continue; // orphaned: the lane is retained in the doc but automates nothing

            // The shared resolver — exact id match on a hosted plugin's LIVE instance
            // parameter, a narrow legacy index fallback, or (for every non-plugin node) exactly the
            // findParameterByID this replaced. `resolved.orphaned` is the doc's business (surfaced by
            // TimelineReconciler, not decided here); the binding build only cares whether something
            // came back to write into.
            const auto resolved = synth::resolveLaneParameter(found->second->getProcessor(), juce::String(lane.paramId),
                                                              lane.paramIndexHint);
            if (!resolved.resolved())
                continue; // the node exists but no longer has a safely-resolvable parameter

            synth::AutomationBindingTable::Binding binding;
            binding.laneIndex = static_cast<int>(laneIndex);
            binding.node = found->second; // refcounted — see AutomationApplier.h
            binding.param = resolved.rangedParam;
            binding.hostedParam = resolved.hostedParam;
            binding.nodeID = found->second->nodeID; // identity for the UI feed's events
            table->bindings.push_back(std::move(binding));
        }
    }

    // The streamer is brought to THIS snapshot before it is published, so a clip the audio
    // thread is about to see either has a stream already opening or is one the streamer deliberately
    // declined (unresolvable, or past its pool cap) — never one it has not been told about. Runs on
    // the message thread and opens no files itself: it resolves refs and hands the resulting paths
    // to its prefetch thread.
    if (snapshotPtr != nullptr)
        clipStreamer_.syncToSnapshot(*snapshotPtr);

    // Snapshot FIRST, bindings SECOND. The whole coherence argument in AutomationApplier.h rests on
    // this order — do not reorder these two lines.
    timelineSnapshots.publish(std::move(snapshot));
    automationBindings_.publish(std::move(table));
}

namespace {
// Soloed strips in `graph`, treating `overrideNode` (if valid) as having solo state
// `overrideSoloed` instead of whatever its flag says right now.
int countSoloedStrips(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID overrideNode = {},
                      bool overrideSoloed = false) {
    int count = 0;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* strip = dynamic_cast<ChannelStripModule*>(node->getProcessor());
        if (strip == nullptr)
            continue;
        const bool soloed = node->nodeID == overrideNode ? overrideSoloed : strip->isSoloed();
        if (soloed)
            ++count;
    }
    return count;
}

// FRO15 (docs/mixer.md §5.15): hand each strip the per-leg mask synth::computeSoloAudibleLegs
// worked out, OPEN BEFORE CLOSE. Pass 1 only ever ORs bits in, pass 2 assigns: a render pass
// landing between the two sees a strip momentarily MORE audible, never one wrongly silent. (A
// single pass would let strip A close its send leg a block before strip B's bus opens, which is
// audible as a dropout on every solo click.)
void publishSoloAudibleMasks(juce::AudioProcessorGraph& graph) {
    const auto masks = synth::computeSoloAudibleLegs(graph);
    for (int pass = 0; pass < 2; ++pass) {
        for (auto* node : graph.getNodes()) {
            if (node == nullptr)
                continue;
            auto* strip = dynamic_cast<ChannelStripModule*>(node->getProcessor());
            if (strip == nullptr)
                continue;
            const auto found = masks.find(node->nodeID);
            const juce::uint32 mask = found != masks.end() ? found->second : ~0u;
            if (pass == 0)
                strip->orSoloAudibleMask(mask);
            else
                strip->setSoloAudibleMask(mask);
        }
    }
}

// Opens every leg of every strip. Used on the un-solo path BEFORE the flag drops, so the gate can
// never be seen closed against a mask that was computed while something was still soloed.
void openEverySoloAudibleMask(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* strip = dynamic_cast<ChannelStripModule*>(node->getProcessor()))
                strip->setSoloAudibleMask(~0u);
}
} // namespace

void AudioEngine::refreshSoloGate() {
    // Masks first, count second: while the count is still 0 the gate is open and the masks are not
    // consulted at all, so this order can never expose a half-published mask set.
    publishSoloAudibleMasks(mainProcessorGraph);
    soloedStripCount_.store(countSoloedStrips(mainProcessorGraph), std::memory_order_relaxed);
}

bool AudioEngine::setChannelStripSoloed(juce::AudioProcessorGraph::NodeID nodeId, bool soloed) {
    auto* node = mainProcessorGraph.getNodeForId(nodeId);
    auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    if (strip == nullptr)
        return false;

    // Ordered so no render pass sees "gate closed, nothing soloed" (every strip silent for a
    // block): soloing raises the strip's flag BEFORE the count; un-soloing drops the count first.
    if (soloed) {
        strip->setSoloed(true);
        refreshSoloGate();
    } else {
        openEverySoloAudibleMask(mainProcessorGraph);
        soloedStripCount_.store(countSoloedStrips(mainProcessorGraph, nodeId, false), std::memory_order_relaxed);
        strip->setSoloed(false);
        refreshSoloGate(); // re-close whatever is still gated by any OTHER soloed strip
    }
    return true;
}
