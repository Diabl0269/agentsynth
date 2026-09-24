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
// can never disagree with the panel/inspector's own labels.
namespace synth::ui {

struct ActionPickerRow {
    bool isHeader = false;
    bool isContinuous = false; // FRO236: this row picks a continuous target, not an action
    juce::String actionId;     // empty for a header or a continuous row
    juce::String label;        // the category name for a header, else the target's description
    synth::ContinuousTargetKind continuousKind = synth::ContinuousTargetKind::bpm; // valid iff isContinuous
};

/** The rows the picker shows for `filter` (case-insensitive substring of the row's own label;
 *  empty = everything). A category (action or continuous) with no matching row gets no header. */
std::vector<ActionPickerRow> buildActionPickerRows(const juce::String& filter);

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
    juce::TextEditor searchEditor_;
    juce::ListBox list_{"actions", this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActionPickerComponent)
};

} // namespace synth::ui
