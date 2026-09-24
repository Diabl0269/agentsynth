#pragma once

// FRO127/FRO253/FRO236: synth::midi::RemoteActionInvoker impl, extracted out of MainComponent.h
// (which sits at the 1,000-line cap -- root CLAUDE.md's Code structure rule) rather than grown in
// place. See MainComponentSetup.cpp's wireMidiRemoteEngine() for how MainComponent wires it,
// including onNodeCommandApplied, which is set there (mixerDock doesn't exist yet at this object's
// own construction time).

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteModel.h"
#include "Transport/TransportNudge.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

class AudioEngine;
class AppUndoManager;

namespace synth {
class TimelineDoc;
}

class MainComponentRemoteActionInvoker final : public synth::midi::RemoteActionInvoker {
public:
    /** `transportNudge` and `timelineDoc` must outlive this object -- MainComponent declares both
     *  earlier in its member list than the invoker itself (member init order), same contract as
     *  `engine`/`undo`. */
    MainComponentRemoteActionInvoker(juce::ApplicationCommandManager& cm, AudioEngine& engine, AppUndoManager& undo,
                                     synth::TransportNudgeState& transportNudge,
                                     const synth::TimelineDoc& timelineDoc) noexcept;

    void invokeRemoteCommand(juce::CommandID commandId) override;
    void invokeNodeCommand(juce::AudioProcessorGraph::NodeID nodeId, synth::NodeCommandKind command) override;

    // ---- FRO236 (docs/control/midi-remote.md#continuous-targets) ----
    double getContinuousValue(synth::ContinuousTargetKind kind) override;
    void setContinuousValue(synth::ContinuousTargetKind kind, double native) override;
    bool getContinuousWindow(synth::ContinuousTargetKind kind, double& lo, double& hi) override;

    /** Set once by wireMidiRemoteEngine(); may be null before that runs, never during real use.
     *  MESSAGE THREAD, same as invokeNodeCommand itself. */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onNodeCommandApplied;

private:
    juce::ApplicationCommandManager& commandManager_;
    AudioEngine& audioEngine_;
    AppUndoManager& undoManager_;
    synth::TransportNudgeState& transportNudge_;
    const synth::TimelineDoc& timelineDoc_;
};
