// OnCardScopeTests.cpp -- the edit bar's Apply to and Preset menus in the on-card editor: which cards a
// write changes (and the note saying how many), saving a preset and loading it back, the tick on the one
// in use, Reset to default, Cancel after an Apply to all, one undo step for the whole session, and a card
// edited here surviving a project save and load.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/CardWidgets/CardFader.h"

using namespace oncard_test;
using synth::CardWidget;

namespace {

const juce::PopupMenu::Item* itemOf(const juce::PopupMenu& menu, const juce::String& text) {
    return cardbody_test::menuItem(menu, text);
}

void choose(const juce::PopupMenu& menu, const juce::String& text) {
    const auto* item = itemOf(menu, text);
    ASSERT_NE(item, nullptr) << text;
    ASSERT_TRUE(item->isEnabled) << text;
    item->action();
}

NodeID onlyFilter(cardbody_test::CardCanvas& canvas) {
    for (auto* node : canvas.engine.getGraph().getNodes())
        if (dynamic_cast<FilterModule*>(node->getProcessor()) != nullptr)
            return node->nodeID;
    return {};
}

void relabel(OnCardRig& rig, CardLayoutOnCardEditor& editor, const juce::String& key, const juce::String& text) {
    auto* panel = openControlPanel(rig, editor, key);
    ASSERT_NE(panel, nullptr);
    panel->getLabelEditorForTest().setText(text, false);
    panel->commitLabelForTest();
}

void showAsFader(OnCardRig& rig, CardLayoutOnCardEditor& editor, const juce::String& key) {
    auto* panel = openControlPanel(rig, editor, key);
    ASSERT_NE(panel, nullptr);
    const auto& choices = panel->getOptions().widgetChoices;
    const auto at = std::find(choices.begin(), choices.end(), CardWidget::FaderV);
    ASSERT_NE(at, choices.end());
    panel->getShowAsForTest()->setSelectedIndex((int)(at - choices.begin()), juce::sendNotificationSync);
}

} // namespace

TEST(OnCardScope, ApplyToOffersThisModuleAndAllOfTheTypeWithANoteOfHowManyCardsChange) {
    OnCardRig rig;
    const auto first = rig.add(std::make_unique<FilterModule>(), 0, 0);
    rig.add(std::make_unique<FilterModule>(), 0, 600);
    rig.add(std::make_unique<OscillatorModule>(), 600, 0);
    auto* editor = rig.openOnCard(first);
    ASSERT_NE(editor, nullptr);

    auto menu = editor->buildApplyToMenuForTest();
    ASSERT_NE(itemOf(menu, "This module"), nullptr);
    ASSERT_NE(itemOf(menu, "All Filter modules"), nullptr);
    EXPECT_TRUE(itemOf(menu, "This module")->isTicked);
    EXPECT_FALSE(itemOf(menu, "All Filter modules")->isTicked);
    ASSERT_NE(itemOf(menu, "Changes 1 card"), nullptr);
    EXPECT_FALSE(itemOf(menu, "Changes 1 card")->isEnabled) << "a note, not a choice";

    choose(menu, "All Filter modules");
    EXPECT_TRUE(editor->isApplyToAllForTest());
    menu = editor->buildApplyToMenuForTest();
    EXPECT_TRUE(itemOf(menu, "All Filter modules")->isTicked);
    EXPECT_NE(itemOf(menu, "Changes 2 cards"), nullptr) << "the two Filters on the canvas, not the Oscillator";
}

TEST(OnCardScope, ChoosingAllWritesTheTypeDefaultClearsThisOverrideAndEveryLaterWriteFollows) {
    OnCardRig rig;
    const auto first = rig.add(std::make_unique<FilterModule>(), 0, 0);
    const auto second = rig.add(std::make_unique<FilterModule>(), 0, 600);
    // This module starts with a layout of its own (Level as a fader), which choosing All clears.
    ASSERT_TRUE(synth::setCardLayoutOverride(
        rig.canvas.engine.getGraph(), nullptr, first,
        cardbody_test::automaticLayoutWith(*rig.canvas.processor(first), {{"outputLevel", CardWidget::FaderV}})));
    rig.canvas.editor.updateComponents();
    auto* editor = rig.openOnCard(first);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive"); // this module only, so far
    ASSERT_TRUE(rig.storedLayout(first).has_value());
    EXPECT_FALSE(rig.card(second)->getCardBody()->hasMoreRow());

    choose(editor->buildApplyToMenuForTest(), "All Filter modules");
    EXPECT_FALSE(rig.storedLayout(first).has_value()) << "this module now follows the type's default";
    const auto stored = rig.store.loadDefault("Filter");
    ASSERT_EQ(stored.status, synth::ModuleCardLayoutStore::LoadStatus::Ok);
    EXPECT_TRUE(stored.layout.hidden.contains("drive"));
    EXPECT_TRUE(rig.card(second)->getCardBody()->hasMoreRow()) << "the other Filter was re-laid out at once";
    EXPECT_FALSE(widgetOf(*rig.card(second), "drive")->isVisible());

    hideThroughPanel(rig, *editor, "resonance");
    EXPECT_FALSE(rig.storedLayout(first).has_value()) << "a later write goes to the default too";
    EXPECT_TRUE(rig.store.loadDefault("Filter").layout.hidden.contains("resonance"));
    EXPECT_FALSE(widgetOf(*rig.card(second), "resonance")->isVisible());

    const int serial = rig.canvas.undo.getEditSerial();
    editor->done();
    EXPECT_EQ(rig.canvas.undo.getEditSerial(), serial + 1) << "the override write and its clearing: one step";
    ASSERT_TRUE(rig.canvas.undo.undo());
    ASSERT_TRUE(rig.storedLayout(first).has_value()) << "undo gives this module its own layout back";
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(widgetOf(*rig.card(first), "outputLevel")), nullptr);
    EXPECT_TRUE(rig.card(second)->getCardBody()->hasMoreRow()) << "the type's default file is a setting: it stays";

    const auto third = rig.add(std::make_unique<FilterModule>(), 600, 0);
    EXPECT_TRUE(rig.card(third)->getCardBody()->hasMoreRow()) << "a new Filter starts from the type's default";
}

