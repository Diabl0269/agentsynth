#include "PreferencesSettingsTabTestFixture.h"

// Topic: the "All" category view: every category under a collapsible header, fold state, the
// Expand all / Collapse all buttons, and that a filter ignores folds.

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
    EXPECT_TRUE(tab.getExpandAllButtonForTest().isVisible());
    EXPECT_TRUE(tab.getCollapseAllButtonForTest().isVisible());
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

TEST_F(PreferencesSettingsTabTest, ExpandAllAndCollapseAllActOnEverySection) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    pickAll(tab);
    tab.getCollapseAllButtonForTest().onClick();
    for (auto category : kSections)
        EXPECT_TRUE(tab.isSectionCollapsed(category));
    EXPECT_FALSE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "MIDI badges")->isVisible());
    EXPECT_FALSE(tab.contentOverflowsViewportForTest());

    tab.getExpandAllButtonForTest().onClick();
    for (auto category : kSections)
        EXPECT_FALSE(tab.isSectionCollapsed(category));
    EXPECT_TRUE(findToggleByText(tab, "MIDI badges")->isVisible());
}

TEST_F(PreferencesSettingsTabTest, ASingleCategoryHasNoHeadersOrFoldButtons) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 900);
    tab.setSelectedCategory(Category::Timeline);
    for (auto category : kSections)
        EXPECT_FALSE(tab.getSectionHeaderForTest(category).isVisible());
    EXPECT_FALSE(tab.getExpandAllButtonForTest().isVisible());
    EXPECT_FALSE(tab.getCollapseAllButtonForTest().isVisible());
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
    EXPECT_FALSE(tab.getExpandAllButtonForTest().isVisible());
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
    EXPECT_FALSE(tab.getExpandAllButtonForTest().getTooltip().isEmpty());
    EXPECT_FALSE(tab.getCollapseAllButtonForTest().getTooltip().isEmpty());
    EXPECT_TRUE(tab.getExpandAllButtonForTest().getWantsKeyboardFocus());
}
