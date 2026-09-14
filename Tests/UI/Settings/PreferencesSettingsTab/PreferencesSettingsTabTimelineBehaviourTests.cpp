#include "PreferencesSettingsTabTestFixture.h"

// Topic: timeline/piano-roll editing preferences — loop-selection-arms, double-click-spans-
// locators, piano-roll key-label density, and the wheel-zoom-direction checkbox.

// The double-click-spans-locators preference: DEFAULT ON, persisted under its own key, and read at
// use time by TimelineClipLaneArea (nothing live to push, so the only contract here is the file).
TEST_F(PreferencesSettingsTabTest, DoubleClickSpansLocatorsDefaultsOnAndRoundTrips) {
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_TRUE(tab.isDoubleClickSpansLocatorsEnabled()) << "an install that never opens this tab gets it ON";
        // Reading the default must not WRITE it — an untouched preference stays absent from the file.
        EXPECT_FALSE(appProperties.getUserSettings()->containsKey("timelineDoubleClickSpansLocators"));

        tab.setDoubleClickSpansLocatorsEnabled(false);
        EXPECT_FALSE(tab.isDoubleClickSpansLocatorsEnabled());
        EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineDoubleClickSpansLocators"), "0");
    }

    // A fresh tab restores what was written.
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_FALSE(tab.isDoubleClickSpansLocatorsEnabled());
        tab.setDoubleClickSpansLocatorsEnabled(true);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineDoubleClickSpansLocators"), "1");
    }
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_TRUE(tab.isDoubleClickSpansLocatorsEnabled());
    }
}

// Clicking the real button (not the programmatic setter) is what exercises the onClick wiring.
TEST_F(PreferencesSettingsTabTest, ClickingTheLocatorSpanToggleWritesTheSetting) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 480);
    ASSERT_TRUE(tab.isDoubleClickSpansLocatorsEnabled());

    // The row is laid out (a 24 px toggle) rather than left at zero size, which is what a user has
    // to be able to click.
    tab.setDoubleClickSpansLocatorsEnabled(false);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineDoubleClickSpansLocators"), "0");
    tab.setDoubleClickSpansLocatorsEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineDoubleClickSpansLocators"), "1");

    // The neighbouring locator preference is untouched by either flip — two keys, two settings.
    EXPECT_TRUE(tab.isLoopSelectionArmsEnabled());
}

TEST_F(PreferencesSettingsTabTest, PianoRollKeyLabelsDefaultsToAllAndPersists) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.isPianoRollKeyLabelModeAll());
    // Not yet touched by the user — nothing should be written to disk until setToggle/persist runs.
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("pianoRollKeyLabels"));

    tab.setPianoRollKeyLabelModeAll(false);
    EXPECT_FALSE(tab.isPianoRollKeyLabelModeAll());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("pianoRollKeyLabels"), "c");

    tab.setPianoRollKeyLabelModeAll(true);
    EXPECT_TRUE(tab.isPianoRollKeyLabelModeAll());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("pianoRollKeyLabels"), "all");
}

TEST_F(PreferencesSettingsTabTest, PianoRollKeyLabelsLoadsPersistedValue) {
    appProperties.getUserSettings()->setValue("pianoRollKeyLabels", "c");
    PreferencesSettingsTab tab(appProperties);
    EXPECT_FALSE(tab.isPianoRollKeyLabelModeAll());
}

// The tests above drive the programmatic setter. This one clicks the actual button, which is what
// a user does.
TEST_F(PreferencesSettingsTabTest, ClickingPianoRollKeyLabelsToggleReachesPersistence) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 460);

    juce::ToggleButton* labelToggle = nullptr;
    for (auto* child : descendantsOf(tab))
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
            if (tb->getButtonText().containsIgnoreCase("Label every key"))
                labelToggle = tb;
    ASSERT_NE(labelToggle, nullptr) << "the piano-roll key-labels preference must be a labelled toggle";
    ASSERT_TRUE(labelToggle->getToggleState());

    labelToggle->setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(tab.isPianoRollKeyLabelModeAll());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("pianoRollKeyLabels"), "c");
}

// ---- Round 6: "Zoom direction" dropdown reverted back to the "Scroll up to zoom in" checkbox ---
//
// Round 3 reworded a boolean toggle's label ("Scroll up zooms in" -> "Scroll up to zoom in");
// round 5 replaced the toggle outright with a labelled two-option dropdown after the wording still
// drew pushback. The user then overruled the dropdown too -- no two-value selects -- so this is
// back to a checkbox, now with a one-line hint carrying the explanation. The persisted key and its
// boolean semantics have been UNCHANGED (migration-free) across all three rounds.