TEST(OnCardScope, ChoosingThisModuleAgainGivesItItsOwnLayoutAndLeavesTheDefault) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    choose(editor->buildApplyToMenuForTest(), "All Filter modules");
    hideThroughPanel(rig, *editor, "drive");
    ASSERT_FALSE(rig.storedLayout(id).has_value());

    choose(editor->buildApplyToMenuForTest(), "This module");
    EXPECT_FALSE(editor->isApplyToAllForTest());
    ASSERT_TRUE(rig.storedLayout(id).has_value()) << "the layout being edited is now this module's own";
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("drive"));
    EXPECT_EQ(rig.store.loadDefault("Filter").status, synth::ModuleCardLayoutStore::LoadStatus::Ok)
        << "the default stays as it was written";
}

TEST(OnCardScope, CancelAfterApplyToAllPutsTheTypesDefaultBackToo) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    const auto other = rig.add(std::make_unique<FilterModule>(), 0, 600);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    choose(editor->buildApplyToMenuForTest(), "All Filter modules");
    hideThroughPanel(rig, *editor, "drive");
    ASSERT_TRUE(rig.card(other)->getCardBody()->hasMoreRow());

    editor->cancel();
    EXPECT_NE(rig.store.loadDefault("Filter").status, synth::ModuleCardLayoutStore::LoadStatus::Ok);
    EXPECT_FALSE(rig.card(id)->getCardBody()->hasMoreRow());
    EXPECT_FALSE(rig.card(other)->getCardBody()->hasMoreRow());
    EXPECT_FALSE(rig.canvas.undo.canUndo());
}

TEST(OnCardScope, SavingAPresetThenLoadingItRestoresTheLayoutAndTicksTheOneInUse) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    editor->savePresetForTest("No drive");
    EXPECT_TRUE(rig.store.listPresets("Filter").contains("No drive"));

    auto menu = editor->buildPresetMenuForTest();
    ASSERT_NE(itemOf(menu, "Save as..."), nullptr);
    ASSERT_NE(itemOf(menu, "Reset to default"), nullptr);
    ASSERT_NE(itemOf(menu, "No drive"), nullptr);
    EXPECT_TRUE(itemOf(menu, "No drive")->isTicked) << "the card is that layout now";

    choose(menu, "Reset to default");
    EXPECT_FALSE(rig.storedLayout(id).has_value());
    EXPECT_FALSE(rig.card(id)->getCardBody()->hasMoreRow());
    EXPECT_FALSE(itemOf(editor->buildPresetMenuForTest(), "No drive")->isTicked) << "no longer the layout in use";

    choose(editor->buildPresetMenuForTest(), "No drive");
    ASSERT_TRUE(rig.storedLayout(id).has_value());
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("drive"));
    EXPECT_FALSE(widgetOf(*rig.card(id), "drive")->isVisible());
    EXPECT_TRUE(itemOf(editor->buildPresetMenuForTest(), "No drive")->isTicked);
    EXPECT_NE(editor->getOutlineForTest("cutoff"), nullptr) << "the overlay re-synced to the rebuilt card";
}

TEST(OnCardScope, ResetToDefaultForAllModulesClearsTheTypeDefaultToo) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    choose(editor->buildApplyToMenuForTest(), "All Filter modules");
    hideThroughPanel(rig, *editor, "drive");
    ASSERT_EQ(rig.store.loadDefault("Filter").status, synth::ModuleCardLayoutStore::LoadStatus::Ok);

    choose(editor->buildPresetMenuForTest(), "Reset to default");
    EXPECT_NE(rig.store.loadDefault("Filter").status, synth::ModuleCardLayoutStore::LoadStatus::Ok);
    EXPECT_FALSE(rig.card(id)->getCardBody()->hasMoreRow());
}

