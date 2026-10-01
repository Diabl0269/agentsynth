#include "PreferencesSettingsTabTestFixture.h"

// Topic: the category picker (one panel of rows at a time), the scrolling viewport that makes every
// row of a category reachable, and the guarantee that the restructure changed no persisted value.

namespace {
using Category = PreferencesSettingsTab::Category;

juce::Label* findLabelByText(PreferencesSettingsTab& tab, const juce::String& text) {
    for (auto* child : descendantsOf(tab))
        if (auto* l = dynamic_cast<juce::Label*>(child))
            if (l->getText().containsIgnoreCase(text))
                return l;
    return nullptr;
}

// One probe row per category: a control that lives in that category and nowhere else.
struct CategoryProbe {
    Category category;
    juce::Component* (*find)(PreferencesSettingsTab&);
};

juce::Component* graphProbe(PreferencesSettingsTab& t) { return findToggleByText(t, "Show Alignment Guides"); }
juce::Component* timelineProbe(PreferencesSettingsTab& t) { return findToggleByText(t, "Label every key"); }
juce::Component* filesProbe(PreferencesSettingsTab& t) { return findToggleByText(t, "Autosave"); }
juce::Component* mixerProbe(PreferencesSettingsTab& t) { return findLabelByText(t, "Mixer placement"); }
juce::Component* panelsProbe(PreferencesSettingsTab& t) { return findLabelByText(t, "When a panel opens"); }
juce::Component* midiProbe(PreferencesSettingsTab& t) { return findToggleByText(t, "MIDI badges"); }

const CategoryProbe kProbes[] = {{Category::Graph, graphProbe},   {Category::Timeline, timelineProbe},
                                 {Category::Files, filesProbe},   {Category::Mixer, mixerProbe},
                                 {Category::Panels, panelsProbe}, {Category::MidiRemote, midiProbe}};
} // namespace

TEST_F(PreferencesSettingsTabTest, PickerListsEveryCategoryAndOpensOnTheRememberedOne) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);
    auto& combo = tab.getCategoryComboForTest();
    EXPECT_EQ(combo.getNumItems(), 7); // the six categories plus All
    EXPECT_EQ(tab.getSelectedCategory(), Category::Graph) << "the fixture remembers Graph";
    EXPECT_EQ(combo.getText(), PreferencesSettingsTab::categoryName(Category::Graph));
    for (const auto& probe : kProbes)
        EXPECT_TRUE(combo.indexOfItemId(static_cast<int>(probe.category) + 1) >= 0);
}

// The picked category is saved under its stable name and the next tab (the next time Settings opens)
// starts on it, whichever category it was.
TEST_F(PreferencesSettingsTabTest, SelectedCategoryIsRememberedByNameAcrossTabs) {
    const struct {
        Category category;
        const char* name;
    } names[] = {{Category::All, "All"},
                 {Category::Graph, "Graph"},
                 {Category::Timeline, "Timeline"},
                 {Category::Files, "Files"},
                 {Category::Mixer, "Mixer"},
                 {Category::Panels, "Panels"},
                 {Category::MidiRemote, "MidiRemote"}};
    for (const auto& entry : names) {
        {
            PreferencesSettingsTab tab(appProperties);
            tab.setSize(500, 700);
            tab.setSelectedCategory(entry.category);
        }
        EXPECT_EQ(appProperties.getUserSettings()->getValue("preferencesCategory"), juce::String(entry.name));
        PreferencesSettingsTab reopened(appProperties);
        reopened.setSize(500, 700);
        EXPECT_EQ(reopened.getSelectedCategory(), entry.category) << entry.name;
        EXPECT_EQ(reopened.getCategoryComboForTest().getSelectedId(), static_cast<int>(entry.category) + 1);
    }
}

// Nothing saved, or a value that names no category, opens on All.
TEST_F(PreferencesSettingsTabTest, MissingOrUnknownRememberedCategoryOpensOnAll) {
    appProperties.getUserSettings()->removeValue("preferencesCategory");
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_EQ(tab.getSelectedCategory(), Category::All);
    }
    for (const juce::String garbage : {"", "Nonsense", "3", "all", "Files & Autosave"}) {
        appProperties.getUserSettings()->setValue("preferencesCategory", garbage);
        PreferencesSettingsTab tab(appProperties);
        EXPECT_EQ(tab.getSelectedCategory(), Category::All) << "saved value \"" << garbage << "\"";
    }
}

// The remembered category is the first one laid out: its rows are on screen straight after
// construction, without the combo having been touched.
TEST_F(PreferencesSettingsTabTest, RememberedCategoryIsLaidOutBeforeTheFirstPaint) {
    appProperties.getUserSettings()->setValue("preferencesCategory", "Timeline");
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 900);
    EXPECT_TRUE(findToggleByText(tab, "Label every key")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
}

// Each category shows only its own panel: selecting it through the real combo's change path shows
// its probe row and hides every other category's.
TEST_F(PreferencesSettingsTabTest, SelectingACategoryShowsOnlyThatCategorysRows) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 900);
    auto& combo = tab.getCategoryComboForTest();

    for (const auto& selected : kProbes) {
        combo.setSelectedId(static_cast<int>(selected.category) + 1, juce::sendNotificationSync);
        EXPECT_EQ(tab.getSelectedCategory(), selected.category);
        for (const auto& probe : kProbes) {
            auto* row = probe.find(tab);
            ASSERT_NE(row, nullptr) << PreferencesSettingsTab::categoryName(probe.category);
            EXPECT_EQ(row->isVisible(), probe.category == selected.category)
                << PreferencesSettingsTab::categoryName(probe.category) << " while "
                << PreferencesSettingsTab::categoryName(selected.category) << " is selected";
        }
    }
}

