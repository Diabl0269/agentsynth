#pragma once

// The source list that opens under the panel's rows: a search box and every source the Mod Matrix would offer, in
// collapsible groups, each row saying how many things it moves. Picking one reports it; the panel connects it. A
// source already on the knob is shown greyed. Searching expands the groups that have a match, hides the rest and
// adds "New <module>" rows for module types that match; clearing the search puts back the folds the user had.
// docs/modules/modulation.md#the-mod-dot-menu.

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
    static constexpr int kSearchHeight = 28;
    static constexpr int kLinksHeight = 22;
    static constexpr int kMaxListHeight = 250;
    static constexpr double kFoldMs = 160.0;

    struct Choice {
        ModSourceItem item;
        bool added = false;
        int targets = 0;      // how many things the source already moves
        juce::String newType; // set on a "New <module>" row: the factory key it creates
    };

    explicit ModDotAddSourcePage(juce::String paramName);
    ~ModDotAddSourcePage() override;

    /** Replaces the list (groups in catalog order, empty ones left out), keeping the user's folds and the query. */
    void setChoices(std::vector<Choice> choices);
    /** The tallest the page may be (0 = no limit beyond kMaxListHeight): the list scrolls inside what is left. */
    void setMaxHeight(int height);
    /** A fresh opening: empty search, folds as the user left them. */
    void reset();

    /** Esc: clears the search when it has text, else asks to collapse. Returns true when it did something. */
    bool stepBack();

    std::function<void()> onCollapseRequested;
    std::function<void(const ModSourceItem&)> onPick;
    /** A "New <module>" row: the factory key and the output channel to read the new source from. */
    std::function<void(const juce::String& typeName, int channel)> onPickNew;

    int preferredHeight() const override;
    void focusEntry() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Test seams and inspection.
    juce::TextEditor& searchEditor() noexcept { return *search_; }
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
