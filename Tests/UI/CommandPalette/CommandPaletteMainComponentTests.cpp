// CommandPaletteMainComponentTests.cpp -- the command palette through the real window: Cmd+K and the top bar's Search
// button open it, a pick runs what the same thing runs elsewhere (a card is selected and centred, the Timeline shows,
// a "+" row adds what the Add list adds), an on/off row flips in place, Escape hands focus back, the bar remembers
// where it was dragged, a plugin track's row has a window button reached from the keyboard, and every Preferences
// control has a Setting row of its name.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "../../TestWait.h"
#include "../Accessibility/AccessibilityAudit.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "MainComponent/MainComponentCommandPalette.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/CommandPalette/CommandPaletteComponent.h"
#include "UI/CommandPalette/CommandPaletteRows.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include "UI/Timeline/TimelineSectionStrip/SectionDropWords.h"
#include "UI/Timeline/TimelineSectionStrip/SectionStripSettings.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace {

using synth::ui::CommandPaletteComponent;
using synth::ui::ModMatrixPicker;

juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<float> position) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(), 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), position,
                            juce::Time::getCurrentTime(), 1, false);
}

class CommandPaletteWindowTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        guard_.emplace(juce::StringArray{
            "librarySidebarVisible", "bottomDockVisible", "aiPanelVisible", synth::kCommandPaletteRecentSettingKey,
            synth::kCommandPalettePositionSettingKey, MainComponent::kNaturalScrollingKey, "smartConnectionMode",
            "smartConnectionSideBySide", "autosaveIntervalMinutes", "themeMode", "defaultDarkThemeId",
            "defaultLightThemeId", synth::ui::kTimelineSectionsCoverOnDropKey, "packMacrosOnCollapse",
            synth::ui::kAskBeforeDeletingTrackKey, synth::ui::kDeleteMacroChoiceKey,
            synth::ui::kDeleteTrackMacroChoiceKey});
        tabGuard_.emplace();
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
        if (auto* ws = mc_->getWelcomeScreenForTest())
            ws->setVisible(false);
    }
    void TearDown() override {
        mc_.reset();
        tabGuard_.reset();
        guard_.reset();
        MainComponentTest::TearDown();
    }

    MainComponentCommandPalette& palette() { return MainComponentCommandPalette::of(*mc_); }
    ModMatrixPicker& picker() { return palette().getPaletteForTest()->getPicker(); }
    juce::PropertiesFile& settingsFile() { return *mc_->getAppPropertiesForTest().getUserSettings(); }
    // The text at the right of the Setting row called `name` (a dropdown's current choice).
    juce::String settingHint(const juce::String& name) {
        for (const auto& entry : palette().getEntriesForTest())
            if (entry.text == name)
                return entry.hint;
        return "<no such row>";
    }
    void openByKey() { ASSERT_TRUE(mc_->getCommandManager().invokeDirectly(AppCommands::openCommandPalette, false)); }
    // Opens the palette, types `query` and picks the first row.
    void run(const juce::String& query) {
        if (!palette().isOpen())
            openByKey();
        ASSERT_TRUE(palette().isOpen());
        picker().setSearchTextForTest(query);
        ASSERT_FALSE(picker().getVisibleItemTextsForTest().empty()) << query;
        picker().chooseVisibleItemForTest(0);
    }
    template <typename Module>
    std::vector<ModuleComponent*> cards() {
        std::vector<ModuleComponent*> found;
        for (auto* c : mc_->getGraphEditor().getModuleComponents())
            if (dynamic_cast<Module*>(c->getModule()) != nullptr)
                found.push_back(c);
        return found;
    }

    std::optional<synth::test::PersistedKeysGuard> guard_;
    std::optional<BottomDockActiveTabResetGuardMDT> tabGuard_;
    std::unique_ptr<MainComponent> mc_;
};

void collect(juce::Component& root, std::vector<juce::Component*>& out) {
    for (auto* child : root.getChildren()) {
        out.push_back(child);
        collect(*child, out);
    }
}

// What a palette row is called when the control's own name is longer than the row's (a number field's accessibility
// title says its unit). Everything else must match the control's text, ignoring case.
const std::map<juce::String, juce::String>& controlNameToRowName() {
    static const std::map<juce::String, juce::String> names{
        {"Autosave interval in minutes", "Autosave interval"},
        {"Follow playhead point, percent of the visible width", "Follow playhead point"},
    };
    return names;
}