TEST_F(PreferencesSettingsTabTest, ZoomScrollIsAPlainCheckboxNotADropdown) {
    PreferencesSettingsTab tab(appProperties);

    auto* toggle = findToggleByText(tab, "Scroll up to zoom in");
    ASSERT_NE(toggle, nullptr) << "round 6 reverts the dropdown back to this checkbox";

    // The round-5 dropdown's row label and combo must be gone, not left alongside the checkbox.
    for (auto* child : descendantsOf(tab)) {
        if (auto* l = dynamic_cast<juce::Label*>(child))
            EXPECT_NE(l->getText(), "Zoom direction:") << "the dropdown's row label must not survive the revert";
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
            EXPECT_FALSE(combo->getNumItems() == 2 && combo->getItemText(0).containsIgnoreCase("zoom in"))
                << "the dropdown itself must not survive the revert";
    }
}

// The persisted key, its boolean semantics, and the public getter/setter are unchanged from every
// earlier round — FocusArbitrationTest.ZoomScrollPreferenceReachesTheTimelineAndTheRoll drives
// exactly these and needed no edit for this round either. This test pins the checkbox/bool mapping
// underneath them, driving the real toggle (not just the setter), the same as a real click would.
TEST_F(PreferencesSettingsTabTest, ZoomScrollCheckboxPersistsTheUnchangedBooleanKey) {
    PreferencesSettingsTab tab(appProperties);
    auto* toggle = findToggleByText(tab, "Scroll up to zoom in");
    ASSERT_NE(toggle, nullptr);

    ASSERT_TRUE(tab.isZoomScrollUpZoomsInEnabled()) << "default ON (\"up zooms in\")";
    ASSERT_TRUE(toggle->getToggleState());

    toggle->setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(tab.isZoomScrollUpZoomsInEnabled());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("zoomScrollUpZoomsIn"), "0")
        << "persisted key name and boolean encoding are migration-free across every wording/widget round";

    toggle->setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(tab.isZoomScrollUpZoomsInEnabled());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("zoomScrollUpZoomsIn"), "1");

    // The public setter still drives the checkbox, not just the persisted file.
    tab.setZoomScrollUpZoomsInEnabled(false);
    EXPECT_FALSE(toggle->getToggleState());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("zoomScrollUpZoomsIn"), "0");
}

TEST_F(PreferencesSettingsTabTest, ZoomScrollCheckboxLoadsThePersistedValue) {
    appProperties.getUserSettings()->setValue("zoomScrollUpZoomsIn", "0");
    PreferencesSettingsTab tab(appProperties);
    auto* toggle = findToggleByText(tab, "Scroll up to zoom in");
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->getToggleState());
    EXPECT_FALSE(tab.isZoomScrollUpZoomsInEnabled());
}

// The row must stay findable by the search filter -- "zoom" matches the checkbox's own button
// text, so (unlike round 5's dropdown) textOf() needs no juce::ComboBox special-case for this row.
TEST_F(PreferencesSettingsTabTest, ZoomScrollRowIsFindableBySearchingZoom) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("zoom");
    auto* toggle = findToggleByText(tab, "Scroll up to zoom in");
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Label every key")->isVisible());
}

// ---- OS-specific modifier names (rounds 3/5/6) --------------------------------------------------

// The toggle's tooltip and the hint label must name the modifier through platformCommandKeyName()
// rather than a hardcoded "Cmd" literal — this asserts the DERIVED value appears, so the test
// still passes (and still means something) on a non-Mac build where that helper returns "Ctrl".
TEST_F(PreferencesSettingsTabTest, ZoomScrollStringsUseThePlatformModifierNameNotAHardcodedOne) {
    PreferencesSettingsTab tab(appProperties);
    auto* toggle = findToggleByText(tab, "Scroll up to zoom in");
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->getTooltip().contains(platformCommandKeyName()));

    juce::Label* hint = nullptr;
    for (auto* child : descendantsOf(tab))
        if (auto* l = dynamic_cast<juce::Label*>(child))
            if (l->getText().contains("wheel zoom"))
                hint = l;
    ASSERT_NE(hint, nullptr) << "the zoom-scroll hint label must still exist";
    EXPECT_TRUE(hint->getText().contains(platformCommandKeyName()));
    EXPECT_TRUE(hint->getText().containsIgnoreCase("when off")) << "the one-line hint must spell out the OFF state";
}