TEST(OnCardScope, TheMenuOffersNoPresetsWhenThereAreNoneAndAReservedNameIsRefused) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_NE(itemOf(editor->buildPresetMenuForTest(), "Save as..."), nullptr);
    const auto menu = editor->buildPresetMenuForTest();
    juce::PopupMenu::MenuItemIterator it(menu);
    int items = 0;
    while (it.next())
        items += it.getItem().isSeparator ? 0 : 1;
    EXPECT_EQ(items, 2) << "Save as and Reset only";

    editor->savePresetForTest("default");
    EXPECT_FALSE(rig.store.listPresets("Filter").contains("default"));
    EXPECT_EQ(editor->getLastAnnouncementForTest(), "Could not save the preset default");
}

TEST(OnCardScope, APresetSavedThroughTheSourceCanBeListedAndDeleted) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    synth::ui::BuiltInCardLayoutSource source(rig.canvas.editor, nullptr, id);
    const auto layout = cardbody_test::automaticLayoutHiding(*rig.canvas.processor(id), {"drive"});
    ASSERT_TRUE(source.savePreset("Mine", layout));
    EXPECT_TRUE(source.listPresets().contains("Mine"));
    ASSERT_TRUE(source.loadPreset("Mine").has_value());
    EXPECT_TRUE(source.deletePreset("Mine"));
    EXPECT_FALSE(source.listPresets().contains("Mine"));
}

TEST(OnCardScope, AWholeSessionOfAddScopeAndPresetIsOneUndoStep) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    const int heightBefore = rig.card(id)->getHeight();
    const int serial = rig.canvas.undo.getEditSerial();
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    editor->savePresetForTest("No drive");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    clickRow(*panel->getRowForTest("drive"));
    choose(editor->buildPresetMenuForTest(), "No drive");
    EXPECT_EQ(rig.canvas.undo.getEditSerial(), serial) << "nothing is recorded while the editor is open";

    editor->done();
    EXPECT_EQ(rig.canvas.undo.getEditSerial(), serial + 1) << "closing records the session once";
    ASSERT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(rig.storedLayout(id).has_value()) << "one undo takes every edit of the session back";
    EXPECT_EQ(rig.card(id)->getHeight(), heightBefore);
    EXPECT_FALSE(rig.canvas.undo.canUndo());
}

TEST(OnCardScope, ACardEditedHereSurvivesSaveAndReload) {
    juce::var saved;
    juce::StringArray visibleBefore;
    {
        OnCardRig rig;
        const auto id = rig.add(std::make_unique<FilterModule>(), 200, 200);
        auto* editor = rig.openOnCard(id);
        ASSERT_NE(editor, nullptr);
        hideThroughPanel(rig, *editor, "drive");
        relabel(rig, *editor, "cutoff", "Freq");
        showAsFader(rig, *editor, "outputLevel");
        editor->done();

        for (const auto& item : rig.card(id)->getCardBody()->getPlan().items)
            if (item.widget != nullptr && item.widget->isVisible() && item.param != nullptr)
                visibleBefore.add(item.param->paramID);
        saved = synth::AIStateMapper::graphToJSON(rig.canvas.engine.getGraph());
    }
    ASSERT_TRUE(juce::JSON::toString(saved).contains("cardLayout")) << "the project carries the layout";

    OnCardRig reopened;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(saved, reopened.canvas.engine.getGraph(), true, true, false));
    reopened.canvas.editor.updateComponents();
    auto* card = reopened.card(onlyFilter(reopened.canvas));
    ASSERT_NE(card, nullptr);
    auto* body = card->getCardBody();
    EXPECT_TRUE(body->hasMoreRow());
    EXPECT_FALSE(body->findWidget("drive")->isVisible()) << "Drive is still hidden";
    EXPECT_EQ(cardlayouteditor_test::captionOf(*card, "cutoff"), "Freq");
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(body->findWidget("outputLevel")), nullptr) << "Level is a fader";
    juce::StringArray visibleAfter;
    for (const auto& item : body->getPlan().items)
        if (item.widget != nullptr && item.widget->isVisible() && item.param != nullptr)
            visibleAfter.add(item.param->paramID);
    EXPECT_EQ(visibleAfter, visibleBefore);
}

TEST(OnCardScope, ApplyToAllThenASecondFilterAddedLaterShowsTheSameLayout) {
    OnCardRig rig;
    const auto first = rig.add(std::make_unique<FilterModule>(), 0, 0);
    auto* editor = rig.openOnCard(first);
    ASSERT_NE(editor, nullptr);
    choose(editor->buildApplyToMenuForTest(), "All Filter modules");
    hideThroughPanel(rig, *editor, "drive");
    relabel(rig, *editor, "cutoff", "Freq");
    editor->done();

    const auto second = rig.add(std::make_unique<FilterModule>(), 0, 700);
    auto* card = rig.card(second);
    EXPECT_FALSE(widgetOf(*card, "drive")->isVisible());
    EXPECT_EQ(cardlayouteditor_test::captionOf(*card, "cutoff"), "Freq");
    EXPECT_FALSE(rig.storedLayout(second).has_value()) << "it follows the type's default, with no copy of its own";
}
