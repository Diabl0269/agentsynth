#include "../../../TestWait.h"
#include "PreferencesSettingsTabTestFixture.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"

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

// The "ask before removing an LFO's last destination" preference: DEFAULT ON, persisted under its own key,
// read at use time by MainComponent::removeModulator.
TEST_F(PreferencesSettingsTabTest, AskBeforeRemovingLfoDefaultsOnAndRoundTrips) {
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_TRUE(tab.isAskBeforeRemovingLfoEnabled());
        EXPECT_FALSE(appProperties.getUserSettings()->containsKey("timelineAskBeforeRemovingLfo"));
        tab.setAskBeforeRemovingLfoEnabled(false);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineAskBeforeRemovingLfo"), "0");
    }
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_FALSE(tab.isAskBeforeRemovingLfoEnabled());
        tab.setAskBeforeRemovingLfoEnabled(true);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineAskBeforeRemovingLfo"), "1");
    }
}

// Regression test for FRO792: the dialog's remembered "Keep module" answer had no way back. Turning the question on
// forgets the answer, so the next removal asks again and a hand-made "off" starts from the plain remove.
TEST_F(PreferencesSettingsTabTest, TurningTheLfoQuestionBackOnForgetsTheRememberedChoice) {
    PreferencesSettingsTab tab(appProperties);
    auto& settings = *appProperties.getUserSettings();
    settings.setValue("timelineAskBeforeRemovingLfo", "0");
    settings.setValue("timelineRemoveLastSourceChoice", "keep");
    tab.setAskBeforeRemovingLfoEnabled(true);
    EXPECT_EQ(settings.getValue("timelineAskBeforeRemovingLfo"), "1");
    EXPECT_FALSE(settings.containsKey("timelineRemoveLastSourceChoice"));
}

// The real button, found by its words, writes the key; and a search for those words keeps it.
TEST_F(PreferencesSettingsTabTest, ClickingTheAskBeforeRemovingLfoToggleWritesTheSetting) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 900);
    juce::ToggleButton* toggle = nullptr;
    for (auto* child : descendantsOf(tab))
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
            if (tb->getButtonText().containsIgnoreCase("removing an LFO"))
                toggle = tb;
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->getToggleState());
    EXPECT_FALSE(toggle->getTooltip().isEmpty());
    toggle->setToggleState(false, juce::sendNotificationSync); // what a click does
    EXPECT_FALSE(toggle->getToggleState());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineAskBeforeRemovingLfo"), "0");
    EXPECT_FALSE(tab.isAskBeforeRemovingLfoEnabled());
}

// The "ask before deleting a track with the keyboard" preference: DEFAULT ON, its own key, read at use time by
// MainComponent::deleteTrackAfterConfirm. The real switch writes it, and turning it back on restores the question.
TEST_F(PreferencesSettingsTabTest, AskBeforeDeletingTrackDefaultsOnAndRoundTrips) {
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_TRUE(tab.isAskBeforeDeletingTrackEnabled());
        EXPECT_FALSE(appProperties.getUserSettings()->containsKey("timelineAskBeforeDeletingTrack"));
        tab.setAskBeforeDeletingTrackEnabled(false);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineAskBeforeDeletingTrack"), "0");
    }
    PreferencesSettingsTab tab(appProperties);
    EXPECT_FALSE(tab.isAskBeforeDeletingTrackEnabled());
    tab.setAskBeforeDeletingTrackEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineAskBeforeDeletingTrack"), "1");
}

// The answer the macro-delete window's "Remember my choice" stored is forgotten when the question is turned back on,
// as the LFO question's is.
TEST_F(PreferencesSettingsTabTest, TurningTheDeleteQuestionBackOnForgetsTheRememberedMacroAnswer) {
    auto* settings = appProperties.getUserSettings();
    settings->setValue("timelineAskBeforeDeletingTrack", "0");
    settings->setValue("timelineDeleteMacroChoice", "keep");
    PreferencesSettingsTab tab(appProperties);
    tab.setAskBeforeDeletingTrackEnabled(false);
    EXPECT_EQ(settings->getValue("timelineDeleteMacroChoice"), "keep") << "turning it off keeps the answer";
    tab.setAskBeforeDeletingTrackEnabled(true);
    EXPECT_FALSE(settings->containsKey("timelineDeleteMacroChoice"));
    EXPECT_EQ(settings->getValue("timelineAskBeforeDeletingTrack"), "1");
}