// The restructure is layout-only: every preference is still constructed, and a value set in any
// category persists and loads back in a fresh tab (which is what closing and reopening Settings does).
TEST_F(PreferencesSettingsTabTest, EveryCategoryRoundTripsItsPersistedValuesAcrossReopen) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setSize(500, 700);
        tab.setSmartConnectionMode(GraphEditor::SmartConnectionMode::Off);
        tab.setAlignmentGuidesEnabled(false);
        tab.setLoopSelectionArmsEnabled(false);
        tab.setNaturalScrollingEnabled(false);
        tab.setPianoRollKeyLabelModeAll(false);
        tab.setAutosaveIntervalMinutes(47);
        tab.setMixerPlacement("window");
        tab.setPanelDetachMode("both");
        tab.setMidiRemoteShowBadgesEnabled(false);
        // The last category's control, changed through its own visible control after switching to it.
        tab.setSelectedCategory(Category::MidiRemote);
        auto* badges = findToggleByText(tab, "MIDI badges");
        ASSERT_NE(badges, nullptr);
        EXPECT_FALSE(badges->getToggleState());
    }
    PreferencesSettingsTab reopened(appProperties);
    reopened.setSize(500, 700);
    EXPECT_EQ(reopened.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::Off);
    EXPECT_FALSE(reopened.isAlignmentGuidesEnabled());
    EXPECT_FALSE(reopened.isLoopSelectionArmsEnabled());
    EXPECT_FALSE(reopened.isNaturalScrollingEnabled());
    EXPECT_FALSE(reopened.isPianoRollKeyLabelModeAll());
    EXPECT_EQ(reopened.getAutosaveIntervalMinutes(), 47);
    EXPECT_EQ(reopened.getMixerPlacement(), "window");
    EXPECT_EQ(reopened.getPanelDetachMode(), "both");
    EXPECT_FALSE(reopened.isMidiRemoteShowBadgesEnabled());
}

// A short window: the Graph category's last row starts below the visible area, and driving the real
// vertical scrollbar brings it inside the viewport. Before this reachability only depended on the
// window being tall enough.
TEST_F(PreferencesSettingsTabTest, ScrollingReachesARowThatStartedOffScreen) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(600, 260);
    auto& viewport = tab.getContentViewportForTest();
    ASSERT_TRUE(tab.contentOverflowsViewportForTest());

    auto* lastRow = findToggleByText(tab, "without Cmd");
    ASSERT_NE(lastRow, nullptr);
    ASSERT_TRUE(lastRow->isVisible());
    const auto inViewport = [&] {
        return viewport.getLocalBounds().contains(viewport.getLocalArea(lastRow, lastRow->getLocalBounds()));
    };
    EXPECT_FALSE(inViewport()) << "premise: the last Graph row is below the fold";

    auto& bar = viewport.getVerticalScrollBar();
    bar.setCurrentRangeStart(bar.getMaximumRangeLimit(), juce::sendNotificationSync);
    EXPECT_GT(viewport.getViewPositionY(), 0);
    EXPECT_TRUE(inViewport()) << "scrolled to the bottom, the last row is reachable";
}

// Every category, not just Graph, can be scrolled until the bottom of its content is in view (or
// fits without scrolling), so no row of any category is cut off.
TEST_F(PreferencesSettingsTabTest, EveryCategoryFitsOrScrollsToItsBottom) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(600, 200);
    auto& viewport = tab.getContentViewportForTest();
    for (const auto& probe : kProbes) {
        tab.setSelectedCategory(probe.category);
        auto& bar = viewport.getVerticalScrollBar();
        bar.setCurrentRangeStart(bar.getMaximumRangeLimit(), juce::sendNotificationSync);
        auto* content = viewport.getViewedComponent();
        EXPECT_GE(viewport.getViewPositionY() + viewport.getHeight(), content->getHeight())
            << PreferencesSettingsTab::categoryName(probe.category);
    }
}

TEST_F(PreferencesSettingsTabTest, SwitchingCategoryScrollsBackToTheTop) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(600, 200);
    auto& viewport = tab.getContentViewportForTest();
    auto& bar = viewport.getVerticalScrollBar();
    bar.setCurrentRangeStart(bar.getMaximumRangeLimit(), juce::sendNotificationSync);
    ASSERT_GT(viewport.getViewPositionY(), 0);

    tab.getCategoryComboForTest().setSelectedId(static_cast<int>(Category::Timeline) + 1, juce::sendNotificationSync);
    EXPECT_EQ(viewport.getViewPositionY(), 0);
}

// A search query looks across every category and parks the picker until it is cleared.
TEST_F(PreferencesSettingsTabTest, SearchCrossesCategoriesAndDisablesThePickerUntilCleared) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 900);
    ASSERT_EQ(tab.getSelectedCategory(), Category::Graph);

    tab.setSearchFilterForTest("midi badges");
    EXPECT_TRUE(findToggleByText(tab, "MIDI badges")->isVisible()) << "found although Graph is selected";
    EXPECT_FALSE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
    EXPECT_FALSE(tab.getCategoryComboForTest().isEnabled());

    tab.setSearchFilterForTest("");
    EXPECT_TRUE(tab.getCategoryComboForTest().isEnabled());
    EXPECT_FALSE(findToggleByText(tab, "MIDI badges")->isVisible());
    EXPECT_TRUE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
}
