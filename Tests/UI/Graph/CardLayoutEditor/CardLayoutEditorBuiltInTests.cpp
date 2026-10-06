// CardLayoutEditorBuiltInTests.cpp
//
// The list editor over a built-in module's source (BuiltInCardLayoutSource), built directly: no menu
// opens it for a built-in card any more (the on-card editor does), but the hosted plugin's editor is the
// same component. Each edit is checked on the rebuilt card itself. The per-type store's binding to the
// canvas's cards is tested here too.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardLayoutEditorTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"

using namespace cardlayouteditor_test;
using synth::CardWidget;

namespace {

// A Filter whose card draws the automatic layout written out as one stored section (the Filter's own code
// default has several sections and a footer), so the group tests below see one group to start from.
NodeID addAutomaticFilter(EditorCanvas& rig) {
    const auto id = rig.add(std::make_unique<FilterModule>());
    EXPECT_TRUE(synth::setCardLayoutOverride(rig.canvas.engine.getGraph(), nullptr, id,
                                             cardbody_test::automaticLayoutWith(*rig.canvas.processor(id), {})));
    rig.canvas.editor.updateComponents();
    return id;
}

} // namespace

TEST(CardLayoutEditorBuiltIn, UntickingHidesTheControlInTheMoreRowLiveAndTickingBringsItBack) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openList(id);
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
    auto* editor = rig.openList(id);
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
    auto* editor = rig.openList(id);
    ASSERT_NE(editor, nullptr);

    auto choicesOf = [&](const juce::String& key) {
        juce::StringArray items;
        auto& combo = editor->getRowForTest(rowOf(*editor, key))->getWidgetComboForTest();
        for (int i = 0; i < combo.getNumItems(); ++i)
            items.add(combo.getItemText(i));
        return combo.isVisible() ? items : juce::StringArray();
    };
    EXPECT_EQ(choicesOf("outputLevel"),
              juce::StringArray("Small knob", "Large knob", "Vertical fader", "Horizontal fader"));
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
    auto* editor = rig.openList(id);
    ASSERT_NE(editor, nullptr);
    auto& combo = editor->getRowForTest(rowOf(*editor, "waveform"))->getWidgetComboForTest();
    ASSERT_TRUE(combo.isVisible());
    EXPECT_EQ(combo.getItemText(0), "Menu");
    EXPECT_EQ(combo.getItemText(1), "Segmented");
}

TEST(CardLayoutEditorBuiltIn, AddGroupAndATitleDrawAHeaderRowOnTheCardAndMeasureMatchesApply) {
    EditorCanvas rig;
    const auto id = addAutomaticFilter(rig);
    const int heightBefore = rig.card(id)->getHeight();
    auto* editor = rig.openList(id);
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
    EXPECT_EQ(card->getCardBody()->layout(0, g, false), card->getCardBody()->layout(0, g, true))
        << "measure == apply with a header row";
    const int headerY = plan.sections[1].header->getY();
    const auto* lastWidget = card->getCardBody()->findWidget(lastKey);
    ASSERT_NE(lastWidget, nullptr);
    EXPECT_GT(lastWidget->getY(), headerY) << "the moved control sits under its group's header";
}

TEST(CardLayoutEditorBuiltIn, ADragAcrossAGroupHeaderMovesTheControlIntoThatGroup) {
    EditorCanvas rig;
    const auto id = addAutomaticFilter(rig);
    auto* editor = rig.openList(id);
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
    const auto id = addAutomaticFilter(rig);
    auto* editor = rig.openList(id);
    ASSERT_NE(editor, nullptr);
    editor->setSearchTextForTest("res");
    EXPECT_EQ(editor->getVisibleRowCountForTest(), 2);
    EXPECT_EQ(editor->getVisibleRowParamIdForTest(0), "#0");
    EXPECT_EQ(editor->getVisibleRowParamIdForTest(1), "resonance");
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

// Every group a card's layout has gets a real name, on every module type the library offers: an
// untitled section is named from its id, never "Untitled group".
TEST(CardLayoutEditorBuiltIn, EveryGroupOnEveryModuleTypeHasARealName) {
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const char* extra : {"Amp Env", "Filter Env"})
        types.addIfNotAlreadyThere(extra);
    int groupsChecked = 0;
    for (const auto& type : types) {
        auto processor = synth::AIStateMapper::createModule(type);
        if (processor == nullptr)
            continue;
        EditorCanvas rig;
        const auto id = rig.add(std::move(processor));
        const auto* body = rig.card(id) != nullptr ? rig.card(id)->getCardBody() : nullptr;
        if (body == nullptr || !body->drawsFromLayout())
            continue; // a bespoke card with no layout to edit
        for (const auto& section : body->explicitLayout().sections) {
            ++groupsChecked;
            const auto name = synth::cardSectionDisplayName(section);
            EXPECT_TRUE(name.trim().isNotEmpty()) << type;
            EXPECT_FALSE(name.containsIgnoreCase("untitled")) << type << ": " << name;
        }
    }
    EXPECT_GT(groupsChecked, 20) << "the walk reached the module types' groups";
}
