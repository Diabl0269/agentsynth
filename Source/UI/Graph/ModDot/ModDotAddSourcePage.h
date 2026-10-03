#pragma once

// The panel's second page: every source the Mod Matrix would offer, in collapsible groups, with a search box.
// Picking one reports it; the panel connects it and returns to the sources page. A source already on the knob is
// shown greyed as "added". Searching expands the groups that have a match and hides the rest; clearing the search
// puts back the folds the user had. docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotAddSourceParts.h"
#include "ModDotPage.h"
#include "UI/Layout/NavigationSearchField.h"
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace synth::ui {

class ModDotAddSourcePage final : public ModDotPage {
public:
    static constexpr int kHeaderHeight = 30;
    static constexpr int kSearchHeight = 28;
    static constexpr int kLinksHeight = 22;
    static constexpr int kMaxListHeight = 250;
    static constexpr double kFoldMs = 160.0;

    struct Choice {
        ModSourceItem item;
        bool added = false;
    };

    explicit ModDotAddSourcePage(juce::String paramName);
    ~ModDotAddSourcePage() override;

    /** Replaces the list (groups in catalog order, empty ones left out), keeping the user's folds and the query. */
    void setChoices(std::vector<Choice> choices);
    /** The tallest the page may be (0 = no limit beyond kMaxListHeight): the list scrolls inside what is left. */
    void setMaxHeight(int height);
    /** A fresh arrival: empty search, the search field focused. */
    void reset();

    /** Esc: clears the search when it has text, else asks to go back. Returns true when it did something. */
    bool stepBack();

    std::function<void()> onBackRequested;
    std::function<void(const ModSourceItem&)> onPick;

    int preferredHeight() const override;
    void focusEntry() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Test seams and inspection.
    juce::TextEditor& searchEditor() noexcept { return *search_; }
    juce::Button& backButton() noexcept { return back_; }
    juce::Button& expandAllButton() noexcept { return expandAll_; }
    juce::Button& collapseAllButton() noexcept { return collapseAll_; }
    void setQuery(const juce::String& query); // as typing would
    /** Visible group names in order, and the visible rows' labels in order. */
    std::vector<juce::String> visibleGroupNames() const;
    std::vector<juce::String> visibleRowLabels() const;
    ModDotChoiceRow* visibleRow(int index) const;
    ModDotGroupHeader* header(const juce::String& groupName) const;
    bool isGroupExpanded(const juce::String& groupName) const; // as shown (target), search included
    bool isAnimating() const noexcept { return anim_.isRunning(); }
    juce::String noMatchText() const { return showsNoMatch() ? "No source matches" : juce::String(); }

private:
    struct Section;
    void rebuild();
    void applyState(bool animate);
    void layoutList();
    void toggleGroup(ModSourceGroup group, bool expand);
    void setAll(bool expand);
    bool effectiveExpanded(ModSourceGroup group) const;
    bool rowMatches(const ModDotChoiceRow& row) const;
    void pickBestMatch();
    std::vector<juce::Component*> focusStops() const;
    void moveStop(juce::Component* from, int step);
    bool showsNoMatch() const;

    juce::String paramName_;
    std::vector<Choice> choices_;
    std::map<ModSourceGroup, bool> userExpanded_;
    std::set<ModSourceGroup> searchCollapsed_;
    juce::String query_;
    std::vector<std::unique_ptr<Section>> sections_;

    ModDotGlyphButton back_;
    std::unique_ptr<NavigationSearchField> search_;
    ModDotLinkButton expandAll_;
    ModDotLinkButton collapseAll_;
    juce::Viewport viewport_;
    juce::Component list_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver anim_;
    int listHeight_ = 0;
    int maxHeight_ = 0;
    int listViewHeight() const;
};

} // namespace synth::ui