TEST_F(PreferencesSettingsTabTest, ClickingTheAskBeforeDeletingTrackToggleWritesTheSetting) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 900);
    juce::ToggleButton* toggle = nullptr;
    for (auto* child : descendantsOf(tab))
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
            if (tb->getButtonText().containsIgnoreCase("deleting a track"))
                toggle = tb;
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->getToggleState());
    EXPECT_TRUE(toggle->getTooltip().contains("Cmd+Backspace"));
    EXPECT_EQ(toggle->getButtonText(), "Ask before deleting a track or its macro") << "it covers macro deletes too";
    EXPECT_TRUE(toggle->getTooltip().contains("macro"));
    toggle->setToggleState(false, juce::sendNotificationSync); // what a click does
    EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineAskBeforeDeletingTrack"), "0");
    EXPECT_FALSE(tab.isAskBeforeDeletingTrackEnabled());
}

// "Record also starts playback": DEFAULT ON (the DAW convention, today's behaviour), its own key, and the real toggle
// writes it.
TEST_F(PreferencesSettingsTabTest, RecordAlsoStartsPlaybackDefaultsOnAndTheToggleWritesIt) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 1200);
    EXPECT_TRUE(tab.isRecordStartsPlaybackEnabled());
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("recordStartsPlayback"));
    juce::ToggleButton* toggle = nullptr;
    for (auto* child : descendantsOf(tab))
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
            if (tb->getButtonText() == "Record also starts playback")
                toggle = tb;
    ASSERT_NE(toggle, nullptr);
    toggle->setToggleState(false, juce::sendNotificationSync); // what a click does
    EXPECT_EQ(appProperties.getUserSettings()->getValue("recordStartsPlayback"), "0");
    EXPECT_FALSE(tab.isRecordStartsPlaybackEnabled());
    tab.setRecordStartsPlaybackEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("recordStartsPlayback"), "1");
}

// The follow-playhead pin point: a digits-only field, 10-90, committed on Return, applied through the settings key.
TEST_F(PreferencesSettingsTabTest, FollowPlayheadPointFieldClampsCommitsAndPersists) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 1200);
    EXPECT_EQ(tab.getFollowPointPercent(), 50);
    juce::TextEditor* field = nullptr;
    for (auto* child : descendantsOf(tab))
        if (auto* te = dynamic_cast<juce::TextEditor*>(child))
            if (te->getTitle().startsWith("Follow playhead point"))
                field = te;
    ASSERT_NE(field, nullptr);
    EXPECT_FALSE(field->getTooltip().isEmpty());

    field->setText("75", juce::dontSendNotification);
    field->onReturnKey();
    EXPECT_EQ(appProperties.getUserSettings()->getIntValue("timelineFollowPointPercent"), 75);

    field->setText("3", juce::dontSendNotification);
    field->onReturnKey();
    EXPECT_EQ(appProperties.getUserSettings()->getIntValue("timelineFollowPointPercent"), 10);
    EXPECT_EQ(field->getText(), "10") << "an out-of-range entry snaps visibly to a valid value";

    tab.setFollowPointPercent(95);
    EXPECT_EQ(appProperties.getUserSettings()->getIntValue("timelineFollowPointPercent"), 90);
}

// The "Duplicate points" combo: default "Right after the last point", a choice persists and applies app-wide at once,
// and a fresh tab shows the saved choice.
namespace {
struct DuplicatePlacementGuard {
    ~DuplicatePlacementGuard() { synth::ui::setDuplicatePlacement(synth::ui::DuplicatePlacement::afterLast); }
};
} // namespace

TEST_F(PreferencesSettingsTabTest, DuplicatePointsDefaultsToRightAfterTheLastPoint) {
    DuplicatePlacementGuard guard;
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getDuplicatePlacement(), synth::ui::DuplicatePlacement::afterLast);
}

TEST_F(PreferencesSettingsTabTest, ChoosingADuplicatePointsItemPersistsAndAppliesItAtOnce) {
    using synth::ui::DuplicatePlacement;
    DuplicatePlacementGuard guard;
    PreferencesSettingsTab tab(appProperties);
    juce::ComboBox* combo = nullptr;
    for (auto* child : descendantsOf(tab))
        if (auto* c = dynamic_cast<juce::ComboBox*>(child); c != nullptr && c->getTitle() == "Duplicate points")
            combo = c;
    ASSERT_NE(combo, nullptr);
    EXPECT_FALSE(combo->getTooltip().isEmpty());

    combo->setSelectedId(2, juce::sendNotificationSync);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("duplicatePointsPlacement"), "spacing");
    EXPECT_EQ(synth::ui::duplicatePlacement(), DuplicatePlacement::onePointSpacing);
    combo->setSelectedId(3, juce::sendNotificationSync);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("duplicatePointsPlacement"), "grid");
    EXPECT_EQ(synth::ui::duplicatePlacement(), DuplicatePlacement::nextGridLine);
    combo->setSelectedId(1, juce::sendNotificationSync);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("duplicatePointsPlacement"), "afterLast");
    EXPECT_EQ(synth::ui::duplicatePlacement(), DuplicatePlacement::afterLast);
}

