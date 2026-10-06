#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FoldAllButton.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <set>
#include <vector>

// The Settings "Keyboard Shortcuts" tab.
//
// One row per registered action: description label on the left, rebind button on the right. Clicking
// a button enters listening mode (button turns orange with "Press a key..."); pressing any key
// except Escape commits the new binding, swapping with whatever action already held that key IN THE
// SAME CATEGORY (ShortcutManager::getConflictingAction is category-scoped — see its comment).
// Reset-to-defaults is guarded by a confirmation dialog, and JSON export/import round-trips every
// binding through ShortcutManager::encodeKeyPress / parseKeyPress.
//
// STRUCTURE (the reason this is not a flat list any more): the table grew past forty actions when
// the timeline and piano-roll surfaces became rebindable, and a flat list that long is unusable —
// so rows are grouped into one collapsible section per ShortcutCategory, with a search field above
// them. The idioms are lifted from ModuleLibraryComponent (docs/layout/module-library.md) on purpose, so the
// two collapsible lists in the app behave identically: header rows with a chevron, a
// collapsed-set keyed by the header's name, and a top strip whose label flips between
// "Collapse all" and "Expand all" (the shared synth::ui::FoldAllButton, as in the Preferences All view).
// Headers, the strip and every rebind button are real buttons,
// so Tab visits them top to bottom and Space/Return act on them; while a row listens for its new
// key, that row's button keeps focus and takes every key, Escape cancelling.
//
// Two things it deliberately does NOT copy from the library sidebar:
//   - No fold ANIMATION. The library's accordion is a VBlank-driven AnimationDriver over a
//     hand-laid-out row list; here the rows are real child components inside a juce::Viewport, so a
//     fold would have to animate child bounds every frame for no benefit in a modal settings dialog.
//     Collapsing is instant.
//   - The folds ARE remembered (user setting shortcutsFolded, the folded categories by stable name):
//     the tab is constructed fresh each time the Settings window opens, and it restores them before
//     its first layout, so it opens as it was left.
//
// ROW INDEXING IS A CONTRACT: row i is ShortcutManager::getActionIds()[i], sections or no sections.
// The section headers are separate widgets, never entries in the row vectors, and
// ShortcutManager's action table keeps each category's ids contiguous so a section is always one
// unbroken run. ShortcutsSettingsTabTests pins row i to ids[i]; getRowDescription/getRowBindingText
// answer for the action at that index whether or not its row is currently on screen (collapsed or
// filtered out).
//
// NOTE: ShortcutsSettingsTab.cpp MUST be added to BOTH the app target AND
// the test target in CMakeLists.txt (consolidation pass).
class ShortcutsSettingsTab : public juce::Component {
public:
    // appProperties (nullable) is where the folded sections are remembered; null keeps them in memory only.
    explicit ShortcutsSettingsTab(ShortcutManager& shortcutManager,
                                  juce::ApplicationProperties* appProperties = nullptr);
    ~ShortcutsSettingsTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // -------------------------------------------------------------------------
    // Pure decision helpers — no component state, callable headlessly
    //
    // The filter and section rules live here rather than inline in rebuildLayout() so they can be
    // tested without a GUI, and so "why did this row disappear" has exactly one answer to read.
    // -------------------------------------------------------------------------

    /** Trimmed query. Empty means "no filter", which is why leading/trailing spaces cannot
     *  accidentally hide every row. */
    static juce::String normalisedQuery(const juce::String& raw) { return raw.trim(); }

    /** Whether a row survives the filter. Matched case-insensitively against BOTH the action's
     *  description and its current binding text, so "cmd" finds every Cmd shortcut and "transpose"
     *  finds the piano-roll block — searching only the description would make the list useless for
     *  the commonest question ("what is on Shift+Q?").
     *
     *  An empty query matches everything: this answers "is this row visible", not "is this a
     *  highlight hit", so no filter means no hiding. */
    static bool rowMatchesQuery(const juce::String& query, const juce::String& description,
                                const juce::String& bindingText);

    /** Whether a section (header included) appears at all. Under an active filter a section with no
     *  surviving rows is dropped entirely rather than left as a lone header over empty space —
     *  ModuleLibraryComponent::buildRows does the same. */
    static bool sectionIsVisible(bool filterActive, bool sectionHasMatch) { return !filterActive || sectionHasMatch; }

    /** Whether a visible section shows its rows. A filter FORCES a matching section open without
     *  touching the collapse flag, so a match can never be trapped inside a fold — and clearing the
     *  query restores exactly the folds the user had. */
    static bool sectionIsExpanded(bool filterActive, bool sectionHasMatch, bool collapsed) {
        if (filterActive)
            return sectionHasMatch;
        return !collapsed;
    }

    // -------------------------------------------------------------------------
    // Testing hooks
    // -------------------------------------------------------------------------

    // Number of action rows (== ShortcutManager::getActionIds().size()); unaffected by collapse or
    // filter state, which is what keeps row i == ids[i].
    int getShortcutCount() const { return static_cast<int>(bindButtons.size()); }

    // Description text of row [index].
    juce::String getRowDescription(int index) const;

    // Binding display string currently shown on row [index].
    juce::String getRowBindingText(int index) const;

