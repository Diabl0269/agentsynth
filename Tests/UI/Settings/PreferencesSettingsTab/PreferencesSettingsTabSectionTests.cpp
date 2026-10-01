#include "PreferencesSettingsTabTestFixture.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include <type_traits>

// Topic: the "All" category view: every category under a collapsible header, fold state, the
// the single fold-all button (shared with the Keyboard Shortcuts tab), and that a filter ignores folds.

namespace {
using Category = PreferencesSettingsTab::Category;
constexpr Category kSections[] = {Category::Graph, Category::Timeline, Category::Files,
                                  Category::Mixer, Category::Panels,   Category::MidiRemote};

juce::Component* mixerRow(PreferencesSettingsTab& t) {
    for (auto* child : descendantsOf(t))
        if (auto* l = dynamic_cast<juce::Label*>(child))
            if (l->getText().containsIgnoreCase("Mixer placement"))
                return l;
    return nullptr;
}

void pickAll(PreferencesSettingsTab& tab) {
    tab.getCategoryComboForTest().setSelectedId(static_cast<int>(Category::All) + 1, juce::sendNotificationSync);
}
} // namespace

TEST_F(PreferencesSettingsTabTest, AllIsTheFirstDropDownEntry) {
    PreferencesSettingsTab tab(appProperties);
    auto& combo = tab.getCategoryComboForTest();
    EXPECT_EQ(combo.getItemText(0), "All");
    pickAll(tab);
    EXPECT_EQ(tab.getSelectedCategory(), Category::All);
}

TEST_F(PreferencesSettingsTabTest, AllShowsEveryCategorysRowsUnderOrderedHeaders) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    pickAll(tab);
    auto* guides = findToggleByText(tab, "Show Alignment Guides");
    auto* midi = findToggleByText(tab, "MIDI badges");
    ASSERT_NE(guides, nullptr);
    ASSERT_NE(midi, nullptr);
    EXPECT_TRUE(guides->isVisible());
    EXPECT_TRUE(midi->isVisible());
    EXPECT_NE(mixerRow(tab), nullptr);

    int previousBottom = -1;
    for (auto category : kSections) {
        auto& header = tab.getSectionHeaderForTest(category);
        EXPECT_TRUE(header.isVisible()) << PreferencesSettingsTab::categoryName(category);
        EXPECT_GT(header.getY(), previousBottom) << "headers must not overlap the previous section";
        previousBottom = header.getBottom();
    }
    // The first row of a section sits below its own header, never under it.
    EXPECT_GT(guides->getY(), tab.getSectionHeaderForTest(Category::Graph).getBottom() - 1);
    EXPECT_GT(midi->getY(), tab.getSectionHeaderForTest(Category::MidiRemote).getBottom() - 1);
    EXPECT_TRUE(tab.getFoldAllButtonForTest().isVisible());
}

TEST_F(PreferencesSettingsTabTest, FoldingASectionHidesItsRowsAndShrinksTheContent) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 300);
    pickAll(tab);
    auto& header = tab.getSectionHeaderForTest(Category::Graph);
    auto* guides = findToggleByText(tab, "Show Alignment Guides");
    ASSERT_NE(guides, nullptr);
    const int midiBefore = tab.getSectionHeaderForTest(Category::MidiRemote).getY();

    header.onClick();
    EXPECT_TRUE(tab.isSectionCollapsed(Category::Graph));
    EXPECT_FALSE(guides->isVisible());
    EXPECT_LT(tab.getSectionHeaderForTest(Category::MidiRemote).getY(), midiBefore);
    EXPECT_TRUE(tab.getSectionHeaderForTest(Category::Graph).isVisible()) << "a folded header stays";

    header.onClick();
    EXPECT_FALSE(tab.isSectionCollapsed(Category::Graph));
    EXPECT_TRUE(guides->isVisible());
    EXPECT_EQ(tab.getSectionHeaderForTest(Category::MidiRemote).getY(), midiBefore);
}

// One button does both jobs: it folds every section while any is open, then reads "Expand all" and
// unfolds them once every section is folded.
TEST_F(PreferencesSettingsTabTest, TheFoldAllButtonFoldsEverySectionThenUnfoldsThemAndFlipsItsLabel) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    pickAll(tab);
    auto& button = tab.getFoldAllButtonForTest();
    EXPECT_EQ(button.getButtonText(), "Collapse all");
    EXPECT_EQ(button.getTooltip(), "Fold every section");

    button.onClick();
    for (auto category : kSections)
        EXPECT_TRUE(tab.isSectionCollapsed(category));
    EXPECT_FALSE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "MIDI badges")->isVisible());
    EXPECT_FALSE(tab.contentOverflowsViewportForTest());
    EXPECT_EQ(button.getButtonText(), "Expand all");
    EXPECT_EQ(button.getTooltip(), "Unfold every section");

    button.onClick();
    for (auto category : kSections)
        EXPECT_FALSE(tab.isSectionCollapsed(category));
    EXPECT_TRUE(findToggleByText(tab, "MIDI badges")->isVisible());
    EXPECT_EQ(button.getButtonText(), "Collapse all");
}

