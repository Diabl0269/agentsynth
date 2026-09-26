#pragma once

#include "MidiRemote/ControllerProfileStore.h"
#include "MidiRemote/MidiRemoteLearnBinder.h"
#include "MidiRemote/PickTarget.h"
#include "MidiRemote/ProfileEditHistory.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteModel.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
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
class PickTargetOverlay;    // Forward declaration -- Source/UI/Graph/PickTargetOverlay/PickTargetOverlay.h
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
    /** Test/inspection (FRO193): proves which directory this instance's ControllerProfileStore
     *  actually resolved to, without exposing the store itself. */
    const juce::File& getControllersDirectoryForTest() const { return profileStore_.getControllersDirectory(); }

    /** "MIDI Learn '<Param>'..." / "MIDI Learn again...". Wired to GraphEditor::onMidiLearnRequested. */
    void arm(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);

    /** "Forget MIDI". Wired to GraphEditor::onMidiForgetRequested. A no-op if there is no
     *  assignment for this target. */
    void forget(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);

    /** Wired to GraphEditor::onQueryMidiMappingsForNode. */
    std::map<juce::String, juce::String> queryMappings(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** FRO253 (docs/control/midi-remote.md#node-command-targets): arms a learn on a node command
     *  (e.g. Solo, ChannelStripModule::soloed_) rather than a graph parameter -- modelled on arm()
     *  above, not armAction(): a node command is PROJECT-scoped, same as a parameter (it lives on
     *  MidiRemoteProjectDoc::assignments, not a ControllerProfile), because it names a graph node
     *  that only makes sense within this project, unlike a fixed ShortcutManager action id.
     *  Wired to MixerPanelComponent::onSoloMidiLearnRequested. */
    void armNodeCommand(juce::AudioProcessorGraph::NodeID nodeId, NodeCommandKind command);

    /** "Forget MIDI" for a node command target. A no-op if there is no assignment for it. Wired to
     *  MixerPanelComponent::onSoloMidiForgetRequested. */
    void forgetNodeCommand(juce::AudioProcessorGraph::NodeID nodeId, NodeCommandKind command);

    /** Every node command mapped for `nodeId`, to its display label -- mirrors queryMappings()
     *  above, keyed by NodeCommandKind instead of a paramId string. Wired to
     *  MixerPanelComponent::onQuerySoloMidiMapping. */
    std::map<NodeCommandKind, juce::String> queryNodeCommandMappings(juce::AudioProcessorGraph::NodeID nodeId) const;

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

    /** Republishes the project doc's assignments to the engine and re-resolves them against the
     *  live graph. Called after every mutation here, as the undo/redo postRestore, and by
     *  MainComponent after a project load/autosave-restore replaces midiRemoteDoc wholesale.
     *
     *  FRO253: RemoteEngine::setAssignments() rebuilds its snapshot with graph == nullptr by
     *  design (docs/architecture/app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step)
     *  -- it can only keep each assignment id's PREVIOUS resolution, so a brand-new id (a
     *  just-settled learn, or its undo/redo) has none and its slot stays unresolved until some
     *  unrelated graph change happens to reach MainComponent's reconcile funnel. This method
     *  reconciles against the live graph itself right after setAssignments() so a fresh learn's
     *  target works immediately, without changing RemoteEngine's setter semantics. */
    void publishAssignments();

    // ---- FRO135: mapping assistant (MidiLearnControllerMapping.cpp, MidiLearnControllerPick.cpp) ----

    /** Project scope for a parameter/node command (undoable), global for an action. Nothing changes unless `assigned`.
     */
    AssignStatus assignControl(const juce::String& profileId, const juce::String& controlId, const PickTarget& target);
    /** By assignment id (project or global), so an orphaned target can still be forgotten. */
    bool forgetAssignment(const juce::String& assignmentId);
    /** Orphan Re-link: one undo step. `ok` is false for an unknown target or a non-orphan source. */
    RelinkOutcome relinkController(const juce::String& orphanProfileId, const juce::String& targetProfileId);
    /** Orphan Recreate on `device`; returns the new profile id, or empty if `orphanProfileId` is not an orphan. */
    juce::String recreateController(const juce::String& orphanProfileId, const ControllerProfile::Input& device);

    /** The layer the pick-target overlay covers (MainComponent). Null until wired. */
    void setPickOverlayHost(juce::Component* host) noexcept { pickOverlayHost_ = host; }
    /** False with no host or an unknown control. Message thread only. */
    bool beginPickTarget(const juce::String& profileId, const juce::String& controlId);
    bool isPickingTarget() const noexcept;
    synth::ui::PickTargetOverlay* getPickOverlayForTest() noexcept { return pickOverlay_.get(); }
    void cancelPickTarget();
    /** Re-collects the pick candidates from every surface (a dock tab switch changed what is showing). */
    void refreshPickTarget();
    /** Components (the dock's tab buttons) whose clicks reach them instead of ending the pick. */
    void setPickPassThrough(std::vector<juce::Component*> components) { pickPassThrough_ = std::move(components); }

    bool isArmed() const noexcept;
    /** Esc key / clicking the canvas elsewhere while armed. A no-op if nothing is armed. */
    void cancelArmed();

    /** FRO263: fires after every mutation that changes what the MIDI Remote panel shows. May be
     *  null (tests, or before MainComponent finishes wiring). See publishAssignments()'s .cpp
     *  comment for exactly which call sites fire it and why the panel-side handler must defer. */
    std::function<void()> onChanged;

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
    // go stale relative to what's on disk. Each records one step on the controller edit history
    // (below), never on AppUndoManager (docs/control/midi-remote.md#undo).

    /** Rename, or a drag-to-move layout change: saves `profile` verbatim (it must already carry
     *  the caller's edit) and republishes. `editLabel` names the history step ("Move control").
     *  Returns false if `profile.id` doesn't match a known profile. */
    bool updateProfile(const ControllerProfile& profile, const juce::String& editLabel = "Edit controller");

    /** FRO134: adds a brand-new profile (Add controller, Detect-from-scratch). Saved, published to
     *  the engine and announced via onChanged. Returns false if `profile.id` is empty or already known. */
    bool addProfile(const ControllerProfile& profile, const juce::String& editLabel = "Add controller");

    // ---- FRO273: controller edit history (MidiLearnControllerHistory.cpp). Message thread only. ----
    bool canUndoProfileEdit() const noexcept { return profileHistory_.canUndo(); }
    bool canRedoProfileEdit() const noexcept { return profileHistory_.canRedo(); }
    /** Empty when there is nothing to undo / redo. */
    juce::String getUndoProfileEditLabel() const { return profileHistory_.getUndoLabel(); }
    juce::String getRedoProfileEditLabel() const { return profileHistory_.getRedoLabel(); }
    /** False (nothing changed) when there is nothing to undo / redo. */
    bool undoProfileEdit();
    bool redoProfileEdit();
    const ProfileEditHistory& getProfileEditHistory() const noexcept { return profileHistory_; }

    enum class ImportStatus { imported, replaced, conflict, invalid };
    struct ImportResult {
        ImportStatus status = ImportStatus::invalid;
        ControllerProfile profile; // set for imported / replaced / conflict (the file's profile)
    };
    /** FRO134: "Import controller..." -- reads the profile document in `srcFile`. `invalid` if it
     *  doesn't parse; `conflict` (nothing changed) if a profile with the same id is already known
     *  and `replaceExisting` is false, so the caller can prompt and call again with true. */
    ImportResult importProfile(const juce::File& srcFile, bool replaceExisting = false);

    /** FRO134/FRO264: the Inspector's edit of an existing control's name, kind or encoding. Replaces
     *  the control (matched by id; its message key and layout are kept as they are on the stored
     *  one) and re-copies the denormalised name/encoding/button-mode onto every assignment that
     *  references it, since the engine reads those from the assignment. Returns false if the
     *  profile or control is unknown. */
    bool updateControl(const juce::String& profileId, const Control& edited);

    /** How many project ("midiRemote") assignments reference `profileId` -- for the Delete
     *  controller confirm dialog's "will orphan N assignments" count. */
    int countProjectAssignmentsForProfile(const juce::String& profileId) const;

    /** Deletes the profile file and drops it from the live set; leaves any project assignment
     *  referencing it as an orphan. Returns false if `profileId` isn't known. See the .cpp for why. */
    bool deleteProfile(const juce::String& profileId);

    /** Removes one control from a profile, and any assignment (global or project) referencing it.
     *  Returns false if the control isn't found. See the .cpp for undo-scope details. */
    bool deleteControl(const juce::String& profileId, const juce::String& controlId);

    /** Plain file-copy passthrough to the underlying store, for the Controllers list's right-click
     *  "Export...". */
    bool exportProfile(const juce::String& profileId, const juce::File& destFile) const {
        return profileStore_.exportProfile(profileId, destFile);
    }

    /** Inspector edit of an existing PROJECT parameter-target assignment's takeover/range/invert.
     *  Returns false if `updated.id` isn't a known project assignment or isn't a parameter target
     *  -- see the .cpp for why action/nodeCommand targets are rejected here. */
    bool updateAssignment(const Assignment& updated);

    // MESSAGE THREAD (FRO262). Republishes the engine's open MIDI inputs to RemoteEngine::setSources().
    void refreshSources();

    /** FRO240 (docs/control/midi-remote.md#replace-and-duplicate, MidiLearnControllerRetarget.cpp):
     *  wired to GraphEditor::onModuleReplaced -- re-targets every project assignment (parameter or
     *  node command) whose target nodeUuid is `oldNodeUuid` onto `newNodeId`'s uuid (assigned
     *  lazily if it has none yet), but ONLY where the new module still supports the target: a
     *  parameter assignment only if `newNodeId`'s processor still resolves that paramId
     *  (synth::resolveLaneParameter); a node command only if the new module supports that command
     *  (toggleSolo: both nodes must be a ChannelStripModule). Anything that doesn't move is left
     *  exactly where it was -- orphaned, same as today's plain delete/replace. Mutates `doc_`
     *  in-place with NO undo recording of its own: the caller (GraphEditor::replaceModule, via
     *  onModuleReplaced) must already be inside a single AppUndoManager::recordGraphAndMidiRemoteChange
     *  transaction that brackets doc_'s own before/after JSON around this call, so the retarget
     *  lands in the SAME undo step as the module replace. Returns true if anything moved. */
    bool retargetNode(const juce::String& oldNodeUuid, juce::AudioProcessorGraph::NodeID newNodeId);

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

    void handleLearned(const LearnResult& result);
    void handleLearnedAction(const LearnResult& result, LearnBindOutcome& outcome);
    void endArmedUi();
    juce::String resolveNodeUuid(juce::AudioProcessorGraph::NodeID nodeId) const;
    juce::String ensureNodeUuid(juce::AudioProcessorGraph::NodeID nodeId) const;
    juce::String deviceNameForSourceKey(const juce::String& sourceKey) const;
    const ControllerProfile* findProfile(const juce::String& id) const;
    std::optional<ControllerProfile> profileSnapshot(const juce::String& id) const;
    void recordProfileEdit(const juce::String& label, const juce::String& profileId,
                           std::optional<ControllerProfile> before);
    bool applyProfileState(const juce::String& profileId, const std::optional<ControllerProfile>& state);
    void resyncAssignmentCopies(const ControllerProfile& profile);

    AudioEngine& engine_;
    GraphEditor& graphEditor_;
    RemoteEngine& remoteEngine_;
    synth::MidiRemoteProjectDoc& doc_;
    AppUndoManager& undo_;
    StatusBarComponent& statusBar_;

    ControllerProfileStore profileStore_;
    std::vector<ControllerProfile> profiles_;
    ProfileEditHistory profileHistory_;
    bool applyingProfileHistory_ = false; // recording is suppressed while an undo/redo applies

    synth::ui::MixerPanelComponent* mixerPanel_ = nullptr;
    synth::ui::TimelineTransportBar* transportBar_ = nullptr;

    UiWatcher watcher_;

    juce::Component::SafePointer<juce::Component> pickOverlayHost_;
    std::unique_ptr<synth::ui::PickTargetOverlay> pickOverlay_;
    std::vector<juce::Component*> pickPassThrough_;
    juce::String pickProfileId_;
    juce::String pickControlId_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnController)
};

} // namespace synth::midi
