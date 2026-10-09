#pragma once

// The search page of the port connections panel ("Add connection"): a search box and every module the jack can be
// connected to, nearest first. A module with exactly one compatible target is ONE row ("Filter 1 . In 1"); a module
// with several is a foldable group with its targets underneath (a jack is a row, a knob of a modulation output is a
// row too). A target already wired to the jack is shown greyed with "Connected" and cannot be picked. Typing filters
// with the app's shared search (SearchMatch.h: all words, any order, matched letters highlighted, aliases searched
// but not painted), expands the groups that have a match and offers "New <module>" rows for module types with a
// compatible jack. Down moves from the field into the rows, Up/Down walk them, Return with nothing focused picks the
// best match (existing targets before New rows), Escape clears the search and then goes back. Hovering or focusing a
// row reports it so the canvas can preview the cable. Modelled on ModDotAddSourcePage.
// docs/layout/cables.md#port-connections-panel.

#include "PortTargetList.h"
#include "UI/Graph/ModDot/ModDotAddSourceParts.h"
#include "UI/Graph/ModDot/ModDotPage.h"
#include "UI/Layout/NavigationSearchField.h"
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace synth::ui {

class PortTargetSearchPage final : public ModDotPage {
public:
    static constexpr int kBackHeight = 22;
    static constexpr int kSearchHeight = 28;
    static constexpr int kMaxListHeight = 250;
    static constexpr double kFoldMs = 160.0;

    explicit PortTargetSearchPage(juce::String jackTitle);
    ~PortTargetSearchPage() override;

    /** Replaces the targets (modules in the order given, then the "New <module>" rows), keeping the query and the
     *  user's folds. A list that is the same as the one shown keeps its rows and focus. */
    void setTargets(std::vector<PortTargetModule> modules, std::vector<PortTarget> newModules);
    /** The tallest the page may be (0 = no limit beyond kMaxListHeight): the list scrolls inside what is left. */
    void setMaxHeight(int height);
    /** A fresh opening: empty search, folds as the user left them. */
    void reset();
    /** Esc: clears the search when it has text, else asks to go back. */
    bool stepBack();

    std::function<void()> onBackRequested;
    std::function<void(const PortTarget&)> onPick;
    /** The row under the pointer or holding the focus (the cable to preview), or null when none does. */
    std::function<void(const PortTarget*)> onPreview;

    int preferredHeight() const override;
    void focusEntry() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Test seams and inspection.
    juce::TextEditor& searchEditor() noexcept { return *search_; }
    juce::Button& backButton() noexcept { return back_; }
    void setQuery(const juce::String& query); // as typing would
    std::vector<juce::String> visibleGroupNames() const;
    /** The visible rows' painted labels in order (a grouped row shows its port, a single one "module . port"). */
    std::vector<juce::String> visibleRowLabels() const;
    ModDotChoiceRow* visibleRow(int index) const;
    const PortTarget* targetOf(const ModDotChoiceRow& row) const;
    ModDotGroupHeader* header(const juce::String& groupName) const;
    bool isGroupExpanded(const juce::String& groupName) const;
    bool isAnimating() const noexcept { return anim_.isRunning(); }
    juce::String noMatchText() const { return showsNoMatch() ? emptyText() : juce::String(); }
    int listViewHeight() const;
    /** Picks the best match as Return in the field would. */
    void pickBestMatchForTest() { pickBestMatch(); }

private:
    struct Section;
    void rebuild();
    void applyState(bool animate);
    void layoutList();
    void toggleSection(const Section& section, bool expand);
    bool effectiveExpanded(const Section& section) const;
    bool rowMatches(const PortTarget& target) const;
    void pickBestMatch();
    std::vector<juce::Component*> focusStops() const;
    void moveStop(juce::Component* from, int step);
    bool showsNoMatch() const;
    juce::String emptyText() const;
    void previewRow(const ModDotChoiceRow& row, bool lit);

    juce::String jackTitle_;
    std::vector<PortTargetModule> modules_;
    std::vector<PortTarget> newModules_;
    juce::StringArray signature_;
    std::map<juce::uint32, bool> userExpanded_;
    std::set<juce::uint32> searchCollapsed_;
    juce::String query_;
    std::vector<std::unique_ptr<Section>> sections_;

    ModDotLinkButton back_;
    std::unique_ptr<NavigationSearchField> search_;
    juce::Viewport viewport_;
    juce::Component list_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver anim_;
    int listHeight_ = 0;
    int maxHeight_ = 0;
};

} // namespace synth::ui
