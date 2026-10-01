// CardLayoutQuickEditTests.cpp
//
// Source/UI/Graph/CardBody/CardLayoutQuickEdit: the right-click quick path, end to end through the real
// handlers -- a synthesized right click on a real card control, the item picked from the menu the card
// built, then the node's override, the rebuilt card and a single undo that puts everything back.

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardBody/CardLayoutQuickEdit.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"

using namespace cardbody_test;
using synth::CardQuickEdit;

namespace {

std::optional<synth::CardLayout> storedLayout(CardCanvas& canvas, NodeID id) {
    const auto json = synth::getCardLayoutOverride(canvas.engine.getGraph(), id);
    if (json.isVoid())
        return std::nullopt;
    const auto parsed = synth::CardLayout::fromVar(json);
    EXPECT_EQ(parsed.status, synth::CardLayout::ParseStatus::Ok);
    // A layout with nothing beyond a flat list is stored in the v1 form; read it back as sections.
    return synth::upgradeV1(parsed.layout, {});
}

const synth::CardParamItem* placedItem(const synth::CardLayout& layout, const juce::String& paramId) {
    for (const auto& section : layout.sections)
        for (const auto& item : section.items)
            if (const auto* p = std::get_if<synth::CardParamItem>(&item); p != nullptr && p->paramId == paramId)
                return p;
    return nullptr;
}

// Right-clicks `paramId`'s control on the node's card and runs the menu item titled `text`.
void pickFromControlMenu(CardCanvas& canvas, NodeID id, const juce::String& paramId, const juce::String& text) {
    auto* card = canvas.card(id);
    ASSERT_NE(card, nullptr);
    auto* control = card->getCardBody()->findWidget(paramId);
    ASSERT_NE(control, nullptr);
    const auto menu = rightClickOnCard(*card, *control);
    const auto* item = menuItem(menu, text);
    ASSERT_NE(item, nullptr) << text << " is offered";
    ASSERT_TRUE(item->isEnabled);
    item->action();
}

std::map<juce::String, juce::Rectangle<int>> boundsById(ModuleComponent& card) {
    std::map<juce::String, juce::Rectangle<int>> bounds;
    for (const auto& item : card.getCardBody()->getPlan().items)
        if (item.param != nullptr && item.widget != nullptr && item.widget->isVisible())
            bounds[item.param->paramID] = item.widget->getBounds();
    return bounds;
}

} // namespace

TEST(CardLayoutQuickEdit, RightClickHideFromCardFoldsTheKnobAwayAndOneUndoBringsItBack) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<FilterModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* before = canvas.card(id);
    const auto shown = boundsById(*before);
    const int height = before->getHeight();
    ASSERT_FALSE(storedLayout(canvas, id).has_value());

    pickFromControlMenu(canvas, id, "cutoff", "Hide from card");

    const auto layout = storedLayout(canvas, id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_TRUE(layout->hidden.contains("cutoff"));
    EXPECT_NE(placedItem(*layout, "cutoff"), nullptr) << "the item keeps its place for Show on card";
    auto* card = canvas.card(id);
    auto* body = card->getCardBody();
    ASSERT_TRUE(body->hasMoreRow());
    EXPECT_FALSE(body->findWidget("cutoff")->isVisible());
    EXPECT_NE(card->getHeight(), height);

    ASSERT_TRUE(canvas.undo.undo());
    EXPECT_FALSE(storedLayout(canvas, id).has_value()) << "one undo removes the override";
    auto* restored = canvas.card(id);
    EXPECT_FALSE(restored->getCardBody()->hasMoreRow());
    EXPECT_EQ(boundsById(*restored), shown);
    EXPECT_EQ(restored->getHeight(), height);

    ASSERT_TRUE(canvas.undo.redo());
    EXPECT_TRUE(canvas.card(id)->getCardBody()->hasMoreRow());
}

TEST(CardLayoutQuickEdit, ShowOnCardFromTheMoreRowPutsTheControlBackWhereItWas) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<FilterModule>(), 0, 0);
    canvas.editor.updateComponents();
    const auto shown = boundsById(*canvas.card(id));

    pickFromControlMenu(canvas, id, "resonance", "Hide from card");
    canvas.card(id)->getCardBody()->setMoreUnfolded(true);
    pickFromControlMenu(canvas, id, "resonance", "Show on card");

    const auto layout = storedLayout(canvas, id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_FALSE(layout->hidden.contains("resonance"));
    EXPECT_FALSE(canvas.card(id)->getCardBody()->hasMoreRow());
    EXPECT_EQ(boundsById(*canvas.card(id)), shown) << "back exactly where the layout had it";

    ASSERT_TRUE(canvas.undo.undo());
    EXPECT_TRUE(storedLayout(canvas, id)->hidden.contains("resonance")) << "one undo per click";
}

TEST(CardLayoutQuickEdit, ShowOnCardPlacesAParameterTheLayoutNeverPlacedAtTheEnd) {
    synth::CardLayout layout;
    synth::CardSection section;
    section.id = "main";
    synth::CardParamItem cutoff;
    cutoff.paramId = "cutoff";
    section.items.emplace_back(cutoff);
    layout.sections = {section};
    const auto shown = synth::applyCardQuickEdit(layout, "drive", CardQuickEdit::ShowOnCard);
    ASSERT_EQ(shown.sections.back().items.size(), 2u);
    EXPECT_EQ(std::get<synth::CardParamItem>(shown.sections.back().items.back()).paramId, "drive");
}

