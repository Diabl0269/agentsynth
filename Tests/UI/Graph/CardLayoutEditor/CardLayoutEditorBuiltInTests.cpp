// CardLayoutEditorBuiltInTests.cpp
//
// The card layout editor over a built-in module (BuiltInCardLayoutSource), always opened through the
// real right-click path: a synthesized right click on a real card control (or the module menu), the
// "Edit Layout..." item the card built, and the editor that click launched. Each edit is checked on
// the rebuilt card itself, and the session's single undo step.

#include "CardLayoutEditorTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"

using namespace cardlayouteditor_test;
using synth::CardWidget;

TEST(CardLayoutEditorBuiltIn, EditLayoutFromAControlAndFromTheModuleMenuOpensTheEditor) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());

    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr) << "the control's Edit Layout... launched the editor";
    EXPECT_EQ(editor->getSource().title(), "Filter layout");
    for (const auto* id2 : {"cutoff", "resonance", "drive", "filterType", "poly", "outputLevel"})
        EXPECT_GE(editor->findRowForTest(id2), 0) << id2;
    EXPECT_EQ(editor->findRowForTest("bypassed"), -1) << "a header button is not a card control";
    EXPECT_EQ(editor->findRowForTest("#0"), 0) << "rows are grouped, the first group's header first";
    rig.close();

    EXPECT_NE(rig.openFromModuleMenu(id), nullptr) << "the module menu's Edit Layout... opens it too";
}

TEST(CardLayoutEditorBuiltIn, UntickingHidesTheControlInTheMoreRowLiveAndTickingBringsItBack) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "drive");
    ASSERT_NE(editor, nullptr);

    editor->triggerRowToggleForTest(rowOf(*editor, "drive"));
    ASSERT_TRUE(rig.storedLayout(id).has_value());
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("drive"));
    auto* card = rig.card(id);
    EXPECT_TRUE(card->getCardBody()->hasMoreRow()) << "the card re-laid out while the editor is open";
    EXPECT_FALSE(card->getCardBody()->findWidget("drive")->isVisible());
    EXPECT_FALSE(editor->getVisibleRowCheckedForTest(rowOf(*editor, "drive"))) << "the row keeps its place";

    editor->triggerRowToggleForTest(rowOf(*editor, "drive"));
    EXPECT_FALSE(rig.card(id)->getCardBody()->hasMoreRow());
}

TEST(CardLayoutEditorBuiltIn, RenamingAControlChangesItsCaptionOnTheCard) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);

    const int row = rowOf(*editor, "cutoff");
    editor->setRowLabelForTest(row, " Freq ");
    editor->commitRowLabelForTest(row);
    EXPECT_EQ(editor->getVisibleRowLabelForTest(row), "Freq");
    EXPECT_EQ(captionOf(*rig.card(id), "cutoff"), "Freq");
    auto* caption =
        dynamic_cast<juce::Label*>(rig.card(id)
                                       ->getCardBody()
                                       ->getPlan()
                                       .items[(size_t)rig.card(id)->getCardBody()->getPlan().findParam("cutoff")]
                                       .label);
    ASSERT_NE(caption, nullptr);
    EXPECT_EQ(caption->getTooltip(), "Cutoff") << "a renamed control keeps its full name in the tooltip";
    EXPECT_EQ(rig.card(id)->getCardBody()->findWidget("cutoff")->getComponentID(), "Cutoff")
        << "the widget is still found by its parameter's name";

    editor->setRowLabelForTest(row, "");
    editor->commitRowLabelForTest(row);
    EXPECT_EQ(captionOf(*rig.card(id), "cutoff"), "Cutoff") << "an empty name puts the parameter's own back";
}

TEST(CardLayoutEditorBuiltIn, TheWidgetChoiceOffersOnlyWidgetsThatSuitTheParameter) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);

    auto choicesOf = [&](const juce::String& key) {
        juce::StringArray items;
        auto& combo = editor->getRowForTest(rowOf(*editor, key))->getWidgetComboForTest();
        for (int i = 0; i < combo.getNumItems(); ++i)
            items.add(combo.getItemText(i));
        return combo.isVisible() ? items : juce::StringArray();
    };
    EXPECT_EQ(choicesOf("outputLevel"), juce::StringArray("Knob", "Large knob", "Vertical fader", "Horizontal fader"));
    EXPECT_EQ(choicesOf("filterType"), juce::StringArray()) << "seven values: a menu only, no choice to offer";
    EXPECT_EQ(choicesOf("poly"), juce::StringArray()) << "a switch is only ever a toggle";

    editor->selectWidgetForTest(rowOf(*editor, "outputLevel"), CardWidget::FaderH);
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(rig.card(id)->getCardBody()->findWidget("outputLevel")), nullptr);
    editor->selectWidgetForTest(rowOf(*editor, "outputLevel"), CardWidget::Knob);
    EXPECT_NE(dynamic_cast<synth::ui::CardKnobSlider*>(rig.card(id)->getCardBody()->findWidget("outputLevel")),
              nullptr);
}

