#pragma once

#include "MidiRemote/ControllerProfileStore.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteModel.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>

// MidiLearnController.h -- FRO130 (docs/control/midi-remote-ui.md#the-learn-interaction): the
// app-side half of a module-card right-click MIDI Learn. GraphEditor/ModuleComponent own the menu
// and the badge/pulse painting but no RemoteEngine or MidiRemoteProjectDoc; this is what ACTS on a
// learn request, mirroring the TrackChannelLinkController seam (a collaborator owned by
// MainComponent rather than more methods on MainComponent itself).
//
// Owns the loaded controller-profile set (loaded once at construction, the same data
// MainComponent::wireMidiRemoteEngine() used to keep as a local temp) and RemoteEngine::onLearned,
// so a learn's whole lifecycle -- arm, cancel (Esc / any click / 10s silent timeout), auto-profile
// creation, undoable project-doc mutation -- lives in one place.

class AudioEngine;
class AppUndoManager;
class GraphEditor;
class StatusBarComponent;

namespace synth::midi {

class MidiLearnController final {
public:
    /** `profileStore` defaults to the real, resolved settings-folder store; tests inject one
     *  pointed at a temp directory (ControllerProfileStore's own test constructor) so a test run
     *  never touches the user's real MIDI Remote controller files. */
    MidiLearnController(AudioEngine& engine, GraphEditor& graphEditor, RemoteEngine& remoteEngine,
                        synth::MidiRemoteProjectDoc& doc, AppUndoManager& undo, StatusBarComponent& statusBar,
                        ControllerProfileStore profileStore = ControllerProfileStore());
    ~MidiLearnController();

    const std::vector<ControllerProfile>& getProfiles() const { return profiles_; }

    /** "MIDI Learn '<Param>'..." / "MIDI Learn again...". Wired to GraphEditor::onMidiLearnRequested. */
    void arm(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);

    /** "Forget MIDI". Wired to GraphEditor::onMidiForgetRequested. A no-op if there is no
     *  assignment for this target. */
    void forget(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);

    /** Wired to GraphEditor::onQueryMidiMappingsForNode. */
    std::map<juce::String, juce::String> queryMappings(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** Republishes the project doc's assignments to the engine. Called after every mutation here,
     *  as the undo/redo postRestore, and by MainComponent after a project load/autosave-restore
     *  replaces midiRemoteDoc wholesale. */
    void publishAssignments();

    bool isArmed() const noexcept;
    /** Esc key / clicking the canvas elsewhere while armed. A no-op if nothing is armed. */
    void cancelArmed();

private:
    /** Forwards MouseListener clicks and polls RemoteEngine::isLearnArmed() for the silent-timeout
     *  cancel path, which fires no callback of its own
     * (docs/control/midi-remote.md#learn-what-does-the-first-message-mean's "cancel silently"). Message-thread only,
     * well away from the MIDI path's no-timer-allocation rule. */
    class UiWatcher final
        : public juce::MouseListener
        , private juce::Timer {
    public:
        std::function<void()> onAnyClick;
        std::function<bool()> stillArmed;
        std::function<void()> onExternallyCancelled;
        void start() { startTimer(200); }
        void stop() { stopTimer(); }

    private:
        void mouseDown(const juce::MouseEvent&) override {
            if (onAnyClick)
                onAnyClick();
        }
        void timerCallback() override {
            if (stillArmed && !stillArmed() && onExternallyCancelled)
                onExternallyCancelled();
        }
    };

    void refreshSources();
    void handleLearned(const LearnResult& result);
    void endArmedUi();
    juce::String resolveNodeUuid(juce::AudioProcessorGraph::NodeID nodeId) const;
    juce::String ensureNodeUuid(juce::AudioProcessorGraph::NodeID nodeId) const;
    juce::String deviceNameForSourceKey(const juce::String& sourceKey) const;
    const ControllerProfile* findProfile(const juce::String& id) const;

    AudioEngine& engine_;
    GraphEditor& graphEditor_;
    RemoteEngine& remoteEngine_;
    synth::MidiRemoteProjectDoc& doc_;
    AppUndoManager& undo_;
    StatusBarComponent& statusBar_;

    ControllerProfileStore profileStore_;
    std::vector<ControllerProfile> profiles_;

    UiWatcher watcher_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnController)
};

} // namespace synth::midi
