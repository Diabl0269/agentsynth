#pragma once

#include "MidiRemote/EncoderAutoDetect.h"
#include "MidiRemote/PickTarget.h"
#include "UI/MidiRemote/AddController/AddControllerPopover.h"
#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceComponent.h"
#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceToolbar.h"
#include "UI/MidiRemote/ControllersList/ControllersListComponent.h"
#include "UI/MidiRemote/Detect/DetectModeController.h"
#include "UI/MidiRemote/Inspector/ControlInspectorComponent.h"
#include "UI/MidiRemote/Orphan/OrphanControllerComponent.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <vector>

class AudioEngine;
class GraphEditor;

namespace synth {
class MidiRemoteProjectDoc;
} // namespace synth

namespace synth::midi {
class RemoteEngine;
class MidiLearnController;
} // namespace synth::midi

// MidiRemotePanelComponent.h -- FRO131 (docs/control/midi-remote-ui.md#the-midi-remote-panel): the
// third bottom-dock tab's content -- Controllers list (left) / Surface (centre) / Inspector
// (right), plus the ONE `{selectedProfileId, selectedControlId}` selection state all three read
// from and write into. Default-constructible with no live dependency (same shape as
// MixerPanelComponent) -- BottomDockComponent holds this by value and is itself constructed before
// RemoteEngine/MidiLearnController exist as MainComponent members (declaration-order constraint,
// see MidiLearnController.h's own setMixerPanel()/setTransportBar() null-contract precedent), so
// every live dependency arrives late via configure(), called once from
// MainComponent::wireMidiRemoteEngine().
namespace synth::ui {

class MidiRemotePanelComponent : public juce::Component {
public:
    MidiRemotePanelComponent();
    ~MidiRemotePanelComponent() override;

    /** Wires the live engine/profile/project/canvas dependencies and does the first rebuild.
     *  Non-owning references, held for the component's lifetime (MainComponent outlives it). */
    void configure(AudioEngine& audioEngine, synth::midi::RemoteEngine& remoteEngine,
                   synth::midi::MidiLearnController& learnController, synth::MidiRemoteProjectDoc& doc,
                   GraphEditor& graphEditor);

    /** Re-reads the profile set (MidiLearnController::getProfiles()) and the project doc's
     *  controller refs, and rebuilds the Controllers list. Call after any mutation this panel
     *  itself makes (rename/delete profile/delete control) and after configure(). Cheap: a handful
     *  of rows, never on the MIDI path. Synchronous -- callers already off a live mouse gesture
     *  (BottomDockComponent's tab-switch-in, MainComponent's post-graph-change reconcile) can call
     *  this directly; scheduleLiveRefresh() below is for a call site that might not be. */
    void rebuildFromProfiles();

    /** FRO136: the Preferences default takeover, so the inspector's "Default" item can name it. */
    void setDefaultTakeover(synth::Takeover takeover) { inspector_.setDefaultTakeover(takeover); }

    /** FRO263 (docs/control/midi-remote-ui.md#the-midi-remote-panel): MidiLearnController::onChanged's
     *  target (wired once in MainComponent::wireMidiRemoteEngine()) -- keeps the panel live while
     *  it's open instead of only catching up on the next tab-switch-in. Defers the actual
     *  rebuildFromProfiles() via MessageManager::callAsync and coalesces repeat calls into one,
     *  because onChanged can fire from inside a cell's own mouseUp call stack (a drag-to-reposition
     *  ending in MidiLearnController::updateProfile()) -- a synchronous rebuild there would free the
     *  very ControllerSurfaceCell whose mouseUp is still executing (same hazard
     *  ControllerSurfaceComponent.cpp's own mid-gesture-rebuild comment documents). A no-op before
     *  configure() (rebuildFromProfiles() itself already guards on that). */
    void scheduleLiveRefresh();

    /** FRO263 test seam: proves scheduleLiveRefresh()'s deferred rebuild actually reaches the
     *  Controllers list, without exposing controllersList_ itself. */
    int getControllersListRowCountForTest() const { return controllersList_.getRowCountForTest(); }
    const ControllersListComponent& getControllersListForTest() const { return controllersList_; }

