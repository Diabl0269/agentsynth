#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// ActionPickerComponent.h -- FRO135 (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn):
// "Choose an action" -- a searchable list of the actions a MIDI control can trigger, grouped by
// ShortcutCategory in the Shortcuts tab's own order and named by ShortcutManager::getActionDescription.
// Only command-dispatched actions are offered (AppCommands::getCommandForAction != kNoCommand); a
// surface action such as a bare arrow key has no command to invoke.
namespace synth::ui {

struct ActionPickerRow {
    bool isHeader = false;
    juce::String actionId; // empty for a header
    juce::String label;    // the category name for a header, else the action's description
};

/** The rows the picker shows for `filter` (case-insensitive substring of the action's description;
 *  empty = everything). A category with no matching action gets no header. */
std::vector<ActionPickerRow> buildActionPickerRows(const juce::String& filter);

class ActionPickerComponent
    : public juce::Component
    , private juce::ListBoxModel {
public:
    ActionPickerComponent();
    ~ActionPickerComponent() override;

    /** An action row was chosen. */
    std::function<void(const juce::String& actionId)> onChosen;

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