// Controls that are not a setting: the picker that chooses which group of rows to show, and the search field above
// them.
const std::set<juce::String>& notSettings() {
    static const std::set<juce::String> names{"Preferences category", "Filter preferences"};
    return names;
}

} // namespace

TEST_F(CommandPaletteWindowTest, CmdKOpensThePaletteAndPressingItAgainClosesIt) {
    openByKey();
    ASSERT_TRUE(palette().isOpen());
    auto* bar = palette().getPaletteForTest();
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->getParentComponent(), mc_.get());
    EXPECT_EQ(picker().getSearchPlaceholderForTest(), "Search everything, or type + to add");
    EXPECT_EQ(bar->getX(), mc_->getWidth() / 2 - CommandPaletteComponent::kWidth / 2) << "centred by default";
    EXPECT_GT(bar->getY(), mc_->getToolbar().getBottom() - 1) << "under the top bar";

    openByKey();
    EXPECT_FALSE(palette().isOpen());
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
}

TEST_F(CommandPaletteWindowTest, TheTopBarSearchButtonOpensItAndBothHintsMentionThePlus) {
    auto* search = dynamic_cast<synth::ui::ToolbarButton*>(mc_->findChildWithID("searchButton"));
    ASSERT_NE(search, nullptr);
    EXPECT_EQ(search->getTitle(), "Search");
    EXPECT_TRUE(search->getTooltip().contains("type + to add")) << search->getTooltip();
    EXPECT_TRUE(search->getTooltip().contains(
        ShortcutManager::keyPressToDisplayString(mc_->getShortcutManager().getBinding("openCommandPalette"))))
        << search->getTooltip();
    ASSERT_TRUE(search->onClick);
    search->onClick();
    ASSERT_TRUE(palette().isOpen());

    auto& info = palette().getPaletteForTest()->getInfoHintForTest();
    auto* tip = dynamic_cast<juce::TooltipClient*>(&info);
    ASSERT_NE(tip, nullptr);
    EXPECT_TRUE(tip->getTooltip().contains("Type + to add")) << tip->getTooltip();
    EXPECT_FALSE(info.getTitle().isEmpty());
}

TEST_F(CommandPaletteWindowTest, APlusRowAddsWhatTheAddListAdds) {
    const auto before = cards<OscillatorModule>().size();
    openByKey();
    picker().setSearchTextForTest("+ osc");
    const auto names = picker().getVisibleRowNamesForTest();
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.front(), "Top match");
    EXPECT_EQ(picker().getVisibleItemTextsForTest().front(), "Oscillator");
    picker().chooseVisibleItemForTest(0);
    EXPECT_EQ(cards<OscillatorModule>().size(), before + 1);
    EXPECT_FALSE(palette().isOpen()) << "a pick closes the palette";
}

// A dropdown setting: Return on its row opens its choices in the same bar (a heading row, then the choices), typing
// narrows them, Return picks one, and the bar steps back to the first list with the setting changed.
TEST_F(CommandPaletteWindowTest, ADropdownSettingIsSetFromItsChoicesInTheSameBar) {
    auto& engine = mc_->getGraphEditor().getSmartConnections();
    engine.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    openByKey();
    picker().setSearchTextForTest("smart connections");
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), "Smart connections");

    EXPECT_TRUE(picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(palette().isOpen()) << "its choices open in the bar, not in the Settings tab";
    auto texts = picker().getVisibleItemTextsForTest();
    ASSERT_EQ(texts.size(), 5u);
    EXPECT_EQ(texts[0], "Smart connections") << "the heading row";
    EXPECT_EQ(texts[1], "Off");
    EXPECT_EQ(picker().getSearchTextForTest(), "") << "the choices start with an empty search";
    EXPECT_TRUE(picker().isVisibleItemTickedForTest(3)) << "the setting's current choice is ticked";
    EXPECT_EQ(picker().getHighlightedItemIndexForTest(), 1)
        << "the highlight starts on the first choice, not the heading";

    picker().setSearchTextForTest("all");
    texts = picker().getVisibleItemTextsForTest();
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[1], "All module moves");
    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));

    EXPECT_EQ(engine.getSmartConnectionMode(), GraphEditor::SmartConnectionMode::AllMoves);
    EXPECT_EQ(mc_->getAppPropertiesForTest().getUserSettings()->getValue("smartConnectionMode"), "AllMoves");
    EXPECT_TRUE(palette().isOpen());
    EXPECT_EQ(picker().getSearchTextForTest(), "smart connections") << "back on the first list, as it was left";
    EXPECT_EQ(picker().getVisibleItemTextsForTest().front(), "Smart connections");
}

