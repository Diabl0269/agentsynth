#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/ShortcutManager.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/PreferencesSettingsTab.h"
#include <algorithm>
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
    // T148 (docs/macros.md §7 item 9): both default ON, unlike the tri-state
    // getMacroAutoPortPreference() default of "ask" — see its own getter/setter comments.
    EXPECT_TRUE(tab.isMacroAutoCreatePortsOnDragEnabled());
    EXPECT_TRUE(tab.isMacroAutoDeletePortsOnLastCableEnabled());
}

// Mirrors DoubleClickSpansLocatorsDefaultsOnAndRoundTrips for the two T148 toggles: default ON,
// each persisted under its own key, reading the default must not write it, a fresh tab restores
// what was written.
TEST_F(PreferencesSettingsTabTest, MacroAutoCreateAndAutoDeleteTogglesDefaultOnAndRoundTrip) {
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_TRUE(tab.isMacroAutoCreatePortsOnDragEnabled());
        EXPECT_TRUE(tab.isMacroAutoDeletePortsOnLastCableEnabled());
        EXPECT_FALSE(appProperties.getUserSettings()->containsKey("macroAutoCreatePortsOnDrag"));
        EXPECT_FALSE(appProperties.getUserSettings()->containsKey("macroAutoDeletePortsOnLastCable"));

        tab.setMacroAutoCreatePortsOnDragEnabled(false);
        tab.setMacroAutoDeletePortsOnLastCableEnabled(false);
        EXPECT_FALSE(tab.isMacroAutoCreatePortsOnDragEnabled());
        EXPECT_FALSE(tab.isMacroAutoDeletePortsOnLastCableEnabled());
        EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoCreatePortsOnDrag"), "0");
        EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoDeletePortsOnLastCable"), "0");
    }
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_FALSE(tab.isMacroAutoCreatePortsOnDragEnabled());
        EXPECT_FALSE(tab.isMacroAutoDeletePortsOnLastCableEnabled());
        tab.setMacroAutoCreatePortsOnDragEnabled(true);
        tab.setMacroAutoDeletePortsOnLastCableEnabled(true);
    }
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_TRUE(tab.isMacroAutoCreatePortsOnDragEnabled());
        EXPECT_TRUE(tab.isMacroAutoDeletePortsOnLastCableEnabled());
    }
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
    appProperties.getUserSettings()->setValue("macroAutoCreatePortsOnDrag", "0");
    appProperties.getUserSettings()->setValue("macroAutoDeletePortsOnLastCable", "0");

    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::Off);
    EXPECT_FALSE(tab.isDoubleClickPortDisconnectEnabled());
    EXPECT_TRUE(tab.getDefaultDualIOForNewModules());
    EXPECT_FALSE(tab.isMacroAutoCreatePortsOnDragEnabled());
    EXPECT_FALSE(tab.isMacroAutoDeletePortsOnLastCableEnabled());
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

    // T148 (docs/macros.md §7 item 9): a toggle flip must reach the live GraphEditor immediately.
    tab.setMacroAutoCreatePortsOnDragEnabled(false);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoCreatePortsOnDrag"), "0");
    EXPECT_FALSE(editor.getAutoCreateMacroPortsOnDragEnabled());

    tab.setMacroAutoCreatePortsOnDragEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoCreatePortsOnDrag"), "1");
    EXPECT_TRUE(editor.getAutoCreateMacroPortsOnDragEnabled());

    tab.setMacroAutoDeletePortsOnLastCableEnabled(false);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoDeletePortsOnLastCable"), "0");
    EXPECT_FALSE(editor.getAutoDeleteMacroPortsOnLastCableEnabled());

    tab.setMacroAutoDeletePortsOnLastCableEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoDeletePortsOnLastCable"), "1");
    EXPECT_TRUE(editor.getAutoDeleteMacroPortsOnLastCableEnabled());
}

// Founder-review fix F5 (docs/macros.md §7 item 6.2): the macro auto-port preference. Tri-state,
// DEFAULT "ask" (Unset) — a silent default of either behaviour would change what grouping does the
// first time this ships with no warning.
TEST_F(PreferencesSettingsTabTest, MacroAutoPortDefaultsToAskAndDoesNotWriteUntouched) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::Unset);
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("macroAutoCreatePorts"));
}

