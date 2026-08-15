#pragma once

#include "../Transport/BlockTimeInfo.h"
#include "AutomationKernel.h"
#include "EpochExchange.h"
#include "TimelineSnapshot.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth {

// TL4-2: the resolved "which lane drives which live parameter" table, built on the message thread
// and published to the audio thread through EpochExchange<AutomationBindingTable>.
//
// Resolution (AudioEngine::publishTimeline) is a message-thread-only walk: for each lane, find the
// graph node whose `properties["uuid"]` equals the lane's nodeUuid, then the RangedAudioParameter
// on that node's processor whose paramID equals the lane's paramId. A lane that resolves to
// neither simply produces no binding — orphaned lanes are retained in the document (TL2-6 policy)
// but silently automate nothing.
//
// -- Why the node pointer is refcounted ---------------------------------------------------------
//
// `node` is a juce::AudioProcessorGraph::Node::Ptr, not a NodeID and not a raw pointer. A Node owns
// its processor, and holding a Ptr keeps that processor alive even after the node is removed from
// the graph — so `param` can never dangle, whatever happens to the graph between two publishes.
// Writing automation into a parameter of a node the graph no longer renders is harmless: nothing
// reads it, and the next publish drops the binding. The alternative (a NodeID re-looked-up per
// block) would mean walking the graph's node list on the audio thread, which is neither
// allocation-free nor lock-free.
//
// -- Why the table carries its own snapshot pointer ---------------------------------------------
//
// `laneIndex` indexes into `snapshot->lanes`, so a table is only meaningful against the exact
// snapshot it was resolved from. Rather than have the applier read the CURRENT snapshot out of the
// snapshot exchange (which could be a different one — see below), the table carries the pointer,
// and the applier reads lanes and points through it. Applier and bindings are therefore always
// coherent by construction.
//
// Lifetime argument for that raw pointer: the engine always publishes snapshot-then-bindings,
// back to back, on the message thread. So a published binding table's snapshot pointer is at most
// one publish behind the snapshot exchange's current value, and the snapshot exchange frees a
// retiree only once the audio epoch has advanced two blocks past the publish that retired it.
// Inside that window the audio thread has already loaded the NEWER bindings table (both exchanges
// are opened in the same render pass, bindings after the snapshot), so no live table can still be
// pointing at a freed snapshot. AudioEngine::shutdown() reclaims both exchanges after the audio
// callback is gone, bindings first is not required — reclaimAllUnsafe frees the tables and the
// snapshots without either reading the other.
//
// Split-brain of exactly one render pass is possible — Track In reading snapshot N+1 through the
// transport while the applier still holds table N pointing at snapshot N — and is harmless: both
// are valid published states, one block apart, and a block is the granularity everything else here
// already works at.
struct AutomationBindingTable {
    struct Binding {
        int laneIndex = -1;                        // into snapshot->lanes
        juce::AudioProcessorGraph::Node::Ptr node; // refcounted: keeps `param`'s owner alive (above)
        juce::RangedAudioParameter* param = nullptr;
        // Audio-thread-only evaluation state, owned by the table (which outlives every block it is
        // published for). `mutable` because the applier receives the table by const reference — the
        // exchange hands out a borrowed const view, and the cursor is the one part of it the audio
        // thread is allowed to write.
        mutable AutomationCursor cursor;
    };

    std::vector<Binding> bindings;
    // The snapshot `laneIndex` refers to. Borrowed, never owned — see the lifetime argument above.
    const TimelineSnapshot* snapshot = nullptr;
};

// TL4-2: pushes one block's worth of automation into live parameters. Audio thread only.
//
// Stateless by design — everything that has to persist between blocks (the per-lane segment
// cursors) lives in the published table, so swapping tables swaps the cursors with them and there
// is nothing to reset. The engine owns one instance purely as a call site.
class AutomationApplier {
public:
    // For each resolved binding: evaluate its lane at `info.startPpq` (block-rate — one value per
    // block, at the block's start position) and store it into the bound parameter.
    //
    // Semantics, all deliberate:
    //   - **Not playing => nothing is written.** A stopped transport leaves every knob free for the
    //     user to turn; automation only takes the wheel while the timeline is moving. TL4-4's record
    //     modes refine this (latch/touch need to know a knob was moved *while* playing); until then
    //     the rule is simply `if (!info.playing) return;`.
    //   - **setValue, never setValueNotifyingHost.** This is a plain store of the normalised value.
    //     Notifying would fire parameter listeners from the audio thread — the host's automation
    //     lane and our own UI both — for every automated parameter on every block. UI reflection of
    //     automation is TL4-5's job and belongs on a message-thread timer reading the parameter.
    //   - **Values clamp into the parameter's CURRENT range**, via
    //     RangedAudioParameter::convertTo0to1, which clamps to [0, 1] internally. A lane authored
    //     against a wider range (or a build that later narrowed one) therefore pins at the endpoint
    //     instead of wrapping or asserting. A non-finite value is skipped outright rather than
    //     handed to JUCE, which would trip a debug assertion inside NormalisableRange.
    //
    // No allocation, no locks, no logging, no juce::String. `noexcept`.
    void applyBlock(const AutomationBindingTable& table, const BlockTimeInfo& info) noexcept;
};

} // namespace synth