TEST_F(CommandPaletteWindowTest, EscapeStepsBackOneLevelFromAChoicesListAndThenCloses) {
    openByKey();
    picker().setSearchTextForTest("autosave interval");
    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), "Autosave interval");
    ASSERT_GT(picker().getVisibleItemTextsForTest().size(), 2u);

    EXPECT_TRUE(picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(palette().isOpen()) << "Esc goes one level back, it does not close the bar";
    EXPECT_EQ(picker().getSearchTextForTest(), "autosave interval");
    EXPECT_EQ(picker().getVisibleItemTextsForTest().size(), 1u);

    EXPECT_TRUE(picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(palette().isOpen());
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
}

TEST_F(CommandPaletteWindowTest, ThePickedChoiceIsWrittenWhereTheSettingsFieldReadsItAndTheHeadingGoesBack) {
    auto* settings = mc_->getAppPropertiesForTest().getUserSettings();
    openByKey();
    picker().setSearchTextForTest("autosave interval");
    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    picker().setSearchTextForTest("10");
    ASSERT_EQ(picker().getVisibleItemTextsForTest().size(), 2u);
    picker().chooseVisibleItemForTest(1);
    EXPECT_EQ(settings->getIntValue("autosaveIntervalMinutes", 0), 10);

    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)); // Return on the top match opens it again
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), "Autosave interval");
    picker().chooseVisibleItemForTest(0); // the heading row is the way back
    EXPECT_EQ(picker().getVisibleItemTextsForTest().size(), 1u);
    EXPECT_EQ(picker().getSearchTextForTest(), "autosave interval");
}

TEST_F(CommandPaletteWindowTest, ThePickedThemeIsAppliedAtOnce) {
    synth::theme::ThemeManager themes;
    synth::theme::AppLookAndFeel lookAndFeel;
    mc_ = std::make_unique<MainComponent>(themes, lookAndFeel, std::make_unique<MockProvider>());
    mc_->setSize(1400, 900);
    openByKey();
    picker().setSearchTextForTest("theme");
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), "Theme");
    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    const auto texts = picker().getVisibleItemTextsForTest();
    ASSERT_GT(texts.size(), 3u);
    int other = -1;
    for (int i = 1; i < (int)texts.size() && other < 0; ++i)
        if (texts[(size_t)i] != themes.getActiveTheme().name)
            other = i;
    ASSERT_GT(other, 0);
    const auto wanted = texts[(size_t)other];
    picker().chooseVisibleItemForTest(other);
    EXPECT_EQ(themes.getActiveTheme().name, wanted);
    mc_.reset(); // before the theme manager and look and feel it points at
}

// Regression test for FRO2026: after Return opens a dropdown's choices, typing narrows them and Return picks the FIRST
// match (it is highlighted without an arrow key).
TEST_F(CommandPaletteWindowTest, TypingInAChoicesListNarrowsItAndReturnPicksTheFirstMatch) {
    synth::theme::ThemeManager themes;
    synth::theme::AppLookAndFeel lookAndFeel;
    mc_ = std::make_unique<MainComponent>(themes, lookAndFeel, std::make_unique<MockProvider>());
    mc_->setSize(1400, 900);
    openByKey();
    picker().setSearchTextForTest("theme");
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), "Theme");
    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    ASSERT_NE(themes.getActiveTheme().name, "Warm Console");
    picker().setSearchTextForTest("warm");
    const auto texts = picker().getVisibleItemTextsForTest();
    ASSERT_GE(texts.size(), 2u);
    EXPECT_EQ(texts[1], "Warm Console");
    EXPECT_EQ(picker().getHighlightedItemIndexForTest(), 1) << "the first match is highlighted";
    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    EXPECT_EQ(themes.getActiveTheme().name, "Warm Console");
    EXPECT_TRUE(picker().getListSlideForTest() <= 0.0f) << "the list comes back from the left (or lands in place)";
    mc_.reset(); // before the theme manager and look and feel it points at
}