TEST_F(PreferencesSettingsTabTest, MacroAutoPortPreferenceRoundTripsThroughApplicationProperties) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::AutoCreatePorts);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoCreatePorts"), "auto");
    }
    {
        // A fresh tab (as a freshly-opened Settings window would build) restores what was written.
        PreferencesSettingsTab tab(appProperties);
        EXPECT_EQ(tab.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::AutoCreatePorts);

        tab.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoCreatePorts"), "leave");
    }
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_EQ(tab.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);

        // "Always ask" is reachable again from the Preferences tab, not just the initial default.
        tab.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::Unset);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoCreatePorts"), "ask");
    }
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_EQ(tab.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::Unset);
    }
}

TEST_F(PreferencesSettingsTabTest, MacroAutoPortPreferencePushesToTheGraphEditor) {
    PreferencesSettingsTab tab(appProperties);
    AudioEngine engine;
    GraphEditor editor(engine);
    tab.setGraphEditor(&editor);
    ASSERT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::Unset);

    tab.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::AutoCreatePorts);
    EXPECT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::AutoCreatePorts);

    tab.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);
    EXPECT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);
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
    appProperties.getUserSettings()->setValue("macroAutoCreatePortsOnDrag", "0");
    appProperties.getUserSettings()->setValue("macroAutoDeletePortsOnLastCable", "0");

    PreferencesSettingsTab tab(appProperties);
    AudioEngine engine;
    GraphEditor editor(engine);
    ASSERT_EQ(editor.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::NewAndUnwired);
    ASSERT_TRUE(editor.getDoubleClickPortDisconnectEnabled());
    ASSERT_FALSE(editor.getDefaultDualIOForNewModules());
    ASSERT_TRUE(editor.getAutoCreateMacroPortsOnDragEnabled());
    ASSERT_TRUE(editor.getAutoDeleteMacroPortsOnLastCableEnabled());

    tab.setGraphEditor(&editor);
    EXPECT_EQ(editor.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::NewOnly);
    EXPECT_FALSE(editor.getDoubleClickPortDisconnectEnabled());
    EXPECT_TRUE(editor.getDefaultDualIOForNewModules());
    EXPECT_FALSE(editor.getAutoCreateMacroPortsOnDragEnabled());
    EXPECT_FALSE(editor.getAutoDeleteMacroPortsOnLastCableEnabled());
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

// ---- Per-module Dual I/O defaults ("Per-module I/O defaults..." button) ----------------------

// Round-trips the persisted per-type override map: absent means Follow global, set/clear/re-read
// across a fresh tab instance, mirroring the "not yet touched" + "fresh tab restores it" pattern
// DoubleClickSpansLocatorsDefaultsOnAndRoundTrips above uses for a plain bool preference.
TEST_F(PreferencesSettingsTabTest, DualIOPerModuleOverrideDefaultsToFollowGlobalAndRoundTrips) {
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_FALSE(tab.getDualIOOverrideForType("Reverb").has_value())
            << "an untouched module type follows the global default";
        // Reading the default must not WRITE it — same discipline as every other preference here.
        EXPECT_FALSE(appProperties.getUserSettings()->containsKey("dualIOPerModuleDefaults"));

        tab.setDualIOOverrideForType("Reverb", true);
        ASSERT_TRUE(tab.getDualIOOverrideForType("Reverb").has_value());
        EXPECT_TRUE(*tab.getDualIOOverrideForType("Reverb"));

        tab.setDualIOOverrideForType("Filter", false);
        ASSERT_TRUE(tab.getDualIOOverrideForType("Filter").has_value());
        EXPECT_FALSE(*tab.getDualIOOverrideForType("Filter"));

        // Clearing an override (picking "Follow global" again) removes it, not just zeroes it.
        tab.setDualIOOverrideForType("Reverb", std::nullopt);
        EXPECT_FALSE(tab.getDualIOOverrideForType("Reverb").has_value());
    }

    // A fresh tab restores exactly what was persisted.
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_FALSE(tab.getDualIOOverrideForType("Reverb").has_value());
        ASSERT_TRUE(tab.getDualIOOverrideForType("Filter").has_value());
        EXPECT_FALSE(*tab.getDualIOOverrideForType("Filter"));
        // Never touched at all, in either tab instance.
        EXPECT_FALSE(tab.getDualIOOverrideForType("Delay").has_value());
    }
}

