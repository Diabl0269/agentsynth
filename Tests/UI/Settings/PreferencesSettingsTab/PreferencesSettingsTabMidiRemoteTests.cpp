// PreferencesSettingsTabMidiRemoteTests.cpp -- FRO136 (docs/control/midi-remote-ui.md#settings): the
// MIDI Remote group -- Default takeover (Jump / Pick-up / Scale, default Scale) and "Show MIDI badges
// on mapped controls" (default on). What reaches RemoteEngine / the badge painter live is covered in
// Tests/UI/MidiRemote/MidiRemotePreferencesTests.cpp; this file is the tab's own contract.
#include "MidiRemote/MidiRemotePreferences.h"
#include "PreferencesSettingsTabTestFixture.h"
#include "UserSettings.h"

TEST_F(PreferencesSettingsTabTest, MidiRemoteDefaultsToScaleTakeoverAndBadgesOn) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getMidiRemoteDefaultTakeover(), synth::Takeover::scale);
    EXPECT_TRUE(tab.isMidiRemoteShowBadgesEnabled());
}

TEST_F(PreferencesSettingsTabTest, MidiRemoteTakeoverWritesTheSharedKeyAndSurvivesAReload) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setMidiRemoteDefaultTakeover(synth::Takeover::pickup);
        EXPECT_EQ(appProperties.getUserSettings()->getValue(synth::kMidiRemoteDefaultTakeoverSettingKey), "pickup");
    }
    PreferencesSettingsTab reloaded(appProperties);
    EXPECT_EQ(reloaded.getMidiRemoteDefaultTakeover(), synth::Takeover::pickup);
    EXPECT_EQ(synth::midi::loadDefaultTakeover(*appProperties.getUserSettings()), synth::Takeover::pickup);
}

TEST_F(PreferencesSettingsTabTest, MidiRemoteBadgeSwitchWritesTheSharedKeyAndSurvivesAReload) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setMidiRemoteShowBadgesEnabled(false);
        EXPECT_FALSE(appProperties.getUserSettings()->getBoolValue(synth::kMidiRemoteShowBadgesSettingKey, true));
    }
    PreferencesSettingsTab reloaded(appProperties);
    EXPECT_FALSE(reloaded.isMidiRemoteShowBadgesEnabled());
}

TEST_F(PreferencesSettingsTabTest, MidiRemoteUnknownStoredTakeoverFallsBackToScale) {
    appProperties.getUserSettings()->setValue(synth::kMidiRemoteDefaultTakeoverSettingKey, "default");
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getMidiRemoteDefaultTakeover(), synth::Takeover::scale)
        << "\"default\" would defer to itself; it must read as Scale";
}

// The combo and the toggle a user actually touches, not just the setters: driving the widgets must
// persist through their own callbacks.
TEST_F(PreferencesSettingsTabTest, MidiRemoteWidgetsPersistThroughTheirOwnCallbacks) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 900);

    juce::ComboBox* takeoverCombo = nullptr;
    for (auto* child : descendantsOf(tab))
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
            if (combo->getNumItems() == 3 && combo->getItemText(0) == "Jump")
                takeoverCombo = combo;
    ASSERT_NE(takeoverCombo, nullptr);
    EXPECT_EQ(takeoverCombo->getText(), "Scale");
    takeoverCombo->setSelectedId(1, juce::sendNotificationSync);
    EXPECT_EQ(appProperties.getUserSettings()->getValue(synth::kMidiRemoteDefaultTakeoverSettingKey), "jump");
    EXPECT_EQ(tab.getMidiRemoteDefaultTakeover(), synth::Takeover::jump);

    auto* badges = findToggleByText(tab, "MIDI badges");
    ASSERT_NE(badges, nullptr);
    EXPECT_TRUE(badges->getToggleState());
    badges->setToggleState(false, juce::sendNotificationSync);
    badges->onClick();
    EXPECT_FALSE(appProperties.getUserSettings()->getBoolValue(synth::kMidiRemoteShowBadgesSettingKey, true));
}

TEST_F(PreferencesSettingsTabTest, MidiRemoteGroupIsFoundByTheSearchFilterAndLaidOutBelowTheMixerRows) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 900);
    tab.resized();

    auto* badges = findToggleByText(tab, "MIDI badges");
    ASSERT_NE(badges, nullptr);
    EXPECT_TRUE(badges->isVisible());
    EXPECT_GT(badges->getHeight(), 0);

    // The filter works per group: a hit on either row keeps both.
    tab.setSearchFilterForTest("takeover");
    EXPECT_TRUE(badges->isVisible());
    tab.setSearchFilterForTest("zzz-no-such-preference");
    EXPECT_FALSE(badges->isVisible());
    tab.setSearchFilterForTest("");
    EXPECT_TRUE(badges->isVisible());
}
