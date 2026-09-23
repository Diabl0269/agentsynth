#pragma once

#include "MidiRemote/RemoteEngine/RemoteEvent.h"
#include "MidiRemote/RemoteModel.h"
#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceCell.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// ControllerSurfaceComponent.h -- FRO131 (docs/control/midi-remote-ui.md#surface-centre): the MIDI
// Remote panel's centre region -- the selected controller's controls drawn on a 56 px grid
// (Control::layout {col,row}, snapped). Out of scope for FRO131 (task 7/8): the
// [Detect]/[Assign...]/[Templates]/[...] toolbar row shown in the design doc's wireframe -- this
// component owns only the grid itself.
namespace synth::ui {

class ControllerSurfaceComponent : public juce::Component {
public:
    struct CellModel {
        synth::Control control;
        juce::String assignmentLabel; // "Filter . Cutoff" / "Play" / "-" / "(missing module)"
        bool isWarning = false;       // orphaned target -> warning-colour label
        bool isMapped = false;        // paints the MIDI badge
    };

    ControllerSurfaceComponent();
    ~ControllerSurfaceComponent() override;

    /** Rebuilds the grid for a newly-selected profile (or clears it for an empty `profileId` --
     *  no controller selected). Structural only: call on profile switch, or when the shown
     *  profile's own control set/assignments change -- never on the activity tick. */
    void setControls(const juce::String& profileId, const std::vector<CellModel>& cells);

    const juce::String& getProfileId() const noexcept { return profileId_; }

    /** MidiRemotePanelComponent::refreshActivity()'s per-control fan-out. A no-op if `controlId`
     *  isn't on the currently shown grid -- the caller doesn't filter first. */
    void noteActivity(const juce::String& controlId, synth::midi::RemoteEventKind kind, float value);

    void setSelectedControlId(const juce::String& controlId);
    juce::String getSelectedControlId() const noexcept { return selectedControlId_; }

    std::function<void(const juce::String& controlId)> onSelectControl;
    /** A drag landed on a new grid cell, already clamped to col >= 0 / row >= 0. The caller writes
     *  this back to the profile (MidiLearnController::updateProfile()) and is responsible for
     *  calling setControls() again afterward to reflect the committed position. */
    std::function<void(const juce::String& controlId, int newCol, int newRow)> onControlMoved;
    /** Delete/Backspace while a cell is selected -- this component itself grabs keyboard focus on
     *  cell selection (grabKeyboardFocus() in the onSelectControl handling) and owns keyPressed,
     *  rather than each cell separately competing for focus. */
    std::function<void(const juce::String& controlId)> onDeleteControlRequested;

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;

    static constexpr int kCellSize = ControllerSurfaceCell::kCellSize;
    static constexpr int kCellMargin = 6;

private:
    juce::String profileId_;
    juce::String selectedControlId_;
    juce::OwnedArray<ControllerSurfaceCell> cells_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllerSurfaceComponent)
};

} // namespace synth::ui