// The canonical module-type list, DERIVED FROM THE MODULES: the expectation is computed by walking
// the module factory and asking each module hasDualIOParameter(), never written out by hand — the
// Ring Modulator was missing from the hand-written version, with no test able to notice. Data-driven
// over the factory, so a module added later is covered without touching this file.
TEST_F(PreferencesSettingsTabTest, DualIOModuleTypesIsDerivedFromTheModulesThemselves) {
    const auto& types = PreferencesSettingsTab::getDualIOModuleTypes();

    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames()) {
        SCOPED_TRACE(name.toStdString());
        auto probe = synth::AIStateMapper::createModule(name);
        ASSERT_NE(probe, nullptr);
        auto* mb = dynamic_cast<ModuleBase*>(probe.get());
        const bool supportsDualIO = mb != nullptr && mb->hasDualIOParameter();
        const auto occurrences = std::count(types.begin(), types.end(), name);
        EXPECT_EQ(occurrences, supportsDualIO ? 1 : 0)
            << (supportsDualIO ? "a module with the Dual I/O parameter must appear exactly once"
                               : "a module without the Dual I/O parameter must not appear");
    }

    // Anchors: the module that was missing, plus one of each family that always belonged.
    EXPECT_EQ(std::count(types.begin(), types.end(), juce::String("Ring Modulator")), 1);
    EXPECT_EQ(std::count(types.begin(), types.end(), juce::String("Oscillator")), 1);
    EXPECT_EQ(std::count(types.begin(), types.end(), juce::String("Reverb")), 1);
    EXPECT_EQ(std::count(types.begin(), types.end(), juce::String("LFO")), 0) << "the LFO has no second audio leg";
}

// Both consumers of "does this type support Dual I/O" must agree with that one list, for EVERY type
// on it: the Preferences popup (a row to set the default) and GraphEditor::applyDefaultDualIOForNewModule
// (the code that applies it to a module the user creates). Looped over the derived list rather than
// spot-checked, so a future module cannot reach main covered by one consumer and missed by the other.
TEST_F(PreferencesSettingsTabTest, EveryDualIOCapableTypeReachesBothThePopupAndTheNewModulePath) {
    const auto& types = PreferencesSettingsTab::getDualIOModuleTypes();
    ASSERT_GT(types.size(), 10u) << "expected the full stereo-capable set";

    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 460);
    auto popup = tab.createDualIOPerModuleDefaultsPopupForTest();
    ASSERT_NE(popup, nullptr);
    popup->setSize(400, 1200);

    std::vector<juce::String> rowLabels;
    for (auto* child : popup->getChildren())
        if (auto* l = dynamic_cast<juce::Label*>(child))
            rowLabels.push_back(l->getText());

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    tab.setGraphEditor(&editor);
    // Global default deliberately the OPPOSITE of every override below, so a type whose override is
    // ignored shows up as the global's value rather than accidentally matching.
    tab.setDefaultDualIOForNewModules(false);

    int y = 100;
    for (const auto& type : types) {
        SCOPED_TRACE(type.toStdString());

        // Consumer 1: the popup offers a row for this type.
        EXPECT_EQ(std::count(rowLabels.begin(), rowLabels.end(), type), 1) << "no row in the per-module defaults popup";

        // Consumer 2: the override for this type reaches a module the user creates.
        tab.setDualIOOverrideForType(type, true);
        editor.addModuleAtCanvasPosition(type, juce::Point<int>(100, y), nullptr);
        y += 40;

        juce::AudioProcessor* created = nullptr;
        for (auto* node : engine.getGraph().getNodes())
            if (synth::AIStateMapper::getFactoryTypeName(node->getProcessor()) == type ||
                node->getProcessor()->getName() == type)
                created = node->getProcessor();
        ASSERT_NE(created, nullptr) << "the type could not be created on the canvas";
        auto* mb = dynamic_cast<ModuleBase*>(created);
        ASSERT_NE(mb, nullptr);
        EXPECT_TRUE(mb->isDualIO()) << "the per-module override never reached applyDefaultDualIOForNewModule";

        tab.setDualIOOverrideForType(type, std::nullopt);
    }
}