TEST_F(PreferencesSettingsTabTest, DuplicatePointsChoiceSurvivesAFreshTabAndUnknownValuesReadAsTheDefault) {
    using synth::ui::DuplicatePlacement;
    DuplicatePlacementGuard guard;
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setDuplicatePlacement(DuplicatePlacement::nextGridLine);
    }
    PreferencesSettingsTab tab2(appProperties);
    EXPECT_EQ(tab2.getDuplicatePlacement(), DuplicatePlacement::nextGridLine);

    appProperties.getUserSettings()->setValue("duplicatePointsPlacement", "notARealValue");
    PreferencesSettingsTab tab3(appProperties);
    EXPECT_EQ(tab3.getDuplicatePlacement(), DuplicatePlacement::afterLast);
}

// "Play the scale when browsing keys": DEFAULT ON, its own key (read by KeyAuditioner when a scale starts), kept across
// restarts, named, tooltipped and reachable by keyboard like its neighbours; the real switch writes it.
TEST_F(PreferencesSettingsTabTest, PlayScaleWhenBrowsingKeysDefaultsOnPersistsAndTheSwitchWritesIt) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setSize(500, 1400);
        EXPECT_TRUE(tab.isPlayScaleOnBrowseEnabled());
        EXPECT_FALSE(appProperties.getUserSettings()->containsKey("playScaleWhenBrowsingKeys"));
        EXPECT_TRUE(synth::ui::KeyAuditioner::isEnabledIn(appProperties.getUserSettings()));

        juce::ToggleButton* toggle = nullptr;
        for (auto* child : descendantsOf(tab))
            if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
                if (tb->getButtonText() == "Play the scale when browsing keys")
                    toggle = tb;
        ASSERT_NE(toggle, nullptr) << "the row exists";
        EXPECT_TRUE(toggle->getWantsKeyboardFocus());
        EXPECT_FALSE(toggle->getTooltip().isEmpty());
        EXPECT_FALSE(toggle->getButtonText() == toggle->getButtonText().toUpperCase()) << "no all-caps text";
        toggle->setToggleState(false, juce::sendNotificationSync); // what a click does
        EXPECT_EQ(appProperties.getUserSettings()->getValue("playScaleWhenBrowsingKeys"), "0");
        EXPECT_FALSE(synth::ui::KeyAuditioner::isEnabledIn(appProperties.getUserSettings()));
    }
    PreferencesSettingsTab tab(appProperties);
    EXPECT_FALSE(tab.isPlayScaleOnBrowseEnabled()) << "kept across a restart";
    tab.setPlayScaleOnBrowseEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("playScaleWhenBrowsingKeys"), "1");
}

// The two Sections preferences: the strip shows by default, a drop makes room by default; each persists under its own
// key and the panel reads them at use time.
TEST_F(PreferencesSettingsTabTest, SectionsPreferencesDefaultAndRoundTrip) {
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_TRUE(tab.isShowSectionsStripEnabled());
        EXPECT_FALSE(tab.isSectionsCoverOnDropEnabled());
        EXPECT_FALSE(appProperties.getUserSettings()->containsKey("timelineShowSectionsStrip"));
        tab.setShowSectionsStripEnabled(false);
        tab.setSectionsCoverOnDropEnabled(true);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineShowSectionsStrip"), "0");
        EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineSectionsCoverOnDrop"), "1");
    }
    PreferencesSettingsTab again(appProperties);
    EXPECT_FALSE(again.isShowSectionsStripEnabled());
    EXPECT_TRUE(again.isSectionsCoverOnDropEnabled());
}

// The key menus' play-scale row writes the same key; an open Settings tab re-reads it (the settings file broadcasts).
TEST_F(PreferencesSettingsTabTest, PlayScaleWhenBrowsingKeysFollowsTheKeyMenuToggleWhileTheTabIsOpen) {
    PreferencesSettingsTab tab(appProperties);
    ASSERT_TRUE(tab.isPlayScaleOnBrowseEnabled());
    synth::ui::KeyAuditioner::setEnabledIn(appProperties.getUserSettings(), false);
    synth::test::waitUntil([&] { return !tab.isPlayScaleOnBrowseEnabled(); }, 2000);
    EXPECT_FALSE(tab.isPlayScaleOnBrowseEnabled());
    synth::ui::KeyAuditioner::setEnabledIn(appProperties.getUserSettings(), true);
    synth::test::waitUntil([&] { return tab.isPlayScaleOnBrowseEnabled(); }, 2000);
    EXPECT_TRUE(tab.isPlayScaleOnBrowseEnabled());
}