TEST(CardLayoutEditorBuiltIn, ASegmentedChoiceIsOfferedForAShortChoice) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<OscillatorModule>());
    auto* editor = rig.openFromModuleMenu(id);
    ASSERT_NE(editor, nullptr);
    auto& combo = editor->getRowForTest(rowOf(*editor, "waveform"))->getWidgetComboForTest();
    ASSERT_TRUE(combo.isVisible());
    EXPECT_EQ(combo.getItemText(0), "Menu");
    EXPECT_EQ(combo.getItemText(1), "Segmented");
}

TEST(CardLayoutEditorBuiltIn, AddGroupAndATitleDrawAHeaderRowOnTheCardAndMeasureMatchesApply) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    const int heightBefore = rig.card(id)->getHeight();
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);

    editor->triggerAddGroupForTest();
    const int header = rowOf(*editor, "#1");
    editor->setRowLabelForTest(header, "Tone");
    editor->commitRowLabelForTest(header);
    // The new group is last; the last control of the first group steps down into it.
    const int last = rowOf(*editor, "#1") - 1;
    const auto lastKey = editor->getVisibleRowParamIdForTest(last);
    ASSERT_TRUE(editor->pressKeyOnRowForTest(
        last, juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::commandModifier, 0)));
    EXPECT_GT(editor->findRowForTest(lastKey), editor->findRowForTest("#1")) << lastKey;

    auto* card = rig.card(id);
    EXPECT_EQ(sectionHeadersOf(*card), juce::StringArray("Tone")) << "sentence case, as typed";
    EXPECT_GT(card->getHeight(), heightBefore) << "the header row adds height";
    const auto& plan = card->getCardBody()->getPlan();
    ASSERT_EQ(plan.sections.size(), 2u);
    ASSERT_NE(plan.sections[1].header, nullptr);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(card->getWidth());
    EXPECT_EQ(card->getCardBody()->layout(0, g, false, false), card->getCardBody()->layout(0, g, true, false))
        << "measure == apply with a header row";
    const int headerY = plan.sections[1].header->getY();
    const auto* lastWidget = card->getCardBody()->findWidget(lastKey);
    ASSERT_NE(lastWidget, nullptr);
    EXPECT_GT(lastWidget->getY(), headerY) << "the moved control sits under its group's header";
}

TEST(CardLayoutEditorBuiltIn, ADragAcrossAGroupHeaderMovesTheControlIntoThatGroup) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);
    editor->triggerAddGroupForTest();

    const int from = rowOf(*editor, "cutoff");
    auto* handle = editor->getRowDragHandleForTest(from);
    ASSERT_NE(handle, nullptr);
    auto& content = editor->getRowsContentForTest();
    const float pressY = (float)editor->getRowBoundsForTest(from).getCentreY();
    const float dropY = (float)editor->getRowBoundsForTest(rowOf(*editor, "#1")).getBottom() + 4.0f;
    const auto at = [&](float listY) {
        const float x = content.getLocalPoint(handle, handle->getLocalBounds().toFloat().getCentre()).x;
        return handle->getLocalPoint(&content, juce::Point<float>(x, listY));
    };
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    handle->mouseDown(cardbody_test::mouseAt(*handle, at(pressY), mods));
    handle->mouseDrag(cardbody_test::mouseAt(*handle, at((pressY + dropY) / 2.0f), mods));
    handle->mouseDrag(cardbody_test::mouseAt(*handle, at(dropY), mods));
    handle->mouseUp(cardbody_test::mouseAt(*handle, at(dropY), juce::ModifierKeys()));

    EXPECT_GT(editor->findRowForTest("cutoff"), editor->findRowForTest("#1"));
    const auto stored = rig.storedLayout(id);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->sections.size(), 2u);
    ASSERT_FALSE(stored->sections[1].items.empty());
    EXPECT_EQ(std::get<synth::CardParamItem>(stored->sections[1].items.front()).paramId, "cutoff");
}

