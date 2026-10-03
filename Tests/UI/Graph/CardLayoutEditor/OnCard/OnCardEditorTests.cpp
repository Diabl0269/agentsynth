// OnCardEditorTests.cpp -- opening the on-card layout editor from the real module menu: one accessible
// outline per control over the card, the card's positions surviving the move to free placement, the
// overlay following the card a write rebuilds, and leaving the canvas when it closes.

#include "../../../Accessibility/AccessibilityAudit.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"

using namespace oncard_test;

namespace {

// What the Filter card draws as controls: every item with a visible widget, the footer's included.
juce::StringArray visibleControls(ModuleComponent& card) {
    juce::StringArray ids;
    const auto& plan = card.getCardBody()->getPlan();
    for (const auto& item : plan.items)
        if (item.param != nullptr && item.widget != nullptr && item.widget->isVisible() && item.section >= 0)
            ids.add(item.param->paramID);
    return ids;
}

} // namespace

TEST(OnCardEditor, EditLayoutFromTheModuleMenuOpensOverTheCardWithAnOutlinePerControl) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 200, 200);
    auto* card = rig.card(id);
    const auto expected = visibleControls(*card);
    ASSERT_GE(expected.size(), 5);

    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->getParentComponent(), card->getParentComponent()) << "a sibling of the card, not its child";
    EXPECT_EQ(editor->getBounds().withHeight(card->getHeight()), card->getBounds())
        << "over the card, and the strip with its Add control under it";
    EXPECT_EQ(editor->getOutlineCountForTest(), expected.size());
    for (const auto& paramId : expected) {
        SCOPED_TRACE(paramId.toStdString());
        auto* outline = editor->getOutlineForTest(paramId);
        ASSERT_NE(outline, nullptr);
        const bool panelOnly = outline->isPanelOnly();
        EXPECT_EQ(outline->getTitle(),
                  cardlayouteditor_test::captionOf(*card, paramId) +
                      (panelOnly ? ", layout: Return for options" : ", layout: drag to move, Return for options"));
        EXPECT_EQ(outline->getTooltip(),
                  panelOnly ? "Right-click for options"
                            : "Drag to move (arrow keys nudge, Shift for 8px). Right-click for options");
        EXPECT_TRUE(outline->getWantsKeyboardFocus());
    }
    for (const auto* id2 : {"cutoff", "resonance", "drive", "outputLevel"})
        EXPECT_NE(editor->getOutlineForTest(id2), nullptr) << id2;
    ASSERT_NE(editor->getOutlineForTest("poly"), nullptr) << "the footer's controls get one too";
    EXPECT_TRUE(editor->getOutlineForTest("poly")->isPanelOnly());
    EXPECT_FALSE(editor->getOutlineForTest("cutoff")->isPanelOnly());
}

TEST(OnCardEditor, EditLayoutFromAControlsMenuOpensTheSameEditor) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* card = rig.card(id);
    const auto menu = cardbody_test::rightClickOnCard(*card, *widgetOf(*card, "cutoff"));
    const auto* item = cardbody_test::menuItem(menu, "Edit Layout...");
    ASSERT_NE(item, nullptr);
    item->action();
    EXPECT_NE(dynamic_cast<CardLayoutOnCardEditor*>(rig.launched.get()), nullptr);
}

TEST(OnCardEditor, EveryOutlineIsAKeyboardReachableNamedControlWithATooltipAndTheBarIsNamed) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->getEditBarForTest().getCancelButton().getButtonText(), "Cancel");
    EXPECT_EQ(editor->getEditBarForTest().getDoneButton().getButtonText(), "Done");
    EXPECT_EQ(editor->getEditBarForTest().getDoneButton().getTooltip(), "Keep the layout");
    EXPECT_EQ(editor->getEditBarForTest().getCancelButton().getTooltip(), "Undo everything since Edit layout (Esc)");

    const auto gaps = synth::test::auditAccessibility(*editor);
    EXPECT_TRUE(gaps.empty()) << "gaps: " << gaps.size();
}