    // Programmatically starts listening mode on row [index] (used by tests to simulate a button
    // click without needing a mouse event).
    void startListeningForTest(int index) { startListening(index); }

    // ---- Search / collapse state (test + owner hooks) ----
    void setSearchText(const juce::String& text);
    juce::String getSearchText() const { return searchEditor.getText(); }
    bool isSearchActive() const { return normalisedQuery(searchEditor.getText()).isNotEmpty(); }

    bool isSectionCollapsed(ShortcutCategory category) const;
    void setSectionCollapsed(ShortcutCategory category, bool collapsed);
    void toggleSection(ShortcutCategory category) { setSectionCollapsed(category, !isSectionCollapsed(category)); }
    /** True when every section is folded — drives the top strip's label and what clicking it does. */
    bool areAllSectionsCollapsed() const;
    void setAllSectionsCollapsed(bool collapsed);
    void toggleAllSections() { setAllSectionsCollapsed(!areAllSectionsCollapsed()); }

    /** True when row [index] currently has a laid-out row on screen — i.e. its section is visible
     *  and expanded and it survived the filter. The one observable that answers "did the filter hide
     *  this" without decoding pixels. */
    bool isRowVisible(int index) const;

    /** True when `category`'s header is currently laid out. */
    bool isSectionVisible(ShortcutCategory category) const;

    // ---- Row geometry (pixels) ----
    static constexpr int kSearchHeight = 26;
    static constexpr int kTopStripHeight = synth::ui::FoldAllButton::kStripHeight;
    static constexpr int kSectionHeaderHeight = 22;
    static constexpr int kRowHeight = 26;
    static constexpr int kRowGap = 3;
    static constexpr int kSectionGap = 10;
    static constexpr int kDescriptionWidth = 240;

private:
    void startListening(int index);
    void cancelListening();
    void refreshBindingLabels();

    /** Recomputes which rows/headers are on screen and where, then applies the bounds and the
     *  visibility. THE single layout pass: the header buttons, the rows and the empty-state text
     *  all read `layout`, so they cannot disagree about where a row is — the same "one
     *  enumeration" rule ModuleLibraryComponent::buildRows follows. */
    void rebuildLayout();

    // One entry per thing on screen, top to bottom, in CONTENT (scrolled) coordinates.
    struct LayoutEntry {
        // -1 for a section header; otherwise the index into actionIds / descLabels / bindButtons.
        int actionIndex = -1;
        ShortcutCategory category = ShortcutCategory::General;
        juce::Rectangle<int> bounds;
        bool isHeader = false;
    };

    /** The scrolled content: a bare host whose paint delegates straight back to the tab, so the
     *  rules and the empty-state text are drawn in the same coordinate space the rows are laid out
     *  in. */
    struct RowsHost : juce::Component {
        explicit RowsHost(ShortcutsSettingsTab& o)
            : owner(o) {
            setMouseClickGrabsKeyboardFocus(false);
        }
        void paint(juce::Graphics& g) override { owner.paintRows(g); }
        ShortcutsSettingsTab& owner;
    };

    /** A section header: a real button, so Tab reaches it and Space/Return fold the section. */
    class HeaderButton
        : public juce::Button
        , public synth::ui::FoldableHeader {
    public:
        explicit HeaderButton(ShortcutCategory c);
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
        void setCollapsed(bool isCollapsed);
        bool isFolded() const override { return collapsed_; }
        void setFolded(bool folded) override;

    private:
        bool collapsed_ = false;
    };

    /** A rebind button. While its row is listening it hands every key to the tab, so any key
     *  (Space and Return included) becomes the new binding rather than pressing the button. */
    class RebindButton : public juce::TextButton {
    public:
        std::function<bool(const juce::KeyPress&)> onKey;
        std::function<void()> onFocusLost;
        bool keyPressed(const juce::KeyPress& key) override {
            return (onKey && onKey(key)) || juce::TextButton::keyPressed(key);
        }
        void focusLost(FocusChangeType type) override {
            juce::TextButton::focusLost(type);
            if (onFocusLost)
                onFocusLost();
        }
    };

    void paintRows(juce::Graphics& g);

    ShortcutManager& shortcutManager;
    juce::ApplicationProperties* appProperties = nullptr; // weak, nullable
    void saveFolds();

    juce::Label titleLabel;
    juce::TextEditor searchEditor;
    juce::StringArray actionIds;
    std::vector<std::unique_ptr<juce::Label>> descLabels;
    std::vector<std::unique_ptr<RebindButton>> bindButtons;
    std::vector<std::unique_ptr<HeaderButton>> headerButtons; // in ShortcutManager::getCategoryOrder() order
    synth::ui::FoldAllButton foldAllButton;                   // the pinned strip above the rows
    juce::TextButton resetButton;
    juce::TextButton exportButton;
    juce::TextButton importButton;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int listeningIndex = -1;

    juce::Viewport rowsViewport;
    RowsHost rowsHost{*this};
    std::vector<LayoutEntry> layout;
    // Set by resized(): where the collapse-all strip button sits.
    juce::Rectangle<int> topStripBounds;
    synth::ui::ScrollIntoViewOnFocus followFocus_{rowsViewport};
    // Keyed by category, exactly as ModuleLibraryComponent keys its own set by header name.
    std::set<ShortcutCategory> collapsedSections;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutsSettingsTab)
};
