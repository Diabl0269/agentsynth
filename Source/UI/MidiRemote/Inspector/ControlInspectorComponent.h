#pragma once

#include "MidiRemote/RemoteModel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// ControlInspectorComponent.h -- FRO131 (docs/control/midi-remote-ui.md#inspector-right): the MIDI
// Remote panel's right region -- the selected control's name/kind/message spec/encoding/button
// mode, then one block per assignment (a control may carry one project + one global assignment at
// most for now, docs/control/midi-remote-ui.md#inspector-right). Takeover, range+invert and Forget
// are wired (FRO131); FRO134 adds the editable name and kind, the Encoding combo and Auto-detect...;
// FRO135 adds Learn target (the assign-from-panel popover) and limits an orphaned row to Forget.
// Still inert: Relearn (re-detecting a control's message needs a panel-side learn primitive).
namespace synth::ui {

class ControlInspectorComponent : public juce::Component {
public:
    struct AssignmentRowModel {
        synth::Assignment assignment;
        juce::String scopeLabel;  // "Project" or "Global"
        juce::String drivesLabel; // "Filter . Cutoff" / the action's display name
        juce::String nodeUuid;    // empty for an action target -- click-to-locate is parameter-only
        bool isOrphaned = false;  // target no longer resolves -- warning colour
    };

    struct ControlModel {
        synth::Control control;
        bool hasControl = false; // false = "no control selected" empty state
        std::vector<AssignmentRowModel> assignments;
        /** FRO270: 2+ when the surface has a multi-selection -- overrides the empty state's "No
         *  control selected" with "N controls selected" instead; every per-control field stays
         *  hidden exactly as it does for `hasControl == false`, since there is no single control's
         *  name/kind/encoding/etc. to show. 0 or 1 -- the ordinary single-control states below. */
        int selectedCount = 0;
    };

    ControlInspectorComponent();
    ~ControlInspectorComponent() override;

    /** Structural rebuild for the newly-selected control (or the empty state). Call on selection
     *  change or whenever the selected control's own assignments change (Forget, a takeover/range
     *  edit committed elsewhere, an undo/redo restoring the project doc) -- this component holds
     *  no live state of its own between rebuilds. */
    void setControl(const ControlModel& model);

    /** FRO136: the Preferences default takeover, so an assignment set to Default reads "Default (Jump)"
     *  and so on. Rebuilds the rows when it changes; Scale until set. */
    void setDefaultTakeover(synth::Takeover takeover);

    /** Parameter-target row's "Drives" label clicked -- jump to the module on the canvas. */
    std::function<void(const juce::String& nodeUuid)> onLocateRequested;
    /** A takeover/range/invert edit was committed (Enter / focus-lost / combo change) on the row
     *  for `assignment.id` -- `assignment` carries every field, already updated, ready for
     *  MidiLearnController::updateAssignment(). Fired only for a PROJECT (parameter-target) row;
     *  a global (action-target) row's takeover/range controls are disabled (Takeover "applies only
     *  to absolute continuous encodings", docs/control/midi-remote.md#takeover -- an action target
     *  is always a button). */
    std::function<void(const synth::Assignment& assignment)> onAssignmentEdited;
    /** FRO134: the name, kind or encoding of the selected control was edited -- `control` carries
     *  every field, already updated, ready for MidiLearnController::updateControl(). */
    std::function<void(const synth::Control& control)> onControlEdited;
    /** FRO134: "Auto-detect..." pressed (enabled only for a CC knob/encoder). */
    std::function<void(const synth::Control& control)> onAutoDetectRequested;
    /** FRO135: "Learn target" pressed; `anchor` is the button, for the assign popover to hang from. */
    std::function<void(juce::Component& anchor)> onLearnTargetRequested;
    /** "Forget" on the row for `assignmentId`. */
    std::function<void(const juce::String& assignmentId)> onForgetRequested;

    /** FRO270 test seam: the model setControl() last built, for asserting selectedCount/hasControl
     *  without a real screenshot. */
    const ControlModel& getModelForTest() const noexcept { return model_; }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class AssignmentRow;

    void rebuildRows();
    void fireControlEdited();

    ControlModel model_;
    synth::Takeover defaultTakeover_ = synth::Takeover::scale;
    juce::Label nameLabel_;
    juce::ComboBox kindCombo_;
    juce::Label messageSpecLabel_;
    juce::TextButton relearnButton_{"Relearn"}; // disabled -- needs the panel-side learn primitive
    juce::ComboBox encodingCombo_;
    juce::TextButton autoDetectButton_{"Auto-detect..."};
    juce::TextButton learnTargetButton_{"Learn target"};
    juce::Label buttonModeLabel_;
    // FRO141 (docs/control/midi-remote.md#focus-bank): "Follow selection (focus bank)".
    juce::ToggleButton focusBankToggle_{"Follow selection (focus bank)"};

    juce::OwnedArray<AssignmentRow> assignmentRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlInspectorComponent)
};

} // namespace synth::ui