// The first write puts a position on every control of the section; each must land exactly where the
// flowing layout had it, or the whole card would jump on the first drag.
TEST(OnCardEditor, FreePlacementOfAFlowingSectionLeavesEveryControlWhereItWas) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* card = rig.card(id);
    auto before = synth::ui::collectCells(*card);
    std::erase_if(before, [](const synth::ui::OnCardCell& cell) { return cell.panelOnly; });
    ASSERT_GE(before.size(), 5u);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(card->getWidth());

    auto layout = card->getCardBody()->explicitLayout();
    const auto& plan = card->getCardBody()->getPlan();
    std::vector<int> relativeY;
    for (const auto& cell : before)
        relativeY.push_back(cell.rect.getY() - plan.sections[(size_t)cell.section].cellTop);
    for (int section = 0; section < (int)plan.sections.size(); ++section) {
        std::vector<synth::ui::OnCardCell> cells;
        for (const auto& cell : before)
            if (cell.section == section)
                cells.push_back(cell);
        layout = synth::ui::withCellPositions(layout, cells, {}, g.contentX, plan.sections[(size_t)section].cellTop);
    }
    synth::ui::BuiltInCardLayoutSource source(rig.canvas.editor, nullptr, id);
    source.apply(layout, false);

    auto* after = rig.card(id);
    ASSERT_NE(after, nullptr);
    auto placed = synth::ui::collectCells(*after);
    std::erase_if(placed, [](const synth::ui::OnCardCell& cell) { return cell.panelOnly; });
    ASSERT_EQ(placed.size(), before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        SCOPED_TRACE(before[i].key.toStdString());
        EXPECT_EQ(placed[i].key, before[i].key);
        // A positioned group is 6 px taller than a flowing one, so the groups under it sit a little lower.
        EXPECT_EQ(placed[i].rect.getX(), before[i].rect.getX());
        EXPECT_EQ(placed[i].rect.getWidth(), before[i].rect.getWidth());
        EXPECT_EQ(placed[i].rect.getHeight(), before[i].rect.getHeight());
        EXPECT_EQ(placed[i].widgetRel, before[i].widgetRel);
        EXPECT_EQ(placed[i].rect.getY() - after->getCardBody()->getPlan().sections[(size_t)placed[i].section].cellTop,
                  relativeY[i]);
    }
}

TEST(OnCardEditor, AfterAWriteTheOverlayFollowsTheRebuiltCardAndKeepsItsOutlines) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* cardBefore = rig.card(id);
    const int count = editor->getOutlineCountForTest();
    auto* outlineBefore = editor->getOutlineForTest("cutoff");

    Pointer pointer(*editor, "cutoff");
    pointer.moveBy({40, 90}, juce::ModifierKeys::commandModifier);
    pointer.release();

    auto* cardAfter = rig.card(id);
    ASSERT_NE(cardAfter, nullptr);
    EXPECT_NE(cardAfter, cardBefore) << "the write rebuilt the card";
    EXPECT_EQ(editor->getBounds().withHeight(cardAfter->getHeight()), cardAfter->getBounds());
    EXPECT_EQ(editor->getOutlineCountForTest(), count);
    EXPECT_EQ(editor->getOutlineForTest("cutoff"), outlineBefore) << "an outline survives a write";
    EXPECT_EQ(editor->getParentComponent(), cardAfter->getParentComponent());
    const auto cell = editor->getCellRectForTest("cutoff");
    for (const auto& onCard : synth::ui::collectCells(*cardAfter))
        if (onCard.key == "cutoff")
            EXPECT_EQ(onCard.rect, cell) << "the overlay's cell is where the rebuilt card draws the knob";
    EXPECT_EQ(outlineBefore->getBounds(), cell.expanded(CardLayoutOutline::kPad));
}

TEST(OnCardEditor, ClosingTakesTheOverlayOffTheCanvas) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* parent = editor->getParentComponent();
    ASSERT_NE(parent, nullptr);
    const int children = parent->getNumChildComponents();

    editor->done();
    EXPECT_TRUE(editor->isClosed());
    EXPECT_EQ(editor->getParentComponent(), nullptr);
    EXPECT_EQ(parent->getNumChildComponents(), children - 1);
}

TEST(OnCardEditor, OpeningAnotherEditorWhileOneRunsLeavesTheFirstSessionAsDone) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* first = rig.openOnCard(id);
    ASSERT_NE(first, nullptr);
    Pointer pointer(*first, "cutoff");
    pointer.moveBy({0, 90}, juce::ModifierKeys::commandModifier);
    pointer.release();
    ASSERT_TRUE(rig.storedLayout(id).has_value());

    EXPECT_NE(rig.openOnCard(id), nullptr) << "the seam replaces the launched editor, ending the first";
    EXPECT_TRUE(rig.storedLayout(id).has_value()) << "the first session's layout stays";
}

TEST(OnCardEditor, TheNodeGoingAwayClosesTheEditor) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    rig.canvas.engine.getGraph().removeNode(id);
    rig.canvas.editor.updateComponents();
    editor->runQueuedSyncForTest();
    EXPECT_TRUE(editor->isClosed());
}
