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
// "+ Add controller" (FRO134) is a footer button; the popover itself is AddControllerPopover,
// owned by MidiRemotePanelComponent. Re-link/Recreate for an orphan row are task 8 -- an orphan row
// renders (state glyph + "not on this machine" tooltip) but offers no action of its own yet.
namespace synth::ui {

class ControllersListComponent
    : public juce::Component
    , public juce::TooltipClient {
public:
    /** `standaloneOnly` (plugin build only): a profile for a real device -- it lives on this machine but
     *  the host never sends its messages, so it is listed and inert. */
    enum class RowState { present, absent, orphan, standaloneOnly };

    struct RowModel {
        juce::String profileId;
        juce::String name;
        RowState state = RowState::present;
        // FRO139 (docs/control/midi-remote.md#controller-feedback): the current "Send feedback to"
        // choice, for the context menu's tick mark. feedbackOutputIdentifier is empty when
        // hasFeedbackOutput is false (mirrors ControllerProfile::hasOutput/output).
        bool hasFeedbackOutput = false;
        juce::String feedbackOutputIdentifier;
    };

    /** FRO139: one "Send feedback to" submenu row. Deliberately not juce::MidiDeviceInfo (that
     *  would pull juce_audio_devices into this header) -- MidiRemotePanelComponent maps
     *  juce::MidiOutput::getAvailableDevices() into this on demand, via queryFeedbackOutputs below,
     *  so this component never calls a live device-enumeration API itself: doing so from inside a
     *  juce::PopupMenu build crashed in the headless test process (no CoreMIDI entitlement/bundle),
     *  and the same "owner supplies the device list" split is how showAddControllerPopover() keeps
     *  AddControllerPopover itself off the live juce::MidiInput enumeration too. */
    struct FeedbackDeviceOption {
        juce::String identifier;
        juce::String name;
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

    /** FRO134: hidden in the plugin build, where the list holds only Host MIDI. */
    void setAddControllerVisible(bool visible);
    /** FRO136: the plugin build. Hides "+ Add controller" and words the standalone-only / orphan
     *  rows' tooltip for a host (a standalone assignment does not fire there). */
    void setHosted(bool hosted);

    /** The tooltip a hover over `profileId`'s row shows; empty for a row that needs none. */
    juce::String getTooltipForProfile(const juce::String& profileId) const;
    juce::String getTooltip() override;
    /** Row text as painted, for tests. */
    juce::String getRowDisplayNameForTest(const juce::String& profileId) const;
    /** "+ Add controller" clicked; `anchor` is the button, for the popover to point at. */
    std::function<void(juce::Component& anchor)> onAddControllerRequested;

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
    /** FRO139 (docs/control/midi-remote.md#controller-feedback): right-click "Send feedback to ->"
     *  item chosen -- empty `identifier`/`name` means "None". Hidden in the plugin build, same as
     *  "+ Add controller" and every other device-facing control (setHosted(true)). */
    std::function<void(const juce::String& profileId, const juce::String& identifier, const juce::String& name)>
        onFeedbackOutputRequested;
    /** Supplies the submenu's device rows on demand (called once per right-click, never cached) --
     *  unset means an empty list (just "None"), which is what every test that doesn't care about
     *  device rows gets for free. */
    std::function<std::vector<FeedbackDeviceOption>()> queryFeedbackOutputs;

    void paint(juce::Graphics& g) override;
    void resized() override;
    // Public, matching MixerColumnComponent's own convention, so a test can drive a real
    // right-click through this exactly like a genuine click would.
    void mouseDown(const juce::MouseEvent& event) override;

    static constexpr int kRowHeight = 28;
    static constexpr int kFooterHeight = 34;

private:
    struct Row : RowModel {
        bool activityLit = false;
    };

    void showContextMenuForRow(int rowIndex);
    juce::Rectangle<int> boundsForRow(int rowIndex) const;
    int rowIndexAt(juce::Point<int> position) const;

    std::vector<Row> rows_;
    juce::String selectedProfileId_;
    bool hosted_ = false;
    juce::TextButton addControllerButton_{"+ Add controller"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllersListComponent)
};

} // namespace synth::ui
