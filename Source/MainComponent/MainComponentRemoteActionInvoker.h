#pragma once

// FRO127/FRO253: synth::midi::RemoteActionInvoker impl, extracted out of MainComponent.h (which
// sits at the 1,000-line cap -- root CLAUDE.md's Code structure rule) rather than grown in place.
// See MainComponentSetup.cpp's wireMidiRemoteEngine() for how MainComponent wires it, including
// onNodeCommandApplied, which is set there (mixerDock doesn't exist yet at this object's own
// construction time).

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteModel.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

class AudioEngine;
class AppUndoManager;

class MainComponentRemoteActionInvoker final : public synth::midi::RemoteActionInvoker {
public:
    MainComponentRemoteActionInvoker(juce::ApplicationCommandManager& cm, AudioEngine& engine,
                                     AppUndoManager& undo) noexcept;

    void invokeRemoteCommand(juce::CommandID commandId) override;
    void invokeNodeCommand(juce::AudioProcessorGraph::NodeID nodeId, synth::NodeCommandKind command) override;

    /** Set once by wireMidiRemoteEngine(); may be null before that runs, never during real use.
     *  MESSAGE THREAD, same as invokeNodeCommand itself. */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onNodeCommandApplied;

private:
    juce::ApplicationCommandManager& commandManager_;
    AudioEngine& audioEngine_;
    AppUndoManager& undoManager_;
};