// With one section folded and the rest open the button still says "Collapse all"; folding the last
// open section by hand flips it, and unfolding one flips it back.
TEST_F(PreferencesSettingsTabTest, TheFoldAllLabelFollowsFoldsMadeByHand) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    pickAll(tab);
    auto& button = tab.getFoldAllButtonForTest();
    tab.setSectionCollapsed(Category::Graph, true);
    EXPECT_EQ(button.getButtonText(), "Collapse all");
    for (auto category : kSections)
        tab.setSectionCollapsed(category, true);
    EXPECT_EQ(button.getButtonText(), "Expand all");
    tab.setSectionCollapsed(Category::Mixer, false);
    EXPECT_EQ(button.getButtonText(), "Collapse all");
}

// The Preferences strip is the very class the Keyboard Shortcuts tab uses, pinned at the right edge of
// the rows area.
TEST_F(PreferencesSettingsTabTest, TheFoldAllButtonIsTheSharedClassPinnedAboveTheRows) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    pickAll(tab);
    auto& button = tab.getFoldAllButtonForTest();
    static_assert(std::is_same_v<std::remove_reference_t<decltype(button)>, synth::ui::FoldAllButton>);
    EXPECT_EQ(button.getHeight(), synth::ui::FoldAllButton::kStripHeight);
    EXPECT_LE(button.getBottom(), tab.getContentViewportForTest().getY());
    EXPECT_GT(button.getY(), tab.getCategoryComboForTest().getBottom() - 1);
    EXPECT_EQ(button.getX(), tab.getContentViewportForTest().getX());
    EXPECT_EQ(button.getWidth(), tab.getContentViewportForTest().getWidth()) << "spans the rows area";
}

TEST_F(PreferencesSettingsTabTest, ASingleCategoryHasNoHeadersOrFoldButtons) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    tab.setSelectedCategory(Category::Timeline);
    for (auto category : kSections)
        EXPECT_FALSE(tab.getSectionHeaderForTest(category).isVisible());
    EXPECT_FALSE(tab.getFoldAllButtonForTest().isVisible());
}

TEST_F(PreferencesSettingsTabTest, AFilterSeesThroughFoldsAndHidesTheHeaders) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    pickAll(tab);
    tab.setAllSectionsCollapsed(true);
    tab.setSearchFilterForTest("badges");
    EXPECT_TRUE(findToggleByText(tab, "MIDI badges")->isVisible());
    for (auto category : kSections)
        EXPECT_FALSE(tab.getSectionHeaderForTest(category).isVisible());
    EXPECT_FALSE(tab.getFoldAllButtonForTest().isVisible());
    tab.setSearchFilterForTest({});
    EXPECT_FALSE(findToggleByText(tab, "MIDI badges")->isVisible()) << "folds return once the filter clears";
    EXPECT_TRUE(tab.getSectionHeaderForTest(Category::MidiRemote).isVisible());
}

// Accessibility: headers take keyboard focus, are named with their state, and the buttons have tooltips.
TEST_F(PreferencesSettingsTabTest, SectionControlsAreKeyboardReachableAndNamedWithTheirState) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    pickAll(tab);
    auto& header = tab.getSectionHeaderForTest(Category::Mixer);
    EXPECT_TRUE(header.getWantsKeyboardFocus());
    EXPECT_EQ(header.getTitle(), "Mixer, expanded");
    EXPECT_FALSE(header.getTooltip().isEmpty());
    header.onClick();
    EXPECT_EQ(header.getTitle(), "Mixer, collapsed");
    tab.setAllSectionsCollapsed(false);
    EXPECT_EQ(header.getTitle(), "Mixer, expanded");
    EXPECT_FALSE(tab.getFoldAllButtonForTest().getTooltip().isEmpty());
    EXPECT_TRUE(tab.getFoldAllButtonForTest().getWantsKeyboardFocus());
}

// Keyboard: pressing Tab repeatedly from the filter field has to reach a control inside the rows
// before it wraps back to the drop-down, and Shift+Tab has to walk back out, in a single category and in
// the All view.
TEST_F(PreferencesSettingsTabTest, TabFromTheFilterFieldReachesTheRowsAndShiftTabComesBack) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    auto& filter = tab.getSearchFieldForTest();
    for (auto category : {Category::Graph, Category::All}) {
        tab.setSelectedCategory(category);
        auto traverser = tab.createFocusTraverser();
        juce::Component* current = &filter;
        juce::Component* firstRow = nullptr;
        for (int step = 0; step < 12 && firstRow == nullptr; ++step) {
            current = traverser->getNextComponent(current);
            ASSERT_NE(current, nullptr);
            if (current == &tab.getCategoryComboForTest())
                break; // wrapped without ever entering the rows
            if (dynamic_cast<juce::Button*>(current) != nullptr && tab.getContentHostForTest().isParentOf(current))
                firstRow = current;
        }
        ASSERT_NE(firstRow, nullptr) << "Tab never reached a preference row from the filter field";
        juce::Component* back = firstRow;
        bool reachedFilter = false;
        for (int step = 0; step < 12 && !reachedFilter; ++step) {
            back = traverser->getPreviousComponent(back);
            reachedFilter = back == &filter || (back != nullptr && filter.isParentOf(back));
        }
        EXPECT_TRUE(reachedFilter) << "Shift+Tab did not come back to the filter field";
    }
}