TEST(CardLayoutEditorBuiltIn, SearchFiltersTheRowsButKeepsTheGroupHeaders) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);
    editor->setSearchTextForTest("res");
    EXPECT_EQ(editor->getVisibleRowCountForTest(), 2);
    EXPECT_EQ(editor->getVisibleRowParamIdForTest(0), "#0");
    EXPECT_EQ(editor->getVisibleRowParamIdForTest(1), "resonance");
}

TEST(CardLayoutEditorBuiltIn, AWholeEditingSessionIsOneUndoStep) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    const int heightBefore = rig.card(id)->getHeight();
    const int serialBefore = rig.canvas.undo.getEditSerial();
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);
    editor->triggerRowToggleForTest(rowOf(*editor, "drive"));
    editor->selectWidgetForTest(rowOf(*editor, "outputLevel"), CardWidget::FaderV);
    editor->setRowLabelForTest(rowOf(*editor, "cutoff"), "Freq");
    editor->commitRowLabelForTest(rowOf(*editor, "cutoff"));
    EXPECT_EQ(rig.canvas.undo.getEditSerial(), serialBefore) << "nothing is recorded while the editor is open";

    rig.close();
    EXPECT_EQ(rig.canvas.undo.getEditSerial(), serialBefore + 1) << "closing records the session once";

    ASSERT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(rig.storedLayout(id).has_value()) << "one undo takes every edit of the session back";
    EXPECT_EQ(captionOf(*rig.card(id), "cutoff"), "Cutoff");
    EXPECT_FALSE(rig.card(id)->getCardBody()->hasMoreRow());
    EXPECT_EQ(rig.card(id)->getHeight(), heightBefore);

    ASSERT_TRUE(rig.canvas.undo.redo());
    EXPECT_EQ(captionOf(*rig.card(id), "cutoff"), "Freq");
}

TEST(CardLayoutEditorBuiltIn, ASessionThatChangesNothingRecordsNoUndoStep) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    const int serialBefore = rig.canvas.undo.getEditSerial();
    ASSERT_NE(rig.openFromControl(id, "cutoff"), nullptr);
    rig.close();
    EXPECT_EQ(rig.canvas.undo.getEditSerial(), serialBefore);
}

TEST(CardLayoutEditorBuiltIn, ApplyToAllWritesTheTypeDefaultClearsThisOverrideAndAnotherFilterFollows) {
    EditorCanvas rig;
    const auto first = rig.add(std::make_unique<FilterModule>(), 0, 0);
    const auto second = rig.add(std::make_unique<FilterModule>(), 0, 600);
    // This module starts with a layout of its own (Level as a fader), which Apply to all clears.
    ASSERT_TRUE(synth::setCardLayoutOverride(
        rig.canvas.engine.getGraph(), nullptr, first,
        cardbody_test::automaticLayoutWith(*rig.canvas.processor(first), {{"outputLevel", CardWidget::FaderV}})));
    rig.canvas.editor.updateComponents();
    auto* editor = rig.openFromControl(first, "drive");
    ASSERT_NE(editor, nullptr);
    editor->triggerRowToggleForTest(rowOf(*editor, "drive")); // this module only, so far
    ASSERT_TRUE(rig.storedLayout(first).has_value());
    EXPECT_FALSE(rig.card(second)->getCardBody()->hasMoreRow());

    editor->setApplyToAllInstancesForTest(true);
    EXPECT_FALSE(rig.storedLayout(first).has_value()) << "this module now follows the type's default";
    const auto stored = rig.store.loadDefault("Filter");
    ASSERT_EQ(stored.status, synth::ModuleCardLayoutStore::LoadStatus::Ok);
    EXPECT_TRUE(stored.layout.hidden.contains("drive"));
    EXPECT_TRUE(rig.card(first)->getCardBody()->hasMoreRow());
    EXPECT_TRUE(rig.card(second)->getCardBody()->hasMoreRow()) << "the other Filter was re-laid out at once";
    EXPECT_FALSE(rig.card(second)->getCardBody()->findWidget("drive")->isVisible());

    const int serial = rig.canvas.undo.getEditSerial();
    rig.close();
    EXPECT_EQ(rig.canvas.undo.getEditSerial(), serial + 1) << "the override write and its clearing: one step";
    ASSERT_TRUE(rig.canvas.undo.undo());
    ASSERT_TRUE(rig.storedLayout(first).has_value()) << "undo gives this module its own layout back";
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(rig.card(first)->getCardBody()->findWidget("outputLevel")), nullptr);
    EXPECT_TRUE(rig.card(second)->getCardBody()->hasMoreRow()) << "the type's default file is a setting: it stays";
    ASSERT_TRUE(rig.canvas.undo.redo());

    const auto third = rig.add(std::make_unique<FilterModule>(), 600, 0);
    EXPECT_TRUE(rig.card(third)->getCardBody()->hasMoreRow()) << "a new Filter starts from the type's default";
}