// Regression test for FRO733: "lfo6" found no "LFO 6", since a typed word had to match one word of the label whole.
TEST_F(CommandPaletteWindowTest, FocusingAModuleSelectsItsCardAndCentresTheViewOnIt) {
    run("+ lfo");
    run("+ lfo"); // the second is numbered: "LFO 2"
    const auto lfos = cards<LFOModule>();
    ASSERT_EQ(lfos.size(), 2u) << "each + row added an LFO";
    auto& editor = mc_->getGraphEditor();
    ModuleComponent* card = nullptr;
    juce::String title;
    // Two of a kind are renumbered ("LFO 1", "LFO 2") on the engine's next naming pass, not at the add.
    ASSERT_TRUE(synth::test::waitUntil([&] {
        for (auto* candidate : lfos)
            if (auto* node = mc_->getAudioEngine().getGraph().getNodeForId(candidate->getNodeId()))
                if (synth::moduleTitle(*node).containsChar(' ')) {
                    card = candidate;
                    title = synth::moduleTitle(*node);
                    return true;
                }
        return false;
    })) << "one LFO has a numbered title";
    editor.centreViewOn({-4000.0f, -4000.0f});
    editor.clearSelection();

    openByKey();
    picker().setSearchTextForTest(title.removeCharacters(" ").toLowerCase()); // "lfo1" for "LFO 1"
    ASSERT_FALSE(picker().getVisibleItemTextsForTest().empty());
    EXPECT_EQ(picker().getVisibleItemTextsForTest().front(), title);
    picker().chooseVisibleItemForTest(0);

    const auto selected = editor.getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), card->getNodeId());
    const auto centre = editor.getLocalArea(card, card->getLocalBounds()).getCentre();
    EXPECT_NEAR(centre.x, editor.getLocalBounds().getCentreX(), 2);
    EXPECT_NEAR(centre.y, editor.getLocalBounds().getCentreY(), 2);
}

TEST_F(CommandPaletteWindowTest, PickingTimelineShowsTheTimeline) {
    if (mc_->isBottomDockConfiguredVisible())
        mc_->simulateToggleBottomPanelClick();
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    openByKey();
    picker().setSearchTextForTest("timeline");
    EXPECT_EQ(picker().getVisibleRowNamesForTest().front(), "Top match");
    EXPECT_EQ(picker().getVisibleItemTextsForTest().front(), "Timeline");
    picker().chooseVisibleItemForTest(0);
    ASSERT_TRUE(synth::test::waitUntil([&] {
        return mc_->isBottomDockConfiguredVisible() &&
               mc_->getBottomDock().getActiveTab() == synth::ui::BottomDockComponent::Tab::Timeline;
    }));
}

TEST_F(CommandPaletteWindowTest, AnOnOffRowFlipsInPlaceAndThePaletteStaysOpen) {
    auto* settings = mc_->getAppPropertiesForTest().getUserSettings();
    const bool before = settings->getBoolValue(MainComponent::kNaturalScrollingKey, true);
    openByKey();
    picker().setSearchTextForTest("natural scrolling");
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), "Natural scrolling");
    ASSERT_EQ(picker().getVisibleItemSwitchForTest(0), std::optional<bool>(before));

    picker().chooseVisibleItemForTest(0);
    EXPECT_TRUE(palette().isOpen());
    EXPECT_EQ(picker().getVisibleItemSwitchForTest(0), std::optional<bool>(!before));
    EXPECT_EQ(settings->getBoolValue(MainComponent::kNaturalScrollingKey, true), !before);

    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    EXPECT_TRUE(palette().isOpen()) << "Return flips it back, still open";
    EXPECT_EQ(settings->getBoolValue(MainComponent::kNaturalScrollingKey, true), before);
}

