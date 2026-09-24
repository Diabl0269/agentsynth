#pragma once

// What a pick-target click or the action picker chose to drive: a graph parameter, a ShortcutManager
// action, or a node command (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn).
// Every learnable surface reports one of these per registered control; surfaces know nodes by
// NodeID, so the uuid is resolved by MidiLearnController::assignControl, not here.

#include "MidiRemote/RemoteModel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth::midi {

struct PickTarget {
    enum class Kind { parameter, action, nodeCommand, continuous };

    Kind kind = Kind::parameter;
    juce::AudioProcessorGraph::NodeID nodeId; // parameter / nodeCommand
    juce::String paramId;                     // parameter
    juce::String actionId;                    // action
    NodeCommandKind command = NodeCommandKind::toggleSolo;
    ContinuousTargetKind continuous = ContinuousTargetKind::bpm; // continuous

    static PickTarget parameter(juce::AudioProcessorGraph::NodeID node, const juce::String& id) {
        PickTarget t;
        t.kind = Kind::parameter;
        t.nodeId = node;
        t.paramId = id;
        return t;
    }
    static PickTarget action(const juce::String& id) {
        PickTarget t;
        t.kind = Kind::action;
        t.actionId = id;
        return t;
    }
    static PickTarget nodeCommandTarget(juce::AudioProcessorGraph::NodeID node, NodeCommandKind kind) {
        PickTarget t;
        t.kind = Kind::nodeCommand;
        t.nodeId = node;
        t.command = kind;
        return t;
    }
    // FRO236 (docs/control/midi-remote.md#continuous-targets): no node -- a continuous target never
    // names a graph node (ContinuousTargetKind's own comment on how each kind resolves).
    static PickTarget continuousTarget(ContinuousTargetKind kind) {
        PickTarget t;
        t.kind = Kind::continuous;
        t.continuous = kind;
        return t;
    }
};

enum class AssignStatus { assigned, unknownControl, unresolvedTarget, invalidAction };

struct RelinkOutcome {
    bool ok = false; // false: the target profile is unknown, or the source profile is not an orphan
    int matched = 0;
    int unmatched = 0;
};

} // namespace synth::midi