// Test seam for the popup: button state plus the exact content component a real click would open,
// without launching the CallOutBox (see the class's header comment for why).
TEST_F(PreferencesSettingsTabTest, PerModuleButtonIsEnabledAndPopupReflectsPersistedOverrides) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 460);
    tab.setDualIOOverrideForType("Reverb", true);
    tab.setDualIOOverrideForType("Filter", false);

    juce::TextButton* perModuleButton = nullptr;
    for (auto* child : tab.getChildren())
        if (auto* btn = dynamic_cast<juce::TextButton*>(child))
            if (btn->getButtonText().containsIgnoreCase("Per-module"))
                perModuleButton = btn;
    ASSERT_NE(perModuleButton, nullptr) << "the per-module defaults control must be a labelled TextButton";
    EXPECT_TRUE(perModuleButton->isEnabled()) << "the popup button must be clickable, not the permanent placeholder";

    auto popup = tab.createDualIOPerModuleDefaultsPopupForTest();
    ASSERT_NE(popup, nullptr);
    popup->setSize(400, 900);

    std::vector<juce::Label*> labels;
    std::vector<juce::ComboBox*> combos;
    for (auto* child : popup->getChildren()) {
        if (auto* l = dynamic_cast<juce::Label*>(child))
            labels.push_back(l);
        if (auto* c = dynamic_cast<juce::ComboBox*>(child))
            combos.push_back(c);
    }
    ASSERT_EQ(labels.size(), PreferencesSettingsTab::getDualIOModuleTypes().size());
    ASSERT_EQ(combos.size(), PreferencesSettingsTab::getDualIOModuleTypes().size());

    auto comboForType = [&](const juce::String& type) -> juce::ComboBox* {
        for (size_t i = 0; i < labels.size(); ++i)
            if (labels[i]->getText() == type)
                return combos[i];
        return nullptr;
    };

    auto* reverbCombo = comboForType("Reverb");
    ASSERT_NE(reverbCombo, nullptr);
    EXPECT_EQ(reverbCombo->getText(), "Always on");

    auto* filterCombo = comboForType("Filter");
    ASSERT_NE(filterCombo, nullptr);
    EXPECT_EQ(filterCombo->getText(), "Always off");

    auto* oscCombo = comboForType("Oscillator");
    ASSERT_NE(oscCombo, nullptr);
    EXPECT_EQ(oscCombo->getText(), "Follow global") << "an untouched type must show Follow global, not a stale state";
}

// Driving the actual ComboBox (not the setter) is what a real click in the popup does — mirrors
// ClickingTheToggleReachesTheEditorAndNewModules driving the real ToggleButton for the global pref.
TEST_F(PreferencesSettingsTabTest, ChangingAPopupRowWritesThroughToPersistence) {
    PreferencesSettingsTab tab(appProperties);
    auto popup = tab.createDualIOPerModuleDefaultsPopupForTest();
    popup->setSize(400, 900);

    std::vector<juce::Label*> labels;
    std::vector<juce::ComboBox*> combos;
    for (auto* child : popup->getChildren()) {
        if (auto* l = dynamic_cast<juce::Label*>(child))
            labels.push_back(l);
        if (auto* c = dynamic_cast<juce::ComboBox*>(child))
            combos.push_back(c);
    }
    ASSERT_EQ(labels.size(), combos.size());

    juce::ComboBox* oscCombo = nullptr;
    for (size_t i = 0; i < labels.size(); ++i)
        if (labels[i]->getText() == "Oscillator")
            oscCombo = combos[i];
    ASSERT_NE(oscCombo, nullptr) << "could not locate the Oscillator row's ComboBox";

    oscCombo->setSelectedId(2 /* Always on */, juce::sendNotificationSync);
    ASSERT_TRUE(tab.getDualIOOverrideForType("Oscillator").has_value());
    EXPECT_TRUE(*tab.getDualIOOverrideForType("Oscillator"));
    EXPECT_TRUE(appProperties.getUserSettings()->getValue("dualIOPerModuleDefaults").contains("Oscillator"));

    oscCombo->setSelectedId(3 /* Always off */, juce::sendNotificationSync);
    ASSERT_TRUE(tab.getDualIOOverrideForType("Oscillator").has_value());
    EXPECT_FALSE(*tab.getDualIOOverrideForType("Oscillator"));

    oscCombo->setSelectedId(1 /* Follow global */, juce::sendNotificationSync);
    EXPECT_FALSE(tab.getDualIOOverrideForType("Oscillator").has_value());
}