TEST_F(CommandPaletteWindowTest, EscapeClosesAndHandsFocusBackToWhatHadIt) {
    auto& editor = mc_->getGraphEditor();
    mc_->setFocusedComponentOverrideForTest(&editor);
    openByKey();
    mc_->setFocusedComponentOverrideForTest(nullptr);
    ASSERT_TRUE(palette().isOpen());
    EXPECT_TRUE(picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(palette().isOpen());
    EXPECT_EQ(palette().getFocusRestoredForTest(), &editor);
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
}

TEST_F(CommandPaletteWindowTest, AnEmptyQueryListsRecentPicksFirstThenTheViews) {
    run("timeline");
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
    openByKey();
    const auto names = picker().getVisibleRowNamesForTest();
    ASSERT_GE(names.size(), 3u);
    EXPECT_EQ(names[0], "Recent");
    EXPECT_EQ(names[1], "Timeline");
    EXPECT_EQ(names[2], "Views");
}

// Regression test for FRO733: the list opened scrolled one header down, hiding "Recent", since its rows went in while
// the picker still had its empty-list height.
TEST_F(CommandPaletteWindowTest, TheListOpensAtItsTopWithTheFirstHeaderInView) {
    run("timeline");
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
    openByKey();
    EXPECT_EQ(picker().getViewTopForTest(), 0) << "\"Recent\" sits above the first row";
}

TEST_F(CommandPaletteWindowTest, DraggingTheFrameMovesTheBarAndTheNextOpenStartsThere) {
    openByKey();
    auto* bar = palette().getPaletteForTest();
    const auto start = bar->getPosition();
    bar->mouseDown(mouseAt(*bar, {20.0f, 6.0f}));
    bar->mouseDrag(mouseAt(*bar, {60.0f, 46.0f}));
    bar->mouseUp(mouseAt(*bar, {60.0f, 46.0f}));
    const auto moved = bar->getPosition();
    EXPECT_EQ(moved, start + juce::Point<int>(40, 40));

    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::escapeKey));
    ASSERT_TRUE(synth::test::waitUntil([&] { return palette().getPaletteForTest() == nullptr; }));
    openByKey();
    EXPECT_EQ(palette().getPaletteForTest()->getPosition(), moved);
}

TEST_F(CommandPaletteWindowTest, APluginTracksRowHasAWindowButtonReachedByTabAndPressedByReturn) {
    const auto tracksBefore = mc_->getTimelineDoc().getTracks().size();
    run("+ midi track");
    ASSERT_TRUE(synth::test::waitUntil([&] { return mc_->getTimelineDoc().getTracks().size() == tracksBefore + 1; }));
    const auto& track = mc_->getTimelineDoc().getTracks().back();
    const auto trackId = track.id;
    const auto name = track.name;
    std::vector<synth::TrackId> toggled;
    palette().isPluginTrackForTest = [trackId](synth::TrackId id) { return id == trackId; };
    palette().togglePluginWindowForTest = [&toggled](synth::TrackId id) { toggled.push_back(id); };

    openByKey();
    picker().setSearchTextForTest(name);
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), name);
    auto* button = picker().getVisibleItemActionButtonForTest(0);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->getTitle(), "Show plugin window, " + name);
    auto* tip = dynamic_cast<juce::TooltipClient*>(button);
    ASSERT_NE(tip, nullptr);
    const auto chord = ShortcutManager::keyPressToDisplayString(
        mc_->getShortcutManager().getBinding("timelineToggleFocusedTrackPluginWindow"));
    EXPECT_TRUE(tip->getTooltip().contains(chord)) << tip->getTooltip();

    EXPECT_TRUE(picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::tabKey)));
    EXPECT_TRUE(picker().isActionSelectedForTest());
    picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    ASSERT_EQ(toggled.size(), 1u);
    EXPECT_EQ(toggled.front(), trackId);
    EXPECT_FALSE(palette().isOpen());
}

TEST_F(CommandPaletteWindowTest, EveryControlInThePaletteHasANameAndATooltip) {
    openByKey();
    const auto gaps = synth::test::auditAccessibility(*palette().getPaletteForTest());
    for (const auto& gap : gaps)
        ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "no name: " : "no tooltip: ") << gap.path;
}

// The guard (bug kind setting-not-searchable): a Preferences row added without its palette row fails here, so Cmd+K
// reaches every setting by name. The fix for a failure is a row in MainComponentCommandPalette (switchSpecs, a
// TabSetting) or MainComponentCommandPaletteChoices (a dropdown); an entry on notSettings() needs a reason.
TEST_F(CommandPaletteWindowTest, EveryPreferencesControlIsFoundByNameInThePalette) {
    openByKey();
    ASSERT_TRUE(palette().isOpen());
    std::set<juce::String> rows;
    for (const auto& entry : palette().getEntriesForTest())
        if (entry.kind == synth::ui::PaletteKind::Setting)
            rows.insert(entry.text.toLowerCase());
    ASSERT_GT(rows.size(), 20u);

    PreferencesSettingsTab tab(mc_->getAppPropertiesForTest());
    std::vector<juce::Component*> all;
    collect(tab, all);
    int checked = 0;
    std::vector<juce::String> missing;
    for (auto* c : all) {
        juce::String name;
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(c))
            name = toggle->getButtonText();
        else if (dynamic_cast<juce::ComboBox*>(c) != nullptr || dynamic_cast<juce::TextEditor*>(c) != nullptr)
            name = c->getTitle();
        if (name.isEmpty() || notSettings().count(name) > 0)
            continue;
        if (const auto alias = controlNameToRowName().find(name); alias != controlNameToRowName().end())
            name = alias->second;
        ++checked;
        if (rows.count(name.toLowerCase()) == 0)
            missing.push_back(name);
    }
    EXPECT_GT(checked, 30) << "the walk should reach the tab's toggles, dropdowns and number fields";
    for (const auto& name : missing)
        ADD_FAILURE() << "Settings > Preferences has \"" << name << "\" but Cmd+K has no Setting row of that name";
}

