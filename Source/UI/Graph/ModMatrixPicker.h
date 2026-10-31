#pragma once

#include "UI/Layout/CalloutReveal.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/NavigationSearchField.h"
#include "UI/Layout/SearchMatch.h"
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
 *  Typing filters case-insensitively by the row text (module title plus output or target label) and its
 *  `searchText`, word by word: each space-separated word must appear somewhere in them (synth::ui::searchMatches), and
 *  highlights the matched letters in the row text and its detail line; a
 *  category header shows only while one of its rows does. A row the filter takes out fades while its slot closes up;
 *  the highlight and a pick only ever land on rows that are still shown. Only the rows in view (plus a few) exist as
 *  components, so a list of thousands opens and filters at once. The
 *  highlighted row on a query is the best match (synth::ui::searchScore), the first among equals. The popup's height is
 * fixed by the full list, so it never resizes under the cursor while filtering. Up/Down move the highlight, Return
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
        // Extra words the search also matches but the row does not show (the add-automation picker puts the
        // module title here, since its rows read as just the parameter name under a module header).
        juce::String searchText;
        // A muted second line under the text, also searched (the add-modulator picker says where an LFO lives
        // and what it moves). Empty = a one-line row.
        juce::String detail;
        // False = shown greyed, never highlighted or picked; `detail` should say why.
        bool enabled = true;
        // The row the list is about right now (the current source): a tick before the text.
        bool ticked = false;
        // A row that creates something ("New LFO"): a green plus before the text and a "New" tag at the right. It
        // stays in the list whatever the search says, so a query that finds nothing can still create.
        bool isNew = false;
        // What a screen reader announces for the row when it should say more than `text` (the add-automation picker
        // adds where the module lives). Empty = `text` (and `detail`).
        juce::String accessibleName;
    };

    ModMatrixPicker(juce::String what, std::vector<Item> items, int selectedId, std::function<void(int)> onChoose);
    ~ModMatrixPicker() override;

    /** Runs once from the destructor, however the picker closed (a pick, Escape, a click away): the launcher's cue
     *  to hand keyboard focus back. After a pick it runs after `onChoose`. */
    std::function<void()> onClosed;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void lookAndFeelChanged() override;
    void parentHierarchyChanged() override;
    // Repaints the focus outline: a call-out is its own window, so nothing else will when focus moves.
    void focusOfChildComponentChanged(FocusChangeType) override { repaint(); }

    /** Re-words what a screen reader announces for the box and its search field, for a caller that reuses the
     *  picker for something other than modulation. The search field's placeholder follows `what`. */
    void setAccessibleNames(const juce::String& pickerTitle, const juce::String& searchTitle);
    /** The search field's placeholder, for a picker that also creates ("Search or create a source"); the default is
     *  "Search <what>s...". */
    void setSearchPlaceholder(const juce::String& text);

    /** Replaces the rows (the list can fill after a download); the new rows fade in and the search text stays. */
    void setItems(std::vector<Item> items);
    /** Puts `footer` in a strip `height` tall under the list and grows the popup to fit; replaces an earlier one. */
    void setFooter(std::unique_ptr<juce::Component> footer, int height);
    /** Removes the footer and shrinks the popup back. */
    void clearFooter();
    juce::Component* getFooter() const noexcept { return footer_.get(); }

    /** Closes the launching CallOutBox, if any. Called on a pick and on Escape. */
    void dismiss();

    // ---- Test seams ----
    void setSearchTextForTest(const juce::String& text);
    juce::String getSearchTextForTest() const;
    /** Visible rows in order: category headers (while showing) and item texts. */
    std::vector<juce::String> getVisibleRowNamesForTest() const;
    /** Just the visible item texts, the rows a pick can land on. */
    std::vector<juce::String> getVisibleItemTextsForTest() const;
    /** The second lines of the visible item rows, in order (empty for a one-line row). */
    std::vector<juce::String> getVisibleItemDetailsForTest() const;
    /** The highlighted letter ranges of the visible item rows' text, in order (empty for a row with no hit). */
    std::vector<std::vector<SearchSpan>> getVisibleItemHighlightSpansForTest() const;
    /** False for a visible item row shown disabled. */
    bool isVisibleItemPickableForTest(int index) const;
    /** True for a visible item row drawn with the current-item tick / the plus and "New" tag. */
    bool isVisibleItemTickedForTest(int index) const;
    bool isVisibleItemNewForTest(int index) const;
    juce::String getSearchPlaceholderForTest() const;
    /** Picks the nth visible ITEM row, as a click on it would. */
    void chooseVisibleItemForTest(int index);
    /** Sends a key to the search field, as typing would. */
    bool sendKeyForTest(const juce::KeyPress& key);
    int getHighlightedItemIndexForTest() const noexcept { return highlighted_; }
    int getHeightForTest() const noexcept { return getHeight(); }
    juce::TextEditor& getSearchEditorForTest() noexcept;
    /** The column the rows live in, and whether any row is still fading in or out of the filter. */
    juce::Component& getRowColumnForTest() noexcept { return rowColumn_; }
    bool isFilterFadingForTest() const;
    /** How many row components exist: the rows in view plus a few, however long the list. */
    int getRowComponentCountForTest() const noexcept { return (int)pool_.size(); }
    /** How many row components the filter passes have steered: the on-screen ones, never the whole list. */
    int getRowsSteeredForTest() const noexcept { return rowsSteered_; }
    /** The viewport's top edge, and whether the highlighted row is inside the view. */
    int getViewTopForTest() const noexcept { return viewport_.getViewPositionY(); }
    bool isHighlightInViewForTest() const;