// Layout: the toggle and the button share one row (not two stacked rows), and a divider separates
// this group from the next preference (loop-selection-arms) — the two bugs from the report.
TEST_F(PreferencesSettingsTabTest, DualIOGroupIsOneLineWithADividerBelow) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 460);

    juce::ToggleButton* dualToggle = nullptr;
    juce::TextButton* perModuleButton = nullptr;
    for (auto* child : tab.getChildren()) {
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
            if (tb->getButtonText().containsIgnoreCase("Split"))
                dualToggle = tb;
        if (auto* btn = dynamic_cast<juce::TextButton*>(child))
            if (btn->getButtonText().containsIgnoreCase("Per-module"))
                perModuleButton = btn;
    }
    ASSERT_NE(dualToggle, nullptr);
    ASSERT_NE(perModuleButton, nullptr);
    ASSERT_FALSE(dualToggle->getBounds().isEmpty());
    ASSERT_FALSE(perModuleButton->getBounds().isEmpty());

    // Same row: same top edge, side by side without overlapping.
    EXPECT_EQ(dualToggle->getBounds().getY(), perModuleButton->getBounds().getY())
        << "the toggle and the per-module button must be on one line";
    EXPECT_LE(dualToggle->getBounds().getRight(), perModuleButton->getBounds().getX())
        << "the toggle and the per-module button must not overlap";

    const int rowBottom = std::max(dualToggle->getBounds().getBottom(), perModuleButton->getBounds().getBottom());
    bool hasDividerBelowRow = false;
    for (const auto& divider : tab.getDividerBoundsForTest())
        if (divider.getY() >= rowBottom)
            hasDividerBelowRow = true;
    EXPECT_TRUE(hasDividerBelowRow) << "missing divider between the Dual I/O group and the next preference";
}

// The consumption path: GraphEditor::applyDefaultDualIOForNewModule (called from
// addModuleAtCanvasPosition, exactly like ClickingTheToggleReachesTheEditorAndNewModules exercises
// for the global default) must let a per-type override win over a global default that disagrees,
// and must fall through to the global default for a type with no override.
TEST_F(PreferencesSettingsTabTest, NewModuleHonoursPerModuleOverrideEvenWhenItDisagreesWithGlobal) {
    PreferencesSettingsTab tab(appProperties);
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    tab.setGraphEditor(&editor);

    // Global says dual, but Filter is overridden to always stay single...
    tab.setDualIOOverrideForType("Filter", false);
    // ...and Reverb is overridden to always be dual, even before the global agrees.
    tab.setDualIOOverrideForType("Reverb", true);
    tab.setDefaultDualIOForNewModules(true);

    editor.addModuleAtCanvasPosition("Filter", juce::Point<int>(100, 100), nullptr);
    editor.addModuleAtCanvasPosition("Reverb", juce::Point<int>(400, 100), nullptr);
    editor.addModuleAtCanvasPosition("Delay", juce::Point<int>(700, 100), nullptr); // no override

    auto isDual = [&engine](const juce::String& type) {
        for (auto* node : engine.getGraph().getNodes())
            if (node->getProcessor()->getName() == type)
                return dynamic_cast<ModuleBase*>(node->getProcessor())->isDualIO();
        ADD_FAILURE() << type << " was not created";
        return false;
    };

    EXPECT_FALSE(isDual("Filter")) << "a per-module override must win over a global default that disagrees";
    EXPECT_TRUE(isDual("Reverb")) << "a per-module override must win even when it already agrees with the global";
    EXPECT_TRUE(isDual("Delay")) << "a type with no override must still follow the global default";

    // Flip the global the other way: the overrides must still hold, and the un-overridden type
    // must follow the global to its new value.
    tab.setDefaultDualIOForNewModules(false);
    editor.addModuleAtCanvasPosition("Filter", juce::Point<int>(100, 300), nullptr);
    editor.addModuleAtCanvasPosition("Chorus", juce::Point<int>(400, 300), nullptr); // no override

    int filterDualCount = 0, filterSingleCount = 0;
    bool chorusIsDual = true;
    for (auto* node : engine.getGraph().getNodes()) {
        const auto name = node->getProcessor()->getName();
        auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (name == "Filter")
            (mb->isDualIO() ? filterDualCount : filterSingleCount)++;
        if (name == "Chorus")
            chorusIsDual = mb->isDualIO();
    }
    EXPECT_EQ(filterDualCount, 0) << "every Filter, old and new, must respect the override";
    EXPECT_GE(filterSingleCount, 2);
    EXPECT_FALSE(chorusIsDual) << "Chorus has no override, so it must follow the global's new value";
}

