// OnCardAddControlTests.cpp -- "+ Add control" in the on-card editor: the panel lists exactly what the card
// does not show, searches, and a click, Return or a drag onto the card puts the control back in the last
// group at a free spot. The list, the search and the layout edit are also pinned as pure functions.

#include "../../../Accessibility/AccessibilityAudit.h"
#include "../../../Accessibility/TabOrderHelpers.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardAddControlModel.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"

using namespace oncard_test;
using synth::ui::oncard::tooClose;

namespace {

void drop(CardLayoutOnCardEditor& editor, const juce::String& key, juce::Point<int> by) {
    Pointer pointer(editor, key);
    pointer.moveBy(by, juce::ModifierKeys::commandModifier);
    pointer.release();
}

// `key` keeps the gap from every other control of its section.
void expectNoCrowding(ModuleComponent& card, const juce::String& key) {
    const auto cells = synth::ui::collectCells(card);
    int section = -1;
    for (const auto& cell : cells)
        if (cell.key == key)
            section = cell.section;
    ASSERT_GE(section, 0) << key << " is on the card";
    for (size_t i = 0; i < cells.size(); ++i)
        for (size_t j = i + 1; j < cells.size(); ++j)
            if (cells[i].section == section && cells[j].section == section &&
                (cells[i].key == key || cells[j].key == key))
                EXPECT_FALSE(tooClose(cells[i].rect, cells[j].rect)) << cells[i].key << " and " << cells[j].key;
}

} // namespace

TEST(OnCardAddControlModel, MatchingControlsFiltersByTheAppsOneMatcherAndRanksPrefixesFirst) {
    const std::vector<synth::ui::AddableControl> controls{{"fine", "Fine tune"}, {"drive", "Drive"}, {"tune", "Tune"}};
    EXPECT_EQ(synth::ui::matchingControls(controls, "").size(), 3u);
    const auto matches = synth::ui::matchingControls(controls, "tune");
    ASSERT_EQ(matches.size(), 2u);
    EXPECT_EQ(matches[0].name, "Tune") << "a name that starts with the word beats one that only contains it";
    EXPECT_EQ(matches[1].name, "Fine tune");
    EXPECT_TRUE(synth::ui::matchingControls(controls, "zzz").empty());
}

TEST(OnCardAddControlModel, TheCountLineCountsTheHiddenControlsAndTheMatches) {
    EXPECT_EQ(synth::ui::addCountText(3, 3, ""), "3 hidden controls");
    EXPECT_EQ(synth::ui::addCountText(1, 1, ""), "1 hidden control");
    EXPECT_EQ(synth::ui::addCountText(2, 3, "r"), "2 of 3 hidden controls");
    EXPECT_EQ(synth::ui::addCountText(0, 3, "zzz"), "No control matches");
}

TEST(OnCardAddControlModel, AddingMovesTheItemToTheLastGridGroupKeepingItsSettingsAndUnhidesIt) {
    synth::CardLayout layout;
    synth::CardSection first;
    first.id = "a";
    synth::CardParamItem drive;
    drive.paramId = "drive";
    drive.widget = synth::CardWidget::FaderV;
    drive.label = "Gain";
    first.items.emplace_back(drive);
    synth::CardSection last;
    last.id = "b";
    synth::CardSection footer;
    footer.id = synth::CardSection::kFooterId;
    layout.sections = {first, last, footer};
    layout.hidden.add("drive");

    const auto added = synth::ui::withControlAdded(layout, "drive", juce::Point<int>(4, 8));
    EXPECT_TRUE(added.hidden.isEmpty());
    EXPECT_TRUE(added.sections[0].items.empty()) << "it left its old place";
    ASSERT_EQ(added.sections[1].items.size(), 1u) << "the footer is never the target";
    const auto& item = std::get<synth::CardParamItem>(added.sections[1].items[0]);
    EXPECT_EQ(item.paramId, "drive");
    EXPECT_EQ(item.widget, synth::CardWidget::FaderV);
    EXPECT_EQ(item.label, std::optional<juce::String>("Gain"));
    EXPECT_EQ(item.at, std::optional<juce::Point<int>>(juce::Point<int>(4, 8)));
}

