#pragma once

#include "../Transport/BlockTimeInfo.h"
#include "AutomationKernel.h"
#include "AutomationRecorder.h" // GestureClaims / AutomationRecordState — the audio-visible half
#include "AutomationUiFeed.h"   // the audio -> UI reflection ring
#include "EpochExchange.h"
#include "TimelineSnapshot.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <limits>
#include <vector>

namespace synth {

// The resolved "which lane drives which live parameter" table, built on the message thread and
// published to the audio thread through EpochExchange<AutomationBindingTable>.
//
// Resolution (AudioEngine::publishTimeline) is a message-thread-only walk: for each lane, find the
// graph node whose `properties["uuid"]` equals the lane's nodeUuid, then the RangedAudioParameter
// on that node's processor whose paramID equals the lane's paramId. A lane that resolves to
// neither produces no binding — orphaned lanes stay in the document but silently automate nothing.
//
// `node` is a juce::AudioProcessorGraph::Node::Ptr, not a raw pointer or NodeID: a Node owns its
// processor, so holding the Ptr keeps `param` alive even after the node leaves the graph. Writing
// into a parameter of a node the graph no longer renders is harmless — nothing reads it, and the
// next publish drops the binding.
//
// `laneIndex` indexes into `snapshot->lanes`, so a table is only meaningful against the exact
// snapshot it was resolved from — the table therefore carries that snapshot's pointer rather than
// having the applier read whatever the snapshot exchange currently holds, which could differ. The
// engine always publishes snapshot-then-bindings back to back on the message thread, and the
// snapshot exchange frees a retiree only two audio blocks after the publish that retired it, so a
// live table can never end up pointing at a freed snapshot. A one-render-pass split brain (Track In
// reading snapshot N+1 while the applier still holds table N -> snapshot N) is possible and
// harmless: both are valid published states one block apart.
struct AutomationBindingTable {
    struct Binding {
        int laneIndex = -1;                        // into snapshot->lanes
        juce::AudioProcessorGraph::Node::Ptr node; // refcounted: keeps `param`'s owner alive (above)
        juce::RangedAudioParameter* param = nullptr;
        // Populated INSTEAD of `param` for a hosted-plugin instance parameter with no
        // NormalisableRange (see AutomationBinding.h) — exactly one of the two is ever non-null on
        // a real binding.
        juce::AudioProcessorParameter* hostedParam = nullptr;
        // The node's id, captured alongside `node` at build time rather than re-read from
        // `node->nodeID` at push time — this is the identity the UI feed's events carry.
        juce::AudioProcessorGraph::NodeID nodeID;
        // Audio-thread-only evaluation state, owned by the table (which outlives every block it is
        // published for). `mutable` because the applier receives the table by const reference — the
        // exchange hands out a borrowed const view, and the cursor is the one part of it the audio
        // thread is allowed to write.
        mutable AutomationCursor cursor;
        // Dedupe: the last normalised value pushed to the UI feed for this binding, so a static
        // lane pushes once rather than once per block. NaN means "never pushed".
        mutable float lastPushedNormalized = std::numeric_limits<float>::quiet_NaN();
    };

    std::vector<Binding> bindings;
    // The snapshot `laneIndex` refers to. Borrowed, never owned — see the lifetime argument above.
    const TimelineSnapshot* snapshot = nullptr;
};

// Pushes one block's worth of automation into live parameters. Audio thread only.
//
// Stateless by design — everything that has to persist between blocks (the per-lane segment
// cursors) lives in the published table, so swapping tables swaps the cursors with them and there
// is nothing to reset. The engine owns one instance purely as a call site.
class AutomationApplier {
public:
    // For each resolved binding: evaluate its lane at `info.startPpq` (block-rate — one value per
    // block, at the block's start position) and store it into the bound parameter.
    //
    // `recordState` is AutomationRecorder's audio-visible half (null when no recorder is installed,
    // which is the "playback only" build and every unit test that doesn't care). It is read, never
    // written, and only through atomics — see AutomationRecorder.h.
    //
    // Semantics, all deliberate:
    //   - **Not playing => nothing is written.** A stopped transport leaves every knob free for the
    //     user to turn; automation only takes the wheel while the timeline is moving.
    //   - **Per-lane record modes** (`TimelineSnapshot::LaneInfo::recordMode`), on top of that:
    //       Off          — never written. The lane is inert in both directions.
    //       Read         — always written (the default).
    //       Touch/Latch  — written UNLESS the bound parameter is CLAIMED, i.e. a user gesture on it
    //                      is in flight. The hand wins for as long as it is on the knob; the instant
    //                      the claim is released, playback resumes from the lane.
    //       Write        — never written while global record is armed. Recording overwrites the
    //                      span, and playing stale data back into the same parameter would fight the
    //                      hand that is writing it. With global record OFF, Write reads like Read.
    //     An out-of-range recordMode cannot occur (TimelineDoc rejects one) and falls through to Read.
    //   - **setValue, never setValueNotifyingHost.** Notifying would fire parameter listeners from
    //     the audio thread — the host's automation lane and our own UI both — for every automated
    //     parameter on every block. It would also feed the player's own output straight back into
    //     AutomationRecorder's parameter listener, making every playback pass re-record itself. UI
    //     reflection goes through `uiFeed` instead. Pinned by
    //     AutomationRecordTest.RecorderNeverHearsTheApplier.
    //   - **Values clamp into the parameter's CURRENT range**, via
    //     RangedAudioParameter::convertTo0to1, which clamps to [0, 1] internally. A non-finite value
    //     is skipped outright rather than handed to JUCE, which would trip a debug assertion.
    //   - **A `hostedParam` binding (a hosted plugin's own parameter) has no NormalisableRange to
    //     convert through** — juce::HostedAudioProcessorParameter is a sibling hierarchy to
    //     RangedAudioParameter, not a subtype (see HostedPluginModule.h and AutomationBinding.h).
    //     Its lane's RangeSnapshot IS the denorm -> 0..1 map instead: `(denormalised -
    //     lane.minValue) / (lane.maxValue - lane.minValue)`, clamped into [0, 1]. Every other rule
    //     above — playing-only, record modes, plain setValue, UI reflection — applies identically
    //     to both kinds of binding.
    //   - **UI reflection rides `uiFeed`, not a notification.** After the plain store above, if this
    //     write's normalised value differs from the binding's `lastPushedNormalized` (or that field
    //     is still NaN), push `{nodeID, param, newNormalised}` to `uiFeed` and update
    //     `lastPushedNormalized` — the only dedupe check, so a held lane pushes once and a ramping
    //     one pushes roughly once per block. `uiFeed` is null in builds/tests that don't care, in
    //     which case this is skipped entirely.
    //
    // No allocation, no locks, no logging, no juce::String. `noexcept`.
    void applyBlock(const AutomationBindingTable& table, const BlockTimeInfo& info,
                    const AutomationRecordState* recordState = nullptr, AutomationUiFeed* uiFeed = nullptr) noexcept;
};

} // namespace synth