// MainComponent pushes loadDualIOPerModuleOverrides() straight into the GraphEditor at startup
// (see MainComponent's constructor), independent of any PreferencesSettingsTab instance — this
// pins the parser it relies on.
TEST_F(PreferencesSettingsTabTest, LoadDualIOPerModuleOverridesParsesWithoutATabInstance) {
    appProperties.getUserSettings()->setValue("dualIOPerModuleDefaults", R"({"Reverb":true,"Filter":false})");
    auto overrides = PreferencesSettingsTab::loadDualIOPerModuleOverrides(appProperties);
    ASSERT_EQ(overrides.count("Reverb"), 1u);
    EXPECT_TRUE(overrides.at("Reverb"));
    ASSERT_EQ(overrides.count("Filter"), 1u);
    EXPECT_FALSE(overrides.at("Filter"));
    EXPECT_EQ(overrides.count("Delay"), 0u);
}

// ---- Round 3 follow-up: live search filter -----------------------------------------------------

namespace {
juce::ToggleButton* findToggleByText(PreferencesSettingsTab& tab, const juce::String& text) {
    for (auto* child : tab.getChildren())
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
            if (tb->getButtonText().containsIgnoreCase(text))
                return tb;
    return nullptr;
}
} // namespace

// ---- Round 4 follow-up: the search field must not auto-grab focus on open ---------------------
//
// The actual bug (searchField stealing OS keyboard focus the instant the Settings DialogWindow's
// peer is first shown) needs a real ComponentPeer — juce::Component::grabKeyboardFocus() is a
// documented no-op without one (see TimelineClipLaneArea.cpp's identical caveat), and nothing in
// this suite calls addToDesktop(). So this pins the code-level fix instead of the runtime
// behaviour it prevents: the tab itself now wants keyboard focus (matching
// ShortcutsSettingsTab's identical fix for its own sibling search box), which is what makes it —
// not the search field — the target the very first, unsolicited focus grab lands on.
TEST_F(PreferencesSettingsTabTest, TabItselfWantsKeyboardFocusSoItInterceptsTheOpeningFocusGrab) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.getWantsKeyboardFocus())
        << "without this, the search field is the first focus-wanting descendant and silently "
           "steals keyboard focus the moment the Settings window is first shown";
}

// An untouched filter must reproduce the exact unfiltered layout — the empty-query fast path
// groupMatches()/resized() rely on, and also the state every existing bounds-sensitive test above
// (e.g. DualIOGroupIsOneLineWithADividerBelow) assumes.
TEST_F(PreferencesSettingsTabTest, EmptySearchFilterShowsEveryRow) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);
    EXPECT_TRUE(tab.getSearchFilterForTest().isEmpty());

    for (const juce::String& label : {"Double-click port to disconnect", "Show Alignment Guides",
                                      "Split Left/Right jacks on new modules", "Label every key"}) {
        auto* toggle = findToggleByText(tab, label);
        ASSERT_NE(toggle, nullptr) << label;
        EXPECT_TRUE(toggle->isVisible()) << label << " should be visible with no filter applied";
    }
}

TEST_F(PreferencesSettingsTabTest, SearchFilterHidesNonMatchingRowsByLabelText) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("alignment");
    EXPECT_TRUE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Double-click port to disconnect")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Label every key")->isVisible());
}

// A query that only appears in a TOOLTIP (not the visible label) must still surface the row — the
// spec is "label/tooltip text", not "label text alone".
TEST_F(PreferencesSettingsTabTest, SearchFilterMatchesByTooltipText) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    auto* alignmentToggle = findToggleByText(tab, "Show Alignment Guides");
    ASSERT_NE(alignmentToggle, nullptr);
    ASSERT_TRUE(alignmentToggle->getTooltip().containsIgnoreCase("dragging"))
        << "test premise: 'dragging' only appears in this row's tooltip, not its label";

    tab.setSearchFilterForTest("dragging");
    EXPECT_TRUE(alignmentToggle->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Label every key")->isVisible());
}

