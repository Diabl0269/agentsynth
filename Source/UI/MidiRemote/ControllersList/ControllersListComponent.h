#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// ControllersListComponent.h -- FRO131 (docs/control/midi-remote-ui.md#controllers-list-left): the
// MIDI Remote panel's left region, one row per controller profile. Deliberately decoupled from
// synth::ControllerProfile/MidiLearnController -- it renders a plain view-model MidiRemotePanelComponent
// builds each rebuild (mirroring MixerColumnComponent's own callback-driven-child shape), so this
// component is headless-testable with no engine/store at all.
//
// Out of scope for FRO131 (task 7): "+ Add controller", Detect. Re-link/Recreate for an orphan row
// are task 8 -- an orphan row renders (state glyph + "not on this machine" tooltip) but offers no
// action of its own yet.
namespace synth::ui {

class ControllersListComponent : public juce::Component {
public:
    enum class RowState { present, absent, orphan };

    struct RowModel {
        juce::String profileId;
        juce::String name;
        RowState state = RowState::present;
    };

    ControllersListComponent();
    ~ControllersListComponent() override;

    /** Structural rebuild (profile added/removed/renamed) -- call only when the row SET or a row's
     *  own fields change, never on the 10 Hz activity tick (see setActivityLit()). Preserves the
     *  current selection if `rows` still contains it. */
    void setRows(const std::vector<RowModel>& rows);

    /** MidiRemotePanelComponent::refreshActivity()'s per-tick call: repaints ONLY the one row whose
     *  lit state actually changed (Source/UI/CLAUDE.md's no-unconditional-repaint rule) -- the
     *  caller already does rising/falling-edge tracking and calls this only on a real transition. */
    void setActivityLit(const juce::String& profileId, bool lit);

    void setSelectedProfileId(const juce::String& profileId);
    juce::String getSelectedProfileId() const noexcept { return selectedProfileId_; }

    /** FRO263: proves the live-refresh pipeline actually reaches this component's rows, without a
     *  getter into every row's own fields. */
    int getRowCountForTest() const noexcept { return static_cast<int>(rows_.size()); }

    /** Row clicked -- select it (also drives the surface/inspector via MidiRemotePanelComponent). */
    std::function<void(const juce::String& profileId)> onSelectProfile;
    /** Right-click "Rename" committed (inline text-edit or an alert text prompt -- implementation's
     *  choice; must not fire for an empty/unchanged name). */
    std::function<void(const juce::String& profileId, const juce::String& newName)> onRenameRequested;
    /** Right-click "Export..." -- opens a native save-file chooser for the destination; the
     *  component owns the chooser, the callback just receives the already-chosen profile id (the
     *  chooser itself calls MidiRemotePanelComponent/MidiLearnController::exportProfile). */
    std::function<void(const juce::String& profileId)> onExportRequested;
    /** Right-click "Delete..." -- the component shows the confirm alert itself, asking the owner
     *  for the orphan count first via `countProjectAssignments` (so the dialog text can read
     *  "will orphan N assignments" -- docs/control/midi-remote-ui.md#controllers-list-left) and
     *  only invoking this callback if the user confirms. */
    std::function<int(const juce::String& profileId)> countProjectAssignments;
    std::function<void(const juce::String& profileId)> onDeleteConfirmed;

    void paint(juce::Graphics& g) override;
    void resized() override;
    // Public, matching MixerColumnComponent's own convention, so a test can drive a real
    // right-click through this exactly like a genuine click would.
    void mouseDown(const juce::MouseEvent& event) override;

    static constexpr int kRowHeight = 28;

private:
    struct Row : RowModel {
        bool activityLit = false;
    };

    void showContextMenuForRow(int rowIndex);
    juce::Rectangle<int> boundsForRow(int rowIndex) const;
    int rowIndexAt(juce::Point<int> position) const;

    std::vector<Row> rows_;
    juce::String selectedProfileId_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllersListComponent)
};

} // namespace synth::ui
