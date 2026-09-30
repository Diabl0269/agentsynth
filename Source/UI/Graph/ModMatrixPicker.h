#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

/** The Mod Matrix's source/destination picker: a search field over a list grouped under category
 *  headers. Lives in a juce::CallOutBox the caller launches (see ModMatrixComponent); it never
 *  launches one itself. Choosing a row reports its id through `onChoose` and closes the box, so the
 *  caller applies it exactly as it would a combo selection.
 *
 *  Typing filters case-insensitively by the row text (module title plus output or target label); a
 *  category header shows only while one of its rows does. The popup's height is fixed by the full
 *  list, so it never resizes under the cursor while filtering. Up/Down move the highlight, Return
 *  picks it (the first match until moved), Escape closes.
 *
 *  Styling is self-contained (opaque themed panel, explicit colours), like MidiDestinationPicker: a
 *  parentless CallOutBox is a new top-level window that does not inherit the app's LookAndFeel. */
class ModMatrixPicker : public juce::Component {
public:
    struct Item {
        int id = 0;
        juce::String category; // header this row sits under; empty = no header (a flat list)
        juce::String text;     // what the row shows and what the search matches
    };

    ModMatrixPicker(juce::String what, std::vector<Item> items, int selectedId, std::function<void(int)> onChoose);
    ~ModMatrixPicker() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void lookAndFeelChanged() override;
    void parentHierarchyChanged() override;

    /** Closes the launching CallOutBox, if any. Called on a pick and on Escape. */
    void dismiss();

    // ---- Test seams ----
    void setSearchTextForTest(const juce::String& text);
    juce::String getSearchTextForTest() const;
    /** Visible rows in order: category headers (while showing) and item texts. */
    std::vector<juce::String> getVisibleRowNamesForTest() const;
    /** Just the visible item texts, the rows a pick can land on. */
    std::vector<juce::String> getVisibleItemTextsForTest() const;
    /** Picks the nth visible ITEM row, as a click on it would. */
    void chooseVisibleItemForTest(int index);
    /** Sends a key to the search field, as typing would. */
    bool sendKeyForTest(const juce::KeyPress& key);
    int getHighlightedItemIndexForTest() const noexcept { return highlighted_; }
    int getHeightForTest() const noexcept { return getHeight(); }
    juce::TextEditor& getSearchEditorForTest() noexcept;

private:
    class Row;
    class SearchField;

    void rebuildRows();
    void applyFilter();
    void layoutRowColumn();
    void moveHighlight(int delta);
    void setHighlight(int visibleItemIndex);
    void chooseRow(const Row& row);
    void chooseHighlighted();
    void applyColours();
    int preferredHeight() const;
    std::vector<Row*> visibleItemRows() const;

    juce::String what_;
    std::vector<Item> items_;
    int selectedId_ = 0;
    std::function<void(int)> onChoose_;

    std::unique_ptr<SearchField> searchEditor_;
    juce::Viewport viewport_;
    juce::Component rowColumn_;
    std::vector<std::unique_ptr<Row>> rows_;
    int highlighted_ = 0; // index among the visible item rows

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModMatrixPicker)
};

} // namespace synth::ui
