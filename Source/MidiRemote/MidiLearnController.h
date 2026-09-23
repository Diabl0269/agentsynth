#pragma once

#include "MidiRemote/ControllerProfileStore.h"
#include "MidiRemote/MidiRemoteLearnBinder.h"
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

namespace synth::ui {
class MixerPanelComponent;  // Forward declaration -- Source/UI/Mixer/MixerPanelComponent/MixerPanelComponent.h
class TimelineTransportBar; // Forward declaration -- Source/UI/Timeline/TimelineTransportBar.h
} // namespace synth::ui

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

    /** FRO133 (docs/control/midi-remote.md#action-targets): arms a learn on a ShortcutManager
     *  action id (e.g. "transportTogglePlayStop") rather than a graph parameter -- always
     *  buttonLike. Unlike arm() above, an action assignment is GLOBAL: it is written into whatever
     *  ControllerProfile the learned device belongs to, not the project doc
     *  (docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project). Wired to
     *  TimelineTransportBar::onMidiLearnRequested. */
    void armAction(const juce::String& actionId);

    /** "Forget MIDI" for an action target. Removes the assignment from every profile that carries
     *  it (in practice at most one) and persists the change. A no-op if none carries it. Wired to
     *  TimelineTransportBar::onMidiForgetRequested. */
    void forgetAction(const juce::String& actionId);

    /** Every action id currently mapped, to its display label ("Knob 1 on Launchkey Mini"),
     *  scanning every profile's global action assignments. Wired to
     *  TimelineTransportBar::onQueryMidiMappingForAction. */
    std::map<juce::String, juce::String> queryActionMappings() const;

    /** Republishes the project doc's assignments to the engine. Called after every mutation here,
     *  as the undo/redo postRestore, and by MainComponent after a project load/autosave-restore
     *  replaces midiRemoteDoc wholesale. */
    void publishAssignments();

    bool isArmed() const noexcept;
    /** Esc key / clicking the canvas elsewhere while armed. A no-op if nothing is armed. */
    void cancelArmed();

    /** FRO133: non-owning, may be null (tests, or before MainComponent finishes wiring -- same
     *  null contract as TimelineTransportBar::setTransport). Set once so a mixer-fader/pan/mute
     *  learn shows its OWN breathing outline on the mixer column, not only on the canvas card
     *  GraphEditor already reaches via setMidiLearnArmed()/clearMidiLearnArmed(). */
    void setMixerPanel(synth::ui::MixerPanelComponent* panel) noexcept { mixerPanel_ = panel; }

    /** Same null contract as setMixerPanel(), for the transport bar's action-target armed outline
     *  and badges. */
    void setTransportBar(synth::ui::TimelineTransportBar* bar) noexcept { transportBar_ = bar; }

    // ---- FRO131: MIDI Remote panel profile mutations ----
    // Every panel-side edit to a ControllerProfile routes through ONE of these rather than the
    // panel writing ControllerProfileStore directly, so the engine's published snapshot can never
    // go stale relative to what's on disk (docs/control/midi-remote.md's "assignment exists ->
    // consumed" rule means a deleted control that never reaches setProfiles() would keep consuming
    // messages). All are profile edits, so -- like arm()/armAction()'s own profile writes -- NONE
    // of these are undoable (docs/control/midi-remote.md#undo: "Profile edits ... not undoable").

    /** Rename, or a drag-to-move layout change: saves `profile` verbatim (it must already carry
     *  the caller's edit) and republishes. Returns false if `profile.id` doesn't match a known
     *  profile. */
    bool updateProfile(const ControllerProfile& profile);

    /** How many project ("midiRemote") assignments reference `profileId` -- for the Delete
     *  controller confirm dialog's "will orphan N assignments" count. */
    int countProjectAssignmentsForProfile(const juce::String& profileId) const;

    /** Right-click Delete on a controller row: deletes the profile file and drops it from the live
     *  set. Project assignments referencing it are left untouched -- they become exactly the
     *  "orphan controller" state a profile missing from this machine already produces
     *  (docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project). Returns
     *  false if `profileId` isn't known. */
    bool deleteProfile(const juce::String& profileId);

    /** Removes one control from a profile: drops it from `profile.controls`, drops any global
     *  action assignment on it from `profile.actions` (not undoable, same as the profile edit
     *  itself), and removes any PROJECT assignment referencing it (undoable, mirroring forget()'s
     *  own before/after-JSON snapshot). Returns false if the control isn't found. */
    bool deleteControl(const juce::String& profileId, const juce::String& controlId);

    /** Plain file-copy passthrough to the underlying store, for the Controllers list's right-click
     *  "Export...". */
    bool exportProfile(const juce::String& profileId, const juce::File& destFile) const {
        return profileStore_.exportProfile(profileId, destFile);
    }

    /** Inspector edit of an existing PROJECT (parameter-target) assignment's takeover/range/invert
     *  -- `updated` must carry the same `id` as an existing entry in the project doc; every other
     *  field is replaced verbatim. Undoable (docs/control/midi-remote.md#undo: "Project
     *  assignments ... edit ... undoable"), same before/after-JSON shape as forget(). Returns false
     *  if no project assignment has `updated.id`, or if `updated.target` isn't a parameter target
     *  (a global action assignment is a profile edit, not this method's job). */
    bool updateAssignment(const Assignment& updated);

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
    void handleLearnedAction(const LearnResult& result, LearnBindOutcome& outcome);
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

    synth::ui::MixerPanelComponent* mixerPanel_ = nullptr;
    synth::ui::TimelineTransportBar* transportBar_ = nullptr;

    UiWatcher watcher_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnController)
};

} // namespace synth::midi