// "section drop", Return, pick Replace: the dropdown that Preferences shows is set from the bar, through the same key.
TEST_F(CommandPaletteWindowTest, TheSectionDropSettingIsFoundByNameAndSetFromItsChoices) {
    using Words = synth::ui::SectionDropWords;
    const juce::String name = "Timeline: dropping a section on other music";
    openByKey();
    picker().setSearchTextForTest("section drop");
    auto texts = picker().getVisibleItemTextsForTest();
    ASSERT_FALSE(texts.empty());
    EXPECT_EQ(texts.front(), name);
    EXPECT_EQ(settingHint(name), Words::insertChoice()) << "the current choice at the row's right";

    EXPECT_TRUE(picker().sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
    texts = picker().getVisibleItemTextsForTest();
    ASSERT_EQ(texts.size(), 3u) << "the heading and the two choices";
    EXPECT_EQ(texts[1], Words::insertChoice());
    EXPECT_EQ(texts[2], Words::replaceChoice());
    EXPECT_TRUE(picker().isVisibleItemTickedForTest(1));

    picker().chooseVisibleItemForTest(2);
    EXPECT_TRUE(synth::ui::sectionsCoverOnDrop(&settingsFile()));
    {
        PreferencesSettingsTab tab(mc_->getAppPropertiesForTest());
        EXPECT_TRUE(tab.isSectionsCoverOnDropEnabled()) << "Preferences shows what the bar picked";
    }
    EXPECT_EQ(settingHint(name), Words::replaceChoice()) << "the row shows its new value";
}

// An on/off row that was missing writes its key and has the live effect of its Preferences checkbox.
TEST_F(CommandPaletteWindowTest, AMissingOnOffRowFlipsInPlaceWithTheLiveEffect) {
    auto& macros = mc_->getGraphEditor().getMacroController();
    settingsFile().setValue("packMacrosOnCollapse", "1");
    macros.setPackMacrosOnCollapse(true);
    openByKey();
    picker().setSearchTextForTest("Collapsing also packs");
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), "Collapsing also packs macros together");
    picker().chooseVisibleItemForTest(0);
    EXPECT_TRUE(palette().isOpen());
    EXPECT_FALSE(settingsFile().getBoolValue("packMacrosOnCollapse", true));
    PreferencesSettingsTab tab(mc_->getAppPropertiesForTest());
    EXPECT_FALSE(tab.isPackMacrosOnCollapseEnabled());
}

// Turning "Ask before deleting a track" back on forgets the answers it had kept, as the Preferences checkbox does.
TEST_F(CommandPaletteWindowTest, TurningAskBeforeDeletingOnFromThePaletteForgetsTheKeptAnswers) {
    settingsFile().setValue(synth::ui::kAskBeforeDeletingTrackKey, "0");
    settingsFile().setValue(synth::ui::kDeleteMacroChoiceKey, "remove");
    settingsFile().setValue(synth::ui::kDeleteTrackMacroChoiceKey, "keep");
    openByKey();
    picker().setSearchTextForTest("Ask before deleting a track");
    ASSERT_EQ(picker().getVisibleItemTextsForTest().front(), "Ask before deleting a track or its macro");
    picker().chooseVisibleItemForTest(0);
    EXPECT_TRUE(settingsFile().getBoolValue(synth::ui::kAskBeforeDeletingTrackKey, false));
    EXPECT_FALSE(settingsFile().containsKey(synth::ui::kDeleteMacroChoiceKey));
    EXPECT_FALSE(settingsFile().containsKey(synth::ui::kDeleteTrackMacroChoiceKey));
}
