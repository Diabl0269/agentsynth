#pragma once

#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceComponent.h"
#include "UI/MidiRemote/ControllersList/ControllersListComponent.h"
#include "UI/MidiRemote/Inspector/ControlInspectorComponent.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

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
// MixerPanelComponent) -- MixerDockComponent holds this by value and is itself constructed before
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
     *  (MixerDockComponent's tab-switch-in, MainComponent's post-graph-change reconcile) can call
     *  this directly; scheduleLiveRefresh() below is for a call site that might not be. */
    void rebuildFromProfiles();

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

    /** FRO262 test seam: a mapped control's widget value as last built by
     *  refreshSurfaceForSelectedProfile(), without exposing controllerSurface_ itself. -1.0f if
     *  `controlId` isn't on the currently shown surface (see ControllerSurfaceComponent's own
     *  getCellValueForTest()). */
    float getSurfaceCellValueForTest(const juce::String& controlId) const {
        return controllerSurface_.getCellValueForTest(controlId);
    }

    /** MixerDockComponent::refreshMidiRemoteActivity() -- drains RemoteEngine::drainActivity() ONCE
     *  and fans the decoded events out to the Controllers list's activity dots and, for whichever
     *  control they match on the CURRENTLY SHOWN profile, the surface's live widget values. A
     *  no-op before configure(). Gated by the caller exactly like MixerDockComponent::refreshMeters
     *  -- only while this tab is showing (docs/control/midi-remote-ui.md#surface-centre's "<=30 Hz,
     *  no free-running timer"). */
    void refreshActivity();

    /** GraphEditor::onEditMidiAssignmentRequested's target, via MixerDockComponent -- resolves the
     *  project assignment for (nodeUuid, paramId), selects its controller/control and switches the
     *  surface/inspector to show it. Returns false (and leaves selection untouched) if no such
     *  assignment exists yet -- the FRO130 decision doc note: "omitted until the panel exists"
     *  no longer applies once this ships, but a control with no assignment has nothing to select
     *  either. Does NOT open the dock or switch dock tabs -- same contract as
     *  MixerDockComponent::revealColumnForStrip, the caller does that first. */
    bool selectAssignmentForParameter(const juce::String& nodeUuid, const juce::String& paramId);

    /** Fired when the inspector's "Drives" row is clicked (docs/control/midi-remote-ui.md#inspector-right):
     *  jump to the module on the canvas via the existing locate path. MainComponent wires this to
     *  selectNodeInGraph(nodeUuid) the same way TrackChannelLinkController's reveal hook does. */
    std::function<void(const juce::String& nodeUuid)> onLocateNode;

    void resized() override;

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

    const synth::ControllerProfile* findSelectedProfile() const;

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

    ControllersListComponent controllersList_;
    ControllerSurfaceComponent controllerSurface_;
    ControlInspectorComponent inspector_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRemotePanelComponent)
};

} // namespace synth::ui
