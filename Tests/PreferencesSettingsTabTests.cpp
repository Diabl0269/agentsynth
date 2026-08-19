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

// The tests above drive the programmatic setters. This one clicks the actual button, which is what
// a user does — it is the only thing that exercises the onClick lambda wiring.
TEST_F(PreferencesSettingsTabTest, ClickingTheToggleReachesTheEditorAndNewModules) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 420);
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    tab.setGraphEditor(&editor);

    juce::ToggleButton* dualToggle = nullptr;
    for (auto* child : tab.getChildren())
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
            if (tb->getButtonText().containsIgnoreCase("Split"))
                dualToggle = tb;
    ASSERT_NE(dualToggle, nullptr) << "the Dual I/O preference must be a labelled toggle";
    ASSERT_FALSE(dualToggle->getBounds().isEmpty()) << "an unlaid-out control cannot be clicked";
    ASSERT_FALSE(dualToggle->getToggleState());

    // NOT triggerClick(): that posts a command message, which never dispatches in a headless test.
    // sendNotificationSync runs the same onClick lambda a real click would, synchronously.
    dualToggle->setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(dualToggle->getToggleState());
    EXPECT_TRUE(editor.getDefaultDualIOForNewModules()) << "the click never reached the GraphEditor";
    EXPECT_EQ(appProperties.getUserSettings()->getValue("defaultDualIOForNewModules"), "1");

    // ...and a module created afterwards actually honours it.
    editor.addModuleAtCanvasPosition("Delay", juce::Point<int>(100, 100), nullptr);
    editor.addModuleAtCanvasPosition("Oscillator", juce::Point<int>(400, 100), nullptr);
    for (auto* node : engine.getGraph().getNodes()) {
        const auto name = node->getProcessor()->getName();
        if (name == "Delay" || name == "Oscillator") {
            auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
            ASSERT_NE(mb, nullptr);
            EXPECT_TRUE(mb->isDualIO()) << name << " ignored the preference set by clicking the toggle";
        }
    }

    dualToggle->setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(editor.getDefaultDualIOForNewModules());
    editor.addModuleAtCanvasPosition("Filter", juce::Point<int>(700, 100), nullptr);
    for (auto* node : engine.getGraph().getNodes())
        if (node->getProcessor()->getName() == "Filter")
            EXPECT_FALSE(dynamic_cast<ModuleBase*>(node->getProcessor())->isDualIO())
                << "Filter ignored the single-jack preference";
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

TEST_F(PreferencesSettingsTabTest, PaintDoesNotCrash) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 400);
    juce::Image img(juce::Image::ARGB, 500, 400, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(tab.paint(g));
    EXPECT_NO_THROW(tab.resized());
}