TEST(CardLayoutEditorBuiltIn, ATypeDefaultWrittenElsewhereRelaysOutTheOpenCardsOfThatTypeOnly) {
    EditorCanvas rig;
    const auto filter = rig.add(std::make_unique<FilterModule>(), 0, 0);
    const auto osc = rig.add(std::make_unique<OscillatorModule>(), 600, 0);
    auto* oscCardBefore = rig.card(osc);
    auto* filterCard = rig.card(filter);
    const auto layout = cardbody_test::automaticLayoutHiding(*rig.canvas.processor(filter), {"resonance"});

    ASSERT_TRUE(rig.store.setDefault("Filter", layout));
    EXPECT_NE(rig.card(filter), filterCard) << "the Filter card was rebuilt";
    EXPECT_TRUE(rig.card(filter)->getCardBody()->hasMoreRow());
    EXPECT_EQ(rig.card(osc), oscCardBefore) << "an Oscillator card is left alone";

    ASSERT_TRUE(rig.store.clearDefault("Filter"));
    EXPECT_FALSE(rig.card(filter)->getCardBody()->hasMoreRow()) << "clearing the default re-lays it out too";
}

TEST(CardLayoutEditorBuiltIn, AnInstanceOverrideWinsOverTheTypeDefault) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* processor = rig.canvas.processor(id);
    ASSERT_TRUE(synth::setCardLayoutOverride(rig.canvas.engine.getGraph(), nullptr, id,
                                             cardbody_test::automaticLayoutHiding(*processor, {"drive"})));
    rig.canvas.editor.updateComponents();
    ASSERT_TRUE(rig.store.setDefault("Filter", cardbody_test::automaticLayoutHiding(*processor, {"cutoff"})));
    EXPECT_FALSE(rig.card(id)->getCardBody()->findWidget("drive")->isVisible());
    EXPECT_TRUE(rig.card(id)->getCardBody()->findWidget("cutoff")->isVisible());
}

TEST(CardLayoutEditorBuiltIn, PresetsSaveLoadAndDeleteAndResetGoesBackToTheDefault) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);

    editor->triggerRowToggleForTest(rowOf(*editor, "drive"));
    editor->triggerSaveAsPresetForTest("No drive");
    EXPECT_TRUE(editor->getPresetNamesForTest().contains("No drive"));
    EXPECT_TRUE(rig.store.listPresets("Filter").contains("No drive"));

    editor->triggerResetToAutomaticForTest();
    EXPECT_FALSE(rig.storedLayout(id).has_value());
    EXPECT_FALSE(rig.card(id)->getCardBody()->hasMoreRow());
    EXPECT_TRUE(editor->getVisibleRowCheckedForTest(rowOf(*editor, "drive")));

    editor->selectPresetForTest("No drive");
    ASSERT_TRUE(rig.storedLayout(id).has_value());
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("drive"));
    EXPECT_FALSE(editor->getVisibleRowCheckedForTest(rowOf(*editor, "drive")));

    editor->triggerDeletePresetForTest("No drive");
    EXPECT_FALSE(editor->getPresetNamesForTest().contains("No drive"));
}

TEST(CardLayoutEditorBuiltIn, ResetForAllModulesClearsTheTypeDefaultToo) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);
    editor->setApplyToAllInstancesForTest(true);
    editor->triggerRowToggleForTest(rowOf(*editor, "drive"));
    ASSERT_EQ(rig.store.loadDefault("Filter").status, synth::ModuleCardLayoutStore::LoadStatus::Ok);

    editor->triggerResetToAutomaticForTest();
    EXPECT_NE(rig.store.loadDefault("Filter").status, synth::ModuleCardLayoutStore::LoadStatus::Ok);
    EXPECT_FALSE(rig.card(id)->getCardBody()->hasMoreRow());
}