TEST(OnCardAddControlModel, ALayoutWithOnlyAFooterGetsAMainGroupFirst) {
    synth::CardLayout layout;
    synth::CardSection footer;
    footer.id = synth::CardSection::kFooterId;
    layout.sections = {footer};
    const auto added = synth::ui::withControlAdded(layout, "drive", std::nullopt);
    ASSERT_EQ(added.sections.size(), 2u);
    EXPECT_EQ(added.sections[0].id, "main");
    EXPECT_EQ(added.sections[0].items.size(), 1u);
    EXPECT_EQ(synth::ui::planSectionIndexOf(added, 1), 1);
    EXPECT_EQ(synth::ui::layoutSectionIndexOfPlan(added, 0), 0);
}

TEST(OnCardAddControl, TheStripUnderTheCardHoldsAButtonThatIsOnOnlyWhileSomethingIsOffTheCard) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto& button = editor->getAddButtonForTest();
    EXPECT_EQ(editor->getHeight(), rig.card(id)->getHeight() + CardLayoutOnCardEditor::kAddStripHeight)
        << "the overlay reaches below the card, so the card keeps its size";
    EXPECT_EQ(editor->getWidth(), rig.card(id)->getWidth());
    EXPECT_EQ(button.getButtonText(), "+ Add control");
    EXPECT_EQ(button.getWidth(), editor->getWidth()) << "full width";
    EXPECT_GE(button.getY(), rig.card(id)->getHeight()) << "below the card's content";
    EXPECT_FALSE(button.isEnabled()) << "every control is on the card";

    hideThroughPanel(rig, *editor, "drive");
    EXPECT_TRUE(button.isEnabled());
    EXPECT_EQ(editor->getBounds().getHeight(), rig.card(id)->getHeight() + CardLayoutOnCardEditor::kAddStripHeight);
}

TEST(OnCardAddControl, ThePanelListsExactlyTheHiddenAndTheUnplacedControls) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    // Keep Key Track out of the layout altogether: the layout never places it.
    auto layout = rig.card(id)->getCardBody()->explicitLayout();
    for (auto& section : layout.sections)
        std::erase_if(section.items, [](const synth::CardItem& item) {
            const auto* param = std::get_if<synth::CardParamItem>(&item);
            return param != nullptr && param->paramId == "keyTrack";
        });
    ASSERT_TRUE(synth::setCardLayoutOverride(rig.canvas.engine.getGraph(), nullptr, id, layout));
    rig.canvas.editor.updateComponents();
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");

    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    auto names = panel->getRowNamesForTest();
    names.sort(false);
    EXPECT_EQ(names, juce::StringArray({"Drive", "Key Track"})) << "hidden and unplaced, nothing on the card";
    EXPECT_EQ(panel->getCountTextForTest(), "2 hidden controls");
    EXPECT_EQ(panel->getHintTextForTest(), "Click to add, or drag onto the card");
    EXPECT_EQ(editor->getAddPanelForTest(), panel);
    EXPECT_EQ(panel->getRowForTest("drive")->getTitle(), "Drive");
}

TEST(OnCardAddControl, TypingFiltersTheRowsAndTheCountLine) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    hideThroughPanel(rig, *editor, "resonance");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->getRowCountForTest(), 2);

    panel->setQueryForTest("res");
    EXPECT_EQ(panel->getRowNamesForTest(), juce::StringArray("Resonance"));
    EXPECT_EQ(panel->getCountTextForTest(), "1 of 2 hidden controls");
    panel->setQueryForTest("zzz");
    EXPECT_EQ(panel->getRowCountForTest(), 0);
    EXPECT_EQ(panel->getCountTextForTest(), "No control matches");
    panel->setQueryForTest("");
    EXPECT_EQ(panel->getRowCountForTest(), 2);
}

