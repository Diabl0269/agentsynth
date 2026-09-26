#pragma once

#include "MidiRemote/RemoteModel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// ActionPickerComponent.h -- FRO135 (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn):
// "Choose an action" -- a searchable list of the actions a MIDI control can trigger, grouped by
// ShortcutCategory in the Shortcuts tab's own order and named by ShortcutManager::getActionDescription.
// Only command-dispatched actions are offered (AppCommands::getCommandForAction != kNoCommand); a
// surface action such as a bare arrow key has no command to invoke. FRO236
// (docs/control/midi-remote.md#continuous-targets) appends one more group after every action category -- "Continuous"
// -- with a fixed three rows (Tempo/Playhead/Master Volume), named via synth::continuousTargetDisplayName so the picker
// can never disagree with the panel/inspector's own labels. FRO142 (docs/control/midi-remote.md#pages)
// appends a further "Pages" group -- Next page / Previous page, then one "Page N" row per page the
// selected control's OWN controller currently has (docs/control/midi-remote-ui.md#pages) -- so the
// row count depends on `effectivePageCount`, unlike every group above it.
namespace synth::ui {

struct ActionPickerRow {
    bool isHeader = false;
    bool isContinuous = false; // FRO236: this row picks a continuous target, not an action
    bool isPage = false;       // FRO142: this row picks a page Target, not an action
    juce::String actionId;     // empty for a header, a continuous row, or a page row
    juce::String label;        // the category name for a header, else the target's description
    synth::ContinuousTargetKind continuousKind = synth::ContinuousTargetKind::bpm; // valid iff isContinuous
    synth::PageCommand pageCommand = synth::PageCommand::next;                     // valid iff isPage
    int pageNumber = 1; // valid iff isPage; only meaningful for pageCommand == go ("Page N")
};

/** The rows the picker shows for `filter` (case-insensitive substring of the row's own label;
 *  empty = everything) and `effectivePageCount` (>= 1, the selected control's controller's own
 *  RemoteEngine::getEffectivePageCount -- 1 with no controller selected). A category (action,
 *  continuous, or page) with no matching row gets no header. */
std::vector<ActionPickerRow> buildActionPickerRows(const juce::String& filter, int effectivePageCount = 1);

class ActionPickerComponent
    : public juce::Component
    , private juce::ListBoxModel {
public:
    ActionPickerComponent();
    ~ActionPickerComponent() override;

    /** An action row was chosen. */
    std::function<void(const juce::String& actionId)> onChosen;
    /** FRO236: a continuous-target row was chosen. */
    std::function<void(synth::ContinuousTargetKind kind)> onContinuousChosen;
    /** FRO142: a page row was chosen. */
    std::function<void(synth::PageCommand command, int page)> onPageChosen;

    /** FRO142: set BEFORE the picker is shown (MidiRemotePanelComponent::showActionPicker()) so the
     *  "Page N" rows match the selected control's own controller. Rebuilds rows_ under the current
     *  filter; a no-op if the count is unchanged. */
    void setEffectivePageCount(int count);

    void setFilter(const juce::String& filter);
    int getRowCountForTest() const { return static_cast<int>(rows_.size()); }
    const ActionPickerRow& getRowForTest(int index) const { return rows_[static_cast<size_t>(index)]; }
    /** What a click on row `index` does. */
    void chooseRowForTest(int index) { chooseRow(index); }

    void resized() override;

    static constexpr int kWidth = 320;
    static constexpr int kHeight = 380;

private:
    int getNumRows() override { return static_cast<int>(rows_.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override { chooseRow(row); }
    void returnKeyPressed(int row) override { chooseRow(row); }
    void chooseRow(int row);

    std::vector<ActionPickerRow> rows_;
    int effectivePageCount_ = 1;
    juce::TextEditor searchEditor_;
    juce::ListBox list_{"actions", this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActionPickerComponent)
};

} // namespace synth::ui