TEST(CardLayoutQuickEdit, ShowAsFaderThenShowAsKnobSwitchesTheWidgetAndMakesRoom) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<FilterModule>(), 0, 0);
    const auto belowId = canvas.add(std::make_unique<LFOModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* filterCard = canvas.card(id);
    const int height = filterCard->getHeight();
    // A neighbour right under the card, as close as layout allows.
    const int belowY = filterCard->getBottom() + 8;
    canvas.engine.getGraph().getNodeForId(belowId)->properties.set("y", belowY);
    canvas.engine.getGraph().getNodeForId(belowId)->properties.set("x", 0);
    canvas.editor.updateComponents();
    ASSERT_EQ(canvas.card(belowId)->getY(), belowY);
    ASSERT_NE(dynamic_cast<synth::ui::CardKnobSlider*>(filterCard->getCardBody()->findWidget("cutoff")), nullptr);

    pickFromControlMenu(canvas, id, "cutoff", "Show as fader");

    const auto layout = storedLayout(canvas, id);
    ASSERT_TRUE(layout.has_value());
    ASSERT_NE(placedItem(*layout, "cutoff"), nullptr);
    EXPECT_EQ(placedItem(*layout, "cutoff")->widget, synth::CardWidget::FaderV);
    auto* card = canvas.card(id);
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(card->getCardBody()->findWidget("cutoff")), nullptr);
    EXPECT_GT(card->getHeight(), height) << "a fader row is taller than a knob row";
    EXPECT_GE(canvas.card(belowId)->getY(), card->getBottom()) << "the neighbour was pushed clear";

    ASSERT_TRUE(canvas.undo.undo());
    EXPECT_FALSE(storedLayout(canvas, id).has_value());
    EXPECT_NE(dynamic_cast<synth::ui::CardKnobSlider*>(canvas.card(id)->getCardBody()->findWidget("cutoff")), nullptr);
    EXPECT_EQ(canvas.card(id)->getHeight(), height);
    EXPECT_EQ(canvas.card(belowId)->getY(), belowY) << "the same undo step puts the neighbour back";

    ASSERT_TRUE(canvas.undo.redo());
    ASSERT_TRUE(storedLayout(canvas, id).has_value()) << "redo writes the fader back";
    pickFromControlMenu(canvas, id, "cutoff", "Show as knob");
    const auto knobLayout = storedLayout(canvas, id);
    ASSERT_TRUE(knobLayout.has_value());
    ASSERT_NE(placedItem(*knobLayout, "cutoff"), nullptr);
    EXPECT_EQ(placedItem(*knobLayout, "cutoff")->widget, synth::CardWidget::Knob);
    EXPECT_NE(dynamic_cast<synth::ui::CardKnobSlider*>(canvas.card(id)->getCardBody()->findWidget("cutoff")), nullptr);
}

TEST(CardLayoutQuickEdit, EveryCardControlMenuOffersTheLayoutItemsAndADisabledEditLayout) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    for (const auto& item : card->getCardBody()->getPlan().items) {
        if (item.param == nullptr || item.widget == nullptr)
            continue;
        SCOPED_TRACE(item.param->paramID.toStdString());
        const auto menu = rightClickOnCard(*card, *item.widget);
        EXPECT_NE(menuItem(menu, "Hide from card"), nullptr);
        const auto* edit = menuItem(menu, "Edit Layout...");
        ASSERT_NE(edit, nullptr);
        EXPECT_FALSE(edit->isEnabled) << "the layout editor is not built yet";
        EXPECT_EQ(menuItem(menu, "Show as fader") != nullptr, synth::isContinuousKind(item.kind));
    }

    const auto moduleMenu = card->buildModuleContextMenu();
    const auto* edit = menuItem(moduleMenu, "Edit Layout...");
    ASSERT_NE(edit, nullptr);
    EXPECT_FALSE(edit->isEnabled);
}

TEST(CardLayoutQuickEdit, BespokeCardsOfferNoLayoutItems) {
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("Sequencer"), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    ASSERT_NE(card, nullptr);
    for (const auto& item : card->getCardBody()->getPlan().items)
        if (item.param != nullptr && item.widget != nullptr) {
            EXPECT_EQ(menuItem(rightClickOnCard(*card, *item.widget), "Hide from card"), nullptr);
            break;
        }
    EXPECT_EQ(menuItem(card->buildModuleContextMenu(), "Edit Layout..."), nullptr);
}

// A layout stored in the v1 form hides, on read, every id its slots did not name (the header buttons,
// the ones edited elsewhere); the quick path must not carry those into the next layout it writes.
TEST(CardLayoutQuickEdit, ALayoutReadBackFromV1CarriesNoHiddenIdsTheCardDoesNotShow) {
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("ADSR"), 0, 0);
    canvas.editor.updateComponents();
    pickFromControlMenu(canvas, id, "attack", "Show as fader");
    pickFromControlMenu(canvas, id, "attack", "Show as knob"); // back to a plain, v1-storable layout
    const auto layout = canvas.card(id)->getCardBody()->explicitLayout();
    EXPECT_TRUE(layout.hidden.isEmpty()) << layout.hidden.joinIntoString(", ");
    pickFromControlMenu(canvas, id, "decay", "Hide from card");
    const auto stored = storedLayout(canvas, id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->hidden, juce::StringArray("decay"));
}