private:
    CalloutReveal reveal_{*this};
    class Row;

    /** One line of the list, header or item, whether or not it has a row component right now. */
    struct Entry {
        bool header = false;
        int item = 0; // into items_; for a header, the first item under it
        int height = 0;
        juce::String haystack; // what the search reads (items only)
        bool shown = true;     // what the filter says, at once; the row's fade follows
        bool wasShown = true;
        int y = 0;
        Row* row = nullptr; // the component showing it, while it is in view or fading
    };
    struct RowViewport : juce::Viewport {
        std::function<void()> onViewMoved;
        void visibleAreaChanged(const juce::Rectangle<int>&) override {
            if (onViewMoved)
                onViewMoved();
        }
    };

    void rebuildRows();
    void applyFilter();
    void steerRows();
    void layoutEntries();
    void syncRows();
    Row& acquireRow(int entryIndex);
    void releaseRow(Entry& entry);
    void onRowFadeFrame();
    bool anyRowFading() const;
    void moveHighlight(int delta);
    int nextPickable(int from, int step) const;
    void setHighlight(int visibleItemIndex);
    void hoverEntry(int entryIndex);
    void chooseEntry(int entryIndex);
    void chooseHighlighted();
    void applyColours();
    int preferredHeight() const;
    const Item& itemAtVisible(int visibleIndex) const;

    juce::String what_;
    juce::String placeholder_;
    std::vector<Item> items_;
    int selectedId_ = 0;
    std::function<void(int)> onChoose_;

    std::unique_ptr<NavigationSearchField> searchEditor_;
    RowViewport viewport_;
    juce::Component rowColumn_;
    FadeVisibility listFade_{&rowColumn_};
    std::unique_ptr<juce::Component> footer_;
    int footerHeight_ = 0;
    std::vector<std::unique_ptr<Row>> pool_; // bound and free row components; each owns its fade
    std::vector<Row*> freeRows_;
    std::vector<Entry> entries_;
    std::vector<int> visibleItems_; // entries_ indices of the items the filter keeps, in order
    juce::String query_;
    int highlighted_ = 0; // index into visibleItems_
    bool laidOut_ = false;
    bool markGutter_ = false; // some row carries a tick or a new mark, so every row leaves room for one
    bool syncing_ = false;
    int rowsSteered_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModMatrixPicker)
};

} // namespace synth::ui