    /** FRO262 test seam: a mapped control's widget value as last built by
     *  refreshSurfaceForSelectedProfile(), without exposing controllerSurface_ itself. -1.0f if
     *  `controlId` isn't on the currently shown surface (see ControllerSurfaceComponent's own
     *  getCellValueForTest()). */
    float getSurfaceCellValueForTest(const juce::String& controlId) const {
        return controllerSurface_.getCellValueForTest(controlId);
    }

    /** BottomDockComponent::refreshMidiRemoteActivity() -- drains RemoteEngine::drainActivity() ONCE
     *  and fans the decoded events out to the Controllers list's activity dots and, for whichever
     *  control they match on the CURRENTLY SHOWN profile, the surface's live widget values. A
     *  no-op before configure(). Gated by the caller exactly like BottomDockComponent::refreshMeters
     *  -- only while this tab is showing (docs/control/midi-remote-ui.md#surface-centre's "<=30 Hz,
     *  no free-running timer"). */
    void refreshActivity();

    // ---- FRO134: Detect, Add controller, Templates, Import/Export, encoder auto-detect -------------
    // (docs/control/midi-remote-ui.md#detect-mode, #add-controller). Units: MidiRemotePanelDetect.cpp
    // (Detect + auto-detect) and MidiRemotePanelControllers.cpp (Add / Templates / Import).

    /** The toolbar's Detect toggle. While on, refreshActivity() turns unknown messages from the
     *  selected profile's device into cells. Turned off by selecting another controller. */
    void setDetectActive(bool active);
    bool isDetectActive() const noexcept { return detect_.isActive(); }

    /** Applies a shipped template to the selected controller (empty: as-is; non-empty: merged, existing
     *  message keys win). Returns how many controls were added, or -1 with no controller selected or
     *  an unknown template. */
    int applyTemplateToSelectedProfile(const juce::String& templateId);

    /** "+ Add controller" -> the popover, anchored to `anchor`. Hidden in Hosted mode by the list. */
    void showAddControllerPopover(juce::Component& anchor);
    /** The popover's OK: creates the profile, opens the device, selects it and (Detect choice) enters
     *  Detect. Returns the new profile's id, or empty if it could not be created. */
    juce::String createControllerFromChoice(const AddControllerPopover::Choice& choice);

    enum class ImportOutcome { imported, replaced, conflict, invalid };
    /** Import controller... with the file already chosen: prompts (async) if a controller with the
     *  same id exists. */
    void importControllerFile(const juce::File& file);
    /** FRO139 (docs/control/midi-remote.md#controller-feedback): the Controllers-list right-click
     *  "Send feedback to" choice -- `device` unset means "None". A public, directly-callable method
     *  (rather than only reachable through the list's own callback) so it is unit-testable without
     *  driving a real right-click menu. */
    void setFeedbackOutput(const juce::String& profileId, const std::optional<synth::ControllerProfile::Input>& device);
    /** The prompt-free half: `replaceExisting` false reports `conflict` and changes nothing. A
     *  successful import selects the controller. */
    ImportOutcome importControllerFileNow(const juce::File& file, bool replaceExisting);

    /** Inspector "Auto-detect...": the two-step turn-left / turn-right prompt, then the encoding is
     *  set on the control. advanceEncoderAutoDetect() is what each prompt's button calls. */
    void beginEncoderAutoDetect(const synth::Control& control);
    void advanceEncoderAutoDetect();
    const synth::midi::EncoderAutoDetect& getEncoderAutoDetectForTest() const noexcept { return encoderDetect_; }
    /** Whether `controlId`'s surface cell is pulsing or flashing right now. */
    bool isSurfaceCellHighlightedForTest(const juce::String& controlId) const {
        return controllerSurface_.isControlPulsingForTest(controlId);
    }

    /** Every prompt this panel raises (auto-detect steps, import conflict/invalid) goes through this
     *  when set, instead of an AlertWindow. `done(true)` = the OK button. */
    using PromptHook = std::function<void(const juce::String& title, const juce::String& message, bool cancellable,
                                          std::function<void(bool ok)> done)>;
    void setPromptHookForTest(PromptHook hook) { promptHook_ = std::move(hook); }

    // ---- FRO135: assign from the panel, orphan controllers ----
    // (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn, #controllers-list-left).
    // Units: MidiRemotePanelAssign.cpp and MidiRemotePanelOrphans.cpp.