TEST(OnCardAddControl, ClickingARowPutsTheControlBackInTheLastGroupAndWritesOnce) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    ASSERT_TRUE(rig.storedLayout(id)->hidden.contains("drive"));
    ASSERT_FALSE(widgetOf(*rig.card(id), "drive")->isVisible());
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);

    clickRow(*panel->getRowForTest("drive"));
    const auto stored = rig.storedLayout(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->hidden.contains("drive"));
    EXPECT_TRUE(widgetOf(*rig.card(id), "drive")->isVisible());
    const auto& last = stored->sections[stored->sections.size() - 2]; // the footer is last
    EXPECT_EQ(std::get<synth::CardParamItem>(last.items.back()).paramId, "drive") << "the last grid group";
    EXPECT_NE(editor->getOutlineForTest("drive"), nullptr) << "and it is outlined again";
    EXPECT_EQ(editor->getLastAnnouncementForTest(), "Drive added");
    EXPECT_EQ(editor->getAddPanelForTest(), nullptr) << "nothing is left to add, so the panel closed";
    EXPECT_FALSE(editor->getAddButtonForTest().isEnabled());

    editor->done();
    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(widgetOf(*rig.card(id), "drive")->isVisible()) << "the add was one step: undo hides it again";
    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(rig.canvas.undo.canUndo()) << "and the hide was the other";
}

TEST(OnCardAddControl, InAPositionedGroupTheControlLandsAtAFreeSpotThatKeepsTheGap) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    drop(*editor, "outputLevel", {0, 30}); // the last group turns positioned
    hideThroughPanel(rig, *editor, "drive");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);

    clickRow(*panel->getRowForTest("drive"));
    const auto stored = rig.storedLayout(id);
    ASSERT_TRUE(stored.has_value());
    const auto* item = storedItem(*stored, "drive");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->at.has_value()) << "a positioned group is written a position";
    ASSERT_TRUE(widgetOf(*rig.card(id), "drive")->isVisible());
    expectNoCrowding(*rig.card(id), "drive");
}

TEST(OnCardAddControl, UpDownMoveTheChosenRowReturnAddsItAndTheSearchFieldKeepsTheFocusKeys) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    hideThroughPanel(rig, *editor, "resonance");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    auto& field = panel->getSearchFieldForTest();
    ASSERT_EQ(panel->getChosenIndexForTest(), 0);
    const auto first = panel->getRowForTest(0)->getControl().paramId;
    const auto second = panel->getRowForTest(1)->getControl().paramId;

    EXPECT_TRUE(field.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(panel->getChosenIndexForTest(), 1);
    EXPECT_TRUE(panel->getRowForTest(1)->isSelected());
    EXPECT_TRUE(field.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(panel->getChosenIndexForTest(), 1) << "clamped at the last row";
    EXPECT_TRUE(field.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(panel->getChosenIndexForTest(), 0);

    EXPECT_TRUE(field.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(widgetOf(*rig.card(id), first)->isVisible());
    EXPECT_FALSE(widgetOf(*rig.card(id), second)->isVisible());
    ASSERT_NE(editor->getAddPanelForTest(), nullptr) << "the panel stays for the next one";
    EXPECT_EQ(editor->getAddPanelForTest()->getRowCountForTest(), 1);

    EXPECT_TRUE(field.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))) << "Esc closes the panel";
    EXPECT_EQ(editor->getAddPanelForTest(), nullptr);
}

TEST(OnCardAddControl, DraggingARowOntoTheCardDropsTheControlWhereItIsReleased) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);

    const auto anchor = editor->getCellRectForTest("outputLevel");
    const auto pointer = anchor.getCentre() + juce::Point<int>(anchor.getWidth(), 12);
    dragRowTo(*editor, *panel->getRowForTest("drive"), pointer);

    const auto stored = rig.storedLayout(id);
    ASSERT_TRUE(stored.has_value());
    const auto* item = storedItem(*stored, "drive");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->at.has_value()) << "a drop writes a position, as a drop of a card control does";
    EXPECT_FALSE(stored->hidden.contains("drive"));
    const auto landed = editor->getCellRectForTest("drive");
    EXPECT_FALSE(landed.isEmpty());
    EXPECT_NEAR(landed.getCentreX(), pointer.x, 3);
    EXPECT_TRUE(landed.expanded(3).contains(pointer)) << "it lands under the pointer";
    expectNoCrowding(*rig.card(id), "drive");
    EXPECT_TRUE(editor->getAddGhostForTest().isEmpty()) << "the ghost is gone once it lands";
}