// The two loop-locator toggles are one filterable group (no divider between them — see their
// declaration comments): a query matching either keeps both visible together.
TEST_F(PreferencesSettingsTabTest, SearchFilterKeepsAGroupedPairTogether) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("locator");
    EXPECT_TRUE(findToggleByText(tab, "Timeline: P (loop selection)")->isVisible());
    EXPECT_TRUE(findToggleByText(tab, "double-click inside the locators")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Label every key")->isVisible());
}

TEST_F(PreferencesSettingsTabTest, ClearingSearchFilterRestoresEveryRow) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("alignment");
    ASSERT_FALSE(findToggleByText(tab, "Label every key")->isVisible());

    tab.setSearchFilterForTest("");
    EXPECT_TRUE(findToggleByText(tab, "Label every key")->isVisible());
    EXPECT_TRUE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
}

// Esc clears the field — driven by invoking the real onEscapeKey callback (see
// TimelinePanelTests.cpp's identical "call onEscapeKey() directly" idiom), not a synthetic key
// event, which a headless run cannot dispatch.
TEST_F(PreferencesSettingsTabTest, EscapeClearsTheSearchFilter) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("alignment");
    ASSERT_EQ(tab.getSearchFilterForTest(), "alignment");
    ASSERT_FALSE(findToggleByText(tab, "Label every key")->isVisible());

    tab.triggerSearchEscapeForTest();
    EXPECT_TRUE(tab.getSearchFilterForTest().isEmpty());
    EXPECT_TRUE(findToggleByText(tab, "Label every key")->isVisible());
}

// Dividers must collapse sensibly: when only ONE group matches, there is nothing left for a
// hairline to separate, so none should be drawn at all.
TEST_F(PreferencesSettingsTabTest, NoDividersLeftOverWhenOnlyOneGroupMatches) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);
    ASSERT_GT(tab.getDividerBoundsForTest().size(), 0u) << "test premise: the unfiltered tab has dividers";

    tab.setSearchFilterForTest("label every key");
    EXPECT_TRUE(tab.getDividerBoundsForTest().empty()) << "a single matching group has no neighbour to separate from";
}

// A query matching two NON-adjacent groups — "double-click" is in both group 2's toggle text
// ("Double-click port to disconnect") and group 5's second toggle text ("Timeline: double-click
// inside the locators spans them"), with groups 3 and 4 (neither mentioning it) filtered out in
// between — must draw exactly ONE divider between the two survivors: not zero (the filtered-out
// groups must not swallow it) and not two (an orphan hairline on each side of the gap).
TEST_F(PreferencesSettingsTabTest, ExactlyOneDividerBetweenTwoSurvivingNonAdjacentGroups) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("double-click");
    EXPECT_TRUE(findToggleByText(tab, "Double-click port to disconnect")->isVisible());
    EXPECT_TRUE(findToggleByText(tab, "double-click inside the locators")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Split Left/Right")->isVisible());
    EXPECT_EQ(tab.getDividerBoundsForTest().size(), 1u);
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
    for (auto* child : tab.getChildren()) {
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
    for (auto* child : tab.getChildren())
        if (auto* l = dynamic_cast<juce::Label*>(child))
            if (l->getText().contains("wheel zoom"))
                hint = l;
    ASSERT_NE(hint, nullptr) << "the zoom-scroll hint label must still exist";
    EXPECT_TRUE(hint->getText().contains(platformCommandKeyName()));
    EXPECT_TRUE(hint->getText().containsIgnoreCase("when off")) << "the one-line hint must spell out the OFF state";
}

// ---- Round 5: hint-label layout (cramped/narrow rendering), kept through round 6's revert -------
//
// naturalScrollingHint and zoomScrollUpZoomsInHint shared the same bug: fixed at 18px tall, room
// for barely one line, so a hint wider than the row got horizontally squeezed instead of wrapping.
// Both are fixed at the shared spot (PreferencesSettingsTab::styleMutedHintLabel); this pins the
// observable parts of that fix without depending on exact pixel values, which the font/theme could
// legitimately move.
TEST_F(PreferencesSettingsTabTest, HintLabelsGetTwoLinesOfHeightAndNeverSqueezeHorizontally) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    juce::Label* naturalHint = nullptr;
    juce::Label* zoomHint = nullptr;
    for (auto* child : tab.getChildren()) {
        if (auto* l = dynamic_cast<juce::Label*>(child)) {
            if (l->getText().containsIgnoreCase("graph canvas pans"))
                naturalHint = l;
            if (l->getText().contains("wheel zoom"))
                zoomHint = l;
        }
    }
    ASSERT_NE(naturalHint, nullptr);
    ASSERT_NE(zoomHint, nullptr);

    for (auto* hint : {naturalHint, zoomHint}) {
        EXPECT_GT(hint->getHeight(), 18) << "must be taller than the old one-line height";
        EXPECT_GE(hint->getMinimumHorizontalScale(), 1.0f)
            << "must never shrink text horizontally to fit - wrap onto a second line instead";
        EXPECT_FALSE(hint->getBounds().isEmpty());
    }
}

