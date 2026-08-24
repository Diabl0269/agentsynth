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

// The setting reads as broken otherwise: the obvious way to check a preference is to flip it and
// look at the patch in front of you, and scoping it to new modules meant that never changed.
TEST_F(PreferencesSettingsTabTest, ChangingThePreferenceRelaysModulesAlreadyOnTheCanvas) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 420);
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    tab.setGraphEditor(&editor);

    // Built BEFORE the preference is touched: a split-block module and a contiguous-pair FX.
    editor.addModuleAtCanvasPosition("Filter", juce::Point<int>(100, 100), nullptr);
    editor.addModuleAtCanvasPosition("Delay", juce::Point<int>(500, 100), nullptr);

    auto stateOf = [&engine](const juce::String& type) {
        for (auto* node : engine.getGraph().getNodes())
            if (node->getProcessor()->getName() == type)
                if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
                    return mb->isDualIO() ? 1 : 0;
        return -1;
    };

    ASSERT_EQ(stateOf("Filter"), 0) << "the pref defaults off, so a new Filter starts collapsed";
    ASSERT_EQ(stateOf("Delay"), 0);

    tab.setDefaultDualIOForNewModules(true);
    EXPECT_EQ(stateOf("Filter"), 1) << "an existing split-block module must follow the preference";
    EXPECT_EQ(stateOf("Delay"), 1) << "an existing FX module must follow the preference";

    tab.setDefaultDualIOForNewModules(false);
    EXPECT_EQ(stateOf("Filter"), 0);
    EXPECT_EQ(stateOf("Delay"), 0);
}

// ...but opening the Settings window, or restoring the setting at startup, must NOT rewrite the
// user's patch. Only a deliberate change does.
TEST_F(PreferencesSettingsTabTest, MerelyWiringTheEditorDoesNotRelayExistingModules) {
    appProperties.getUserSettings()->setValue("defaultDualIOForNewModules", "0");

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);

    // A patch where the user (or a factory preset) deliberately split a module's jacks.
    editor.addModuleAtCanvasPosition("Filter", juce::Point<int>(100, 100), nullptr);
    for (auto* node : engine.getGraph().getNodes())
        if (node->getProcessor()->getName() == "Filter")
            if (auto* dual = findParameterByID(node->getProcessor(), "dualIO"))
                dual->setValueNotifyingHost(1.0f);

    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 420);
    tab.setGraphEditor(&editor); // what happens every time Settings opens

    for (auto* node : engine.getGraph().getNodes())
        if (node->getProcessor()->getName() == "Filter")
            EXPECT_TRUE(dynamic_cast<ModuleBase*>(node->getProcessor())->isDualIO())
                << "opening Settings must not collapse a module the user deliberately split";
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

// Moved from AppearanceSettingsTab; persistence key ("alignmentGuidesEnabled") is unchanged so
// existing saved prefs still apply.
TEST_F(PreferencesSettingsTabTest, AlignmentGuidesDefaultsToEnabledAndPersists) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.isAlignmentGuidesEnabled());
    // Not yet touched by the user — nothing should be written to disk until setToggle/persist runs.
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("alignmentGuidesEnabled"));

    AudioEngine engine;
    GraphEditor editor(engine);
    tab.setGraphEditor(&editor);

    tab.setAlignmentGuidesEnabled(false);
    EXPECT_FALSE(tab.isAlignmentGuidesEnabled());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("alignmentGuidesEnabled"), "0");
    EXPECT_FALSE(editor.getAlignmentGuidesEnabled());

    tab.setAlignmentGuidesEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("alignmentGuidesEnabled"), "1");
    EXPECT_TRUE(editor.getAlignmentGuidesEnabled());
}

TEST_F(PreferencesSettingsTabTest, AlignmentGuidesLoadsPersistedValue) {
    appProperties.getUserSettings()->setValue("alignmentGuidesEnabled", "0");
    PreferencesSettingsTab tab(appProperties);
    EXPECT_FALSE(tab.isAlignmentGuidesEnabled());
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
    for (auto* child : tab.getChildren())
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
            if (tb->getButtonText().containsIgnoreCase("Label every key"))
                labelToggle = tb;
    ASSERT_NE(labelToggle, nullptr) << "the piano-roll key-labels preference must be a labelled toggle";
    ASSERT_TRUE(labelToggle->getToggleState());

    labelToggle->setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(tab.isPianoRollKeyLabelModeAll());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("pianoRollKeyLabels"), "c");
}

TEST_F(PreferencesSettingsTabTest, PaintDoesNotCrash) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 400);
    juce::Image img(juce::Image::ARGB, 500, 400, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(tab.paint(g));
    EXPECT_NO_THROW(tab.resized());
}