TEST(OnCardAddControl, ADragThatEndsOffTheCardAddsNothing) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    const auto before = rig.storedLayout(id);

    dragRowTo(*editor, *panel->getRowForTest("drive"), {editor->getWidth() + 80, 20});
    EXPECT_EQ(rig.storedLayout(id), before);
    EXPECT_FALSE(widgetOf(*rig.card(id), "drive")->isVisible());
}

TEST(OnCardAddControl, TheGhostFollowsThePointerWhileARowIsDragged) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    auto& row = *panel->getRowForTest("drive");

    const auto from = row.localPointToGlobal(row.getLocalBounds().getCentre());
    const auto at = editor->getCellRectForTest("cutoff").getCentre();
    row.mouseDown(eventAtScreen(row, from, juce::ModifierKeys::leftButtonModifier));
    row.mouseDrag(eventAtScreen(row, editor->localPointToGlobal(at), juce::ModifierKeys::leftButtonModifier));
    EXPECT_FALSE(editor->getAddGhostForTest().isEmpty());
    EXPECT_NEAR(editor->getAddGhostForTest().getCentreX(), at.x, 3);
    row.mouseUp(eventAtScreen(row, editor->localPointToGlobal(at), 0));
    EXPECT_TRUE(editor->getAddGhostForTest().isEmpty());
}

TEST(OnCardAddControl, TheAddedControlIsNotLeftFadedOut) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    clickRow(*panel->getRowForTest("drive"));
    editor->finishMotionForTest();
    EXPECT_FLOAT_EQ(widgetOf(*rig.card(id), "drive")->getAlpha(), 1.0f);
}

TEST(OnCardAddControl, EveryNewControlIsNamedTooltippedAndReachedByTabInOrder) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");

    auto& bar = editor->getEditBarForTest();
    EXPECT_EQ(bar.getPresetButton().getTitle(), "Preset");
    EXPECT_EQ(bar.getPresetButton().getTooltip(), "Save, load or reset this card's layout");
    EXPECT_EQ(bar.getApplyToButton().getTitle(), "Apply to");
    EXPECT_EQ(bar.getApplyToButton().getTooltip(), "Choose which cards this layout changes");
    EXPECT_EQ(editor->getAddButtonForTest().getTitle(), "Add control");
    EXPECT_EQ(editor->getAddButtonForTest().getTooltip(), "Add a hidden control");

    const auto walk = synth::test::walkTabOrder(*editor);
    const auto names = walk.names();
    ASSERT_GE(names.size(), 6);
    EXPECT_EQ(names[0], "Preset");
    EXPECT_EQ(names[1], "Apply to");
    EXPECT_EQ(names[2], "Cancel");
    EXPECT_EQ(names[3], "Done");
    EXPECT_EQ(names[names.size() - 1], "Add control") << "after the outlines";

    const auto gaps = synth::test::auditAccessibility(*editor);
    EXPECT_TRUE(gaps.empty()) << "gaps: " << gaps.size();
    auto* panel = openAddPanel(rig, *editor);
    ASSERT_NE(panel, nullptr);
    for (const auto& gap : synth::test::auditAccessibility(*panel))
        ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "missing name: " : "missing tooltip: ")
                      << gap.path;
    EXPECT_EQ(panel->getSearchFieldForTest().getTitle(), "Search hidden controls");
    EXPECT_TRUE(panel->getRowForTest("drive")->getTooltip().isNotEmpty());
}

TEST(OnCardAddControl, TheBarFitsAStandardCardsHeader) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto& bar = editor->getEditBarForTest();
    EXPECT_GE(bar.getX(), 0) << "all four buttons are inside the card";
    for (auto* button : {&bar.getPresetButton(), &bar.getApplyToButton(), &bar.getCancelButton(), &bar.getDoneButton()})
        EXPECT_TRUE(bar.getLocalBounds().contains(button->getBounds())) << button->getTitle();
}