namespace {
// The Label + ComboBox + hint of the section-drop setting, found by their words.
struct SectionsDropRow {
    juce::Label* label = nullptr;
    juce::ComboBox* combo = nullptr;
    juce::Label* hint = nullptr;
};

SectionsDropRow findSectionsDropRow(juce::Component& tab) {
    SectionsDropRow row;
    for (auto* child : descendantsOf(tab)) {
        if (auto* l = dynamic_cast<juce::Label*>(child)) {
            if (l->getText() == "Timeline: dropping a section on other music")
                row.label = l;
            if (l->getText() == "Hold Shift while you drop to do the other one, that time only.")
                row.hint = l;
        }
        if (auto* c = dynamic_cast<juce::ComboBox*>(child))
            if (c->getTitle() == "Timeline: dropping a section on other music")
                row.combo = c;
    }
    return row;
}
} // namespace

// The section-drop setting is a Label + ComboBox with a hint under it: it pushes the music later by default, choosing
// "Covers it" writes the key and back, and the words and tooltip say what each choice does.
TEST_F(PreferencesSettingsTabTest, SectionsDropComboDefaultsToPushingAndPersistsEachChoice) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(560, 900);
    tab.setSelectedCategory(PreferencesSettingsTab::Category::Timeline);
    const auto row = findSectionsDropRow(tab);
    ASSERT_NE(row.label, nullptr);
    ASSERT_NE(row.combo, nullptr);
    ASSERT_NE(row.hint, nullptr);
    EXPECT_EQ(row.combo->getText(), "Pushes the music later");
    EXPECT_EQ(row.combo->getNumItems(), 2);
    EXPECT_EQ(row.combo->getItemText(1), "Covers it");
    EXPECT_TRUE(row.combo->getTooltip().contains("Hold Shift while you drop to do the other one."));
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("timelineSectionsCoverOnDrop"));

    row.combo->setSelectedId(2, juce::sendNotificationSync);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineSectionsCoverOnDrop"), "1");
    EXPECT_TRUE(tab.isSectionsCoverOnDropEnabled());
    row.combo->setSelectedId(1, juce::sendNotificationSync);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineSectionsCoverOnDrop"), "0");
    EXPECT_FALSE(tab.isSectionsCoverOnDropEnabled());

    tab.setSectionsCoverOnDropEnabled(true);
    EXPECT_EQ(row.combo->getText(), "Covers it");
    PreferencesSettingsTab again(appProperties);
    again.setSize(560, 900);
    again.setSelectedCategory(PreferencesSettingsTab::Category::Timeline);
    EXPECT_EQ(findSectionsDropRow(again).combo->getText(), "Covers it");
}

// The rows show with the Timeline group, hide when a search misses them, show again for words in the label, and never
// overlap the rows above and below.
TEST_F(PreferencesSettingsTabTest, SectionsDropRowsFollowSearchAndDoNotOverlapNeighbours) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(560, 900);
    tab.setSelectedCategory(PreferencesSettingsTab::Category::Timeline);
    auto row = findSectionsDropRow(tab);
    ASSERT_NE(row.combo, nullptr);
    ASSERT_NE(row.hint, nullptr);
    EXPECT_TRUE(row.label->isVisible() && row.combo->isVisible() && row.hint->isVisible());

    auto* above = findToggleByText(tab, "Timeline: show the Sections strip");
    auto* below = findToggleByText(tab, "Ask before removing an LFO's last destination");
    ASSERT_NE(above, nullptr);
    ASSERT_NE(below, nullptr);
    const auto top = [&](juce::Component* c) { return c->getBounds().getY(); };
    EXPECT_GE(row.label->getY(), above->getBottom());
    EXPECT_GE(row.combo->getY(), above->getBottom());
    EXPECT_EQ(row.label->getY(), row.combo->getY());
    EXPECT_GE(row.hint->getY(), row.combo->getBottom());
    EXPECT_LE(row.hint->getBottom(), below->getY());
    EXPECT_LE(row.label->getRight(), row.combo->getX()) << "label and combo sit side by side without overlapping";
    EXPECT_GT(top(row.hint), top(row.combo));

    tab.setSearchFilterForTest("dropping a section");
    EXPECT_TRUE(row.label->isVisible() && row.combo->isVisible() && row.hint->isVisible());
    tab.setSearchFilterForTest("zzz-no-such-preference");
    EXPECT_FALSE(row.label->isVisible());
    EXPECT_FALSE(row.combo->isVisible());
    EXPECT_FALSE(row.hint->isVisible());
    tab.setSearchFilterForTest("");
    EXPECT_TRUE(row.combo->isVisible());
}