    /** Assign... / Learn target: a two-item menu -- "Pick a module control" or "Choose an action...". */
    void showAssignMenu(juce::Component& anchor);
    /** "Pick a module control": the pick-target overlay for the selected control. False with no control selected. */
    bool startPickTargetForSelectedControl();
    /** "Choose an action...": the searchable picker, anchored to `anchor`. */
    void showActionPicker(juce::Component& anchor);
    /** The picker's choice: assigns the selected control to `actionId`. False if nothing is selected or the action is
     * not invokable. */
    bool assignSelectedControlToAction(const juce::String& actionId);
    /** FRO236 (docs/control/midi-remote.md#continuous-targets): the picker's "Continuous" choice.
     *  False if nothing is selected. */
    bool assignSelectedControlToContinuous(synth::ContinuousTargetKind kind);

    /** An orphan controller row (a project reference with no local profile) is selected. */
    bool isOrphanSelected() const;
    void showRelinkMenu(juce::Component& anchor);
    void showRecreateMenu(juce::Component& anchor);
    /** Re-link: the selected orphan's assignments onto `profileId`'s controls. Selects that profile if nothing stays
     * orphaned. */
    synth::midi::RelinkOutcome relinkSelectedOrphanTo(const juce::String& profileId);
    /** Recreate on `device`; selects the new profile. Returns its id, or empty. */
    juce::String recreateSelectedOrphanOn(const synth::ControllerProfile::Input& device);
    /** MIDI inputs a Recreate could bind to: not already used by a profile (Host MIDI in the plugin build). */
    std::vector<synth::ControllerProfile::Input> getRecreateInputs() const;
    juce::String getOrphanStatusTextForTest() const { return orphanView_.getStatusTextForTest(); }
    bool isOrphanViewShownForTest() const { return orphanView_.isVisible(); }
    OrphanControllerComponent& getOrphanViewForTest() { return orphanView_; }
    ControlInspectorComponent& getInspectorForTest() { return inspector_; }
    const ControllerSurfaceCell* findSurfaceCellForTest(const juce::String& controlId) const {
        return controllerSurface_.findCellForTest(controlId);
    }
    ControllerSurfaceToolbar& getToolbarForTest() { return toolbar_; }
    void selectForTest(const juce::String& profileId, const juce::String& controlId) {
        selectProfile(profileId);
        if (controlId.isNotEmpty())
            selectControl(controlId);
    }

    /** GraphEditor::onEditMidiAssignmentRequested's target, via BottomDockComponent -- resolves the
     *  project assignment for (nodeUuid, paramId), selects its controller/control and switches the
     *  surface/inspector to show it. Returns false (and leaves selection untouched) if no such
     *  assignment exists yet -- the FRO130 decision doc note: "omitted until the panel exists"
     *  no longer applies once this ships, but a control with no assignment has nothing to select
     *  either. Does NOT open the dock or switch dock tabs -- same contract as
     *  BottomDockComponent::revealColumnForStrip, the caller does that first. */
    bool selectAssignmentForParameter(const juce::String& nodeUuid, const juce::String& paramId);

    /** Fired when the inspector's "Drives" row is clicked (docs/control/midi-remote-ui.md#inspector-right):
     *  jump to the module on the canvas via the existing locate path. MainComponent wires this to
     *  selectNodeInGraph(nodeUuid) the same way TrackChannelLinkController's reveal hook does. */
    std::function<void(const juce::String& nodeUuid)> onLocateNode;

    // ---- FRO273: undo routing + cue (MidiRemotePanelUndo.cpp) ----
    /** True while keyboard focus is inside this panel (docked or detached): Cmd+Z then acts on the
     *  controller edit history instead of the project's. */
    bool holdsUndoFocus() const;
    /** Headless stand-in for real focus (no native peer in tests); nullopt = use real focus. */
    void setHoldsUndoFocusForTest(std::optional<bool> focused);
    /** Re-derives the toolbar's "Cmd+Z undoes: ..." hint; repaints only if the text changed. */
    void refreshUndoHint();

    void resized() override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    void focusOfChildComponentChanged(FocusChangeType cause) override;