// ---------------------------------------------------------------------------
// P8-4: autosave enabled/interval preferences.
// ---------------------------------------------------------------------------

TEST_F(PreferencesSettingsTabTest, AutosaveDefaultsToOnAtTwoMinutesWithFiveBackupsAndDoesNotWriteUntouched) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.isAutosaveEnabled()) << "a safety net, not an opt-in";
    EXPECT_EQ(tab.getAutosaveIntervalMinutes(), 2);
    EXPECT_EQ(tab.getAutosaveBackupCount(), 5);
    // Reading the defaults must not WRITE them — an untouched preference stays absent from the file,
    // same discipline every other row in this tab follows.
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("autosaveEnabled"));
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("autosaveIntervalMinutes"));
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("autosaveBackupCount"));
}

TEST_F(PreferencesSettingsTabTest, AutosaveEnabledIntervalAndBackupCountRoundTrip) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setAutosaveEnabled(false);
        // An exact value outside the old fixed-choice combo's 1/2/5/10 set — proves this is now a
        // free-entry field, not a preset list.
        tab.setAutosaveIntervalMinutes(47);
        tab.setAutosaveBackupCount(13);
        EXPECT_FALSE(tab.isAutosaveEnabled());
        EXPECT_EQ(tab.getAutosaveIntervalMinutes(), 47);
        EXPECT_EQ(tab.getAutosaveBackupCount(), 13);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("autosaveEnabled"), "0");
        EXPECT_EQ(appProperties.getUserSettings()->getIntValue("autosaveIntervalMinutes"), 47);
        EXPECT_EQ(appProperties.getUserSettings()->getIntValue("autosaveBackupCount"), 13);
    }

    // A fresh tab restores what was written — the same key MainComponent::maybeAutosave/
    // performAutosave read directly (no live push from this tab, same "read at use time" idiom as
    // the natural-scrolling preference documents).
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_FALSE(tab.isAutosaveEnabled());
        EXPECT_EQ(tab.getAutosaveIntervalMinutes(), 47);
        EXPECT_EQ(tab.getAutosaveBackupCount(), 13);
    }
}

TEST_F(PreferencesSettingsTabTest, LoadsPersistedAutosaveValues) {
    appProperties.getUserSettings()->setValue("autosaveEnabled", "0");
    appProperties.getUserSettings()->setValue("autosaveIntervalMinutes", 10);
    appProperties.getUserSettings()->setValue("autosaveBackupCount", 20);

    PreferencesSettingsTab tab(appProperties);
    EXPECT_FALSE(tab.isAutosaveEnabled());
    EXPECT_EQ(tab.getAutosaveIntervalMinutes(), 10);
    EXPECT_EQ(tab.getAutosaveBackupCount(), 20);
}

TEST_F(PreferencesSettingsTabTest, SettingAutosaveBackupCountToZeroDisablesTheHistory) {
    PreferencesSettingsTab tab(appProperties);
    tab.setAutosaveBackupCount(0);
    EXPECT_EQ(tab.getAutosaveBackupCount(), 0);
    EXPECT_EQ(appProperties.getUserSettings()->getIntValue("autosaveBackupCount"), 0);
}

TEST_F(PreferencesSettingsTabTest, ClickingTheAutosaveToggleWritesTheSetting) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);
    ASSERT_TRUE(tab.isAutosaveEnabled());

    tab.setAutosaveEnabled(false);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("autosaveEnabled"), "0");
    tab.setAutosaveEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("autosaveEnabled"), "1");
}
