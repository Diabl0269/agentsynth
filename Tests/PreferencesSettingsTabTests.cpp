#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/PreferencesSettingsTab.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

class PreferencesSettingsTabTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "PreferencesTabTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
        if (auto* s = appProperties.getUserSettings())
            s->clear();
    }

    void TearDown() override {
        if (auto* s = appProperties.getUserSettings())
            s->clear();
    }

    juce::ApplicationProperties appProperties;
};

TEST_F(PreferencesSettingsTabTest, DefaultsToNewAndUnwiredAndDoubleClickOn) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::NewAndUnwired);
    EXPECT_TRUE(tab.isDoubleClickPortDisconnectEnabled());
    EXPECT_FALSE(tab.getDefaultDualIOForNewModules());
}

TEST_F(PreferencesSettingsTabTest, LoadsPersistedValues) {
    appProperties.getUserSettings()->setValue("smartConnectionMode", "Off");
    appProperties.getUserSettings()->setValue("doubleClickPortDisconnect", "0");
    appProperties.getUserSettings()->setValue("defaultDualIOForNewModules", "1");

    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::Off);
    EXPECT_FALSE(tab.isDoubleClickPortDisconnectEnabled());
    EXPECT_TRUE(tab.getDefaultDualIOForNewModules());
}

TEST_F(PreferencesSettingsTabTest, ChangingControlsPersistsAndPushesToEditor) {
    PreferencesSettingsTab tab(appProperties);
    AudioEngine engine;
    GraphEditor editor(engine);
    tab.setGraphEditor(&editor);

    tab.setSmartConnectionMode(GraphEditor::SmartConnectionMode::AllMoves);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("smartConnectionMode"), "AllMoves");
    EXPECT_EQ(editor.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::AllMoves);

    tab.setDoubleClickPortDisconnectEnabled(false);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("doubleClickPortDisconnect"), "0");
    EXPECT_FALSE(editor.getDoubleClickPortDisconnectEnabled());

    tab.setDoubleClickPortDisconnectEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("doubleClickPortDisconnect"), "1");
    EXPECT_TRUE(editor.getDoubleClickPortDisconnectEnabled());

    tab.setDefaultDualIOForNewModules(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("defaultDualIOForNewModules"), "1");
    EXPECT_TRUE(editor.getDefaultDualIOForNewModules());

    tab.setDefaultDualIOForNewModules(false);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("defaultDualIOForNewModules"), "0");
    EXPECT_FALSE(editor.getDefaultDualIOForNewModules());
}

TEST_F(PreferencesSettingsTabTest, SetGraphEditorPushesCurrentValues) {
    appProperties.getUserSettings()->setValue("smartConnectionMode", "NewOnly");
    appProperties.getUserSettings()->setValue("doubleClickPortDisconnect", "0");
    appProperties.getUserSettings()->setValue("defaultDualIOForNewModules", "1");

    PreferencesSettingsTab tab(appProperties);
    AudioEngine engine;
    GraphEditor editor(engine);
    ASSERT_EQ(editor.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::NewAndUnwired);
    ASSERT_TRUE(editor.getDoubleClickPortDisconnectEnabled());
    ASSERT_FALSE(editor.getDefaultDualIOForNewModules());

    tab.setGraphEditor(&editor);
    EXPECT_EQ(editor.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::NewOnly);
    EXPECT_FALSE(editor.getDoubleClickPortDisconnectEnabled());
    EXPECT_TRUE(editor.getDefaultDualIOForNewModules());
}

TEST_F(PreferencesSettingsTabTest, TimelineFeatureDefaultsToEnabled) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.isTimelineFeatureEnabled());
    // Not yet touched by the user — nothing should be written to disk until setToggle/persist runs.
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("timelineFeatureEnabled"));
}

TEST_F(PreferencesSettingsTabTest, TimelineFeatureLoadsPersistedValue) {
    appProperties.getUserSettings()->setValue("timelineFeatureEnabled", "0");
    PreferencesSettingsTab tab(appProperties);
    EXPECT_FALSE(tab.isTimelineFeatureEnabled());
}

TEST_F(PreferencesSettingsTabTest, TogglingTimelineFeaturePersistsAndFiresCallback) {
    PreferencesSettingsTab tab(appProperties);

    std::vector<bool> fired;
    tab.onTimelineFeatureToggled = [&](bool enabled) { fired.push_back(enabled); };

    tab.setTimelineFeatureEnabled(false);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineFeatureEnabled"), "0");
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_FALSE(fired.back());

    tab.setTimelineFeatureEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("timelineFeatureEnabled"), "1");
    ASSERT_EQ(fired.size(), 2u);
    EXPECT_TRUE(fired.back());
}

TEST_F(PreferencesSettingsTabTest, PaintDoesNotCrash) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 400);
    juce::Image img(juce::Image::ARGB, 500, 400, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(tab.paint(g));
    EXPECT_NO_THROW(tab.resized());
}