    static constexpr int kListWidth = 200;
    static constexpr int kInspectorWidth = 240;

private:
    void selectProfile(const juce::String& profileId);
    void selectControl(const juce::String& controlId);
    void refreshSurfaceForSelectedProfile();
    void refreshInspectorForSelection();
    void handleRenameRequested(const juce::String& profileId, const juce::String& newName);
    void handleExportRequested(const juce::String& profileId);
    void handleDeleteProfileRequested(const juce::String& profileId);
    void handleControlMoved(const juce::String& controlId, int col, int row);
    void handleDeleteControlRequested(const juce::String& controlId);
    void handleForgetRequested(const juce::String& assignmentId);
    void handleControlEdited(const synth::Control& control);

    // FRO134
    void showPrompt(const juce::String& title, const juce::String& message, bool cancellable,
                    std::function<void(bool ok)> done);
    void showTemplatesMenu(juce::Component& anchor);
    void showMoreMenu(juce::Component& anchor);
    void showImportChooser();
    void promptEncoderStep();
    void finishEncoderAutoDetect();
    /** After refreshActivity()'s drain: persist Detect's additions once and drive the pulse/flash. */
    void commitDetectStep(const std::optional<synth::ControllerProfile>& working, bool profileChanged,
                          const std::vector<juce::String>& litControlIds,
                          const std::vector<synth::midi::RemoteEvent>& events);

    const synth::ControllerProfile* findSelectedProfile() const;
    bool isOrphanId(const juce::String& profileId) const;
    bool isProfilePresent(const synth::ControllerProfile& profile) const;

    // FRO136 (docs/control/midi-remote-ui.md#plugin-build): the plugin build's one live controller.
    // Its row is always listed, under this id, even before a profile exists; selecting the row
    // creates the profile under the same id (a Learn that got there first has a profile with the
    // host source key under its own id, and then no extra row is added).
    static constexpr const char* kHostMidiProfileId = "host-midi";
    bool hostMidiProfileExists() const;
    void createHostMidiProfile();
    /** A profile is selected and can hear something: always in the standalone app, only Host MIDI in a host. */
    bool isSelectedProfileUsable() const;

    AudioEngine* audioEngine_ = nullptr;
    synth::midi::RemoteEngine* remoteEngine_ = nullptr;
    synth::midi::MidiLearnController* learnController_ = nullptr;
    synth::MidiRemoteProjectDoc* doc_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;

    juce::String selectedProfileId_;
    juce::String selectedControlId_;
    // Rising/falling-edge tracking so refreshActivity() repaints an activity dot only on change
    // (Source/UI/CLAUDE.md's "no unconditional per-tick repaint" rule), not on every 10 Hz tick.
    std::map<juce::String, bool> profileActivityLit_;
    std::map<juce::String, juce::int64> profileLastActivityMs_;
    static constexpr int kActivityLitMs = 150;

    // FRO263: scheduleLiveRefresh()'s coalescing latch -- true from the first call until the
    // deferred rebuildFromProfiles() actually runs, so several onChanged notifications in a row
    // (e.g. a Learn's profile write followed by its project-assignment write) collapse into one
    // rebuild rather than one per notification.
    bool liveRefreshPending_ = false;

    /** Gives the panel keyboard focus on a press anywhere inside it (MidiRemotePanelUndo.cpp). */
    class FocusOnClick final : public juce::MouseListener {
    public:
        explicit FocusOnClick(MidiRemotePanelComponent& owner)
            : owner_(owner) {}
        void mouseDown(const juce::MouseEvent& e) override;

    private:
        MidiRemotePanelComponent& owner_;
    };
    FocusOnClick focusOnClick_{*this};
    std::optional<bool> holdsUndoFocusForTest_;

    DetectModeController detect_;
    synth::midi::EncoderAutoDetect encoderDetect_;
    juce::String encoderTargetControlId_;
    PromptHook promptHook_;

    ControllersListComponent controllersList_;
    ControllerSurfaceToolbar toolbar_;
    ControllerSurfaceComponent controllerSurface_;
    ControlInspectorComponent inspector_;
    OrphanControllerComponent orphanView_;
    juce::String orphanStatus_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRemotePanelComponent)
};

} // namespace synth::ui
