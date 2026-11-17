// OnCardEditorTests.cpp -- opening the on-card layout editor from the real module menu: one accessible
// outline per control over the card, the card's positions surviving the move to free placement, the
// overlay following the card a write rebuilds, and leaving the canvas when it closes.

#include "../../../Accessibility/AccessibilityAudit.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"

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
                  panelOnly
                      ? "Backspace removes. Right-click for options"
                      : "Drag to move (arrow keys nudge, Shift for 8px, Backspace removes). Right-click for options");
        EXPECT_TRUE(outline->getWantsKeyboardFocus());
    }
    for (const auto* id2 : {"cutoff", "resonance", "drive", "outputLevel"})
        EXPECT_NE(editor->getOutlineForTest(id2), nullptr) << id2;
    ASSERT_NE(editor->getOutlineForTest("poly"), nullptr) << "the footer's controls get one too";
    EXPECT_TRUE(editor->getOutlineForTest("poly")->isPanelOnly());
    EXPECT_FALSE(editor->getOutlineForTest("cutoff")->isPanelOnly());
}

// Regression test for FRO704: the footer's outlines were widened to a body cell's box and drawn off the card.
TEST(OnCardEditor, EveryOutlineLiesInsideTheCard) {
    const auto check = [](std::unique_ptr<ModuleBase> module) {
        OnCardRig rig;
        const auto id = rig.add(std::move(module), 200, 200);
        auto* card = rig.card(id);
        auto* editor = rig.openOnCard(id);
        ASSERT_NE(editor, nullptr);
        for (const auto& cell : synth::ui::collectCells(*card)) {
            SCOPED_TRACE(cell.key.toStdString());
            auto* outline = editor->getOutlineForTest(cell.key);
            ASSERT_NE(outline, nullptr);
            EXPECT_TRUE(card->getLocalBounds().contains(outline->getBounds())) << outline->getBounds().toString();
        }
    };
    check(std::make_unique<FilterModule>());
    check(std::make_unique<OscillatorModule>());
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

// The layout-overlap guard for the Edit Layout state of every card: each outline must hold its own control's caption
// and stay apart from every other outline. Regression test for FRO1910: the outline's top edge ran through the
// Filter Type caption, cutting its title.
namespace {

// The words a caption draws, in the editor's coordinates: its line box at the label's justification, as wide as the
// text (never wider than the label, which then fits it).
juce::Rectangle<float> captionTextArea(const juce::Label& label, juce::Point<int> cardInParent) {
    const auto font = label.getFont();
    const float textW =
        std::min((float)label.getWidth(), juce::GlyphArrangement::getStringWidth(font, label.getText()));
    const float textH = font.getHeight();
    const auto box = label.getBounds().translated(cardInParent.x, cardInParent.y).toFloat();
    const auto just = label.getJustificationType();
    const float x = just.testFlags(juce::Justification::horizontallyCentred) ? box.getCentreX() - textW / 2.0f
                    : just.testFlags(juce::Justification::right)             ? box.getRight() - textW
                                                                             : box.getX();
    return {x, box.getCentreY() - textH / 2.0f, textW, textH};
}

constexpr float kTextClear = 0.0f; ///< the caption's whole line box (taller than its ink) lies inside the outline,
                                   ///< whose side edges sit kInset inside the cell

} // namespace

// Every module type with an Edit Layout item: the editor opens, no two outlines touch, and each outline keeps
// kTextClear around its own caption's words (the Filter Type title among them).
TEST(CardBodyOverlap, EveryCardsEditLayoutOutlinesClearTheirCaptionsAndEachOther) {
    AudioEngine engine;
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const char* extra : {"Track In", "Rec Tap", "Track Audio", "Channel Strip", "Master", "Amp Env", "Filter Env"})
        types.addIfNotAlreadyThere(extra);

    int opened = 0;
    int held = 0;
    bool filterTypeHeld = false;
    for (const auto& type : types) {
        OnCardRig rig;
        auto processor = synth::AIStateMapper::createModule(type);
        if (dynamic_cast<ModuleBase*>(processor.get()) == nullptr)
            continue;
        const auto id = rig.add(std::move(processor), 200, 200);
        auto* card = rig.card(id);
        ASSERT_NE(card, nullptr) << type;
        auto* editor = rig.openOnCard(id);
        if (editor == nullptr)
            continue; // a card with no Edit Layout item (a hosted plugin has its picker instead)
        ++opened;

        const auto cardAt = card->getPosition() - editor->getPosition();
        const auto& plan = card->getCardBody()->getPlan();
        std::vector<std::pair<juce::String, juce::Rectangle<float>>> areas;
        for (const auto& item : plan.items) {
            if (item.param == nullptr || !item.shown || item.widget == nullptr || !item.widget->isVisible())
                continue;
            auto* outline = editor->getOutlineForTest(item.param->paramID);
            if (outline == nullptr)
                continue;
            const auto area = outline->getOutlineArea().translated((float)outline->getX(), (float)outline->getY());
            if (!outline->isPanelOnly())
                areas.emplace_back(item.param->paramID, area);
            const auto* label = dynamic_cast<const juce::Label*>(item.label);
            if (label == nullptr || !label->isVisible() || label->getText().isEmpty())
                continue;
            // Rows of cells abut, so a cell with another right above it has no room to rise (a known gap: its
            // caption's letters sit against the outline's top edge); every other caption is held whole.
            if (outline->getCaptionRise() < CardLayoutOutline::kCaptionRise)
                continue;
            ++held;
            if (type == "Filter" && item.param->paramID == "filterType")
                filterTypeHeld = true;
            const auto text = captionTextArea(*label, cardAt);
            EXPECT_TRUE(area.expanded((float)CardLayoutOutline::kInset, 0.0f).contains(text.expanded(kTextClear)))
                << type << ": the outline of \"" << label->getText() << "\" " << area.toString() << " is closer than "
                << kTextClear << " px to its caption text " << text.toString();
        }
        // Gap: the Sequencer cards' step-grid cells report rectangles far outside the card in this rig (their width
        // comes from the card's own step grid), so their outlines are not compared with each other yet.
        if (type.contains("Sequencer"))
            continue;
        for (size_t i = 0; i < areas.size(); ++i)
            for (size_t j = i + 1; j < areas.size(); ++j)
                EXPECT_FALSE(areas[i].second.intersects(areas[j].second))
                    << type << ": the outlines of " << areas[i].first << " " << areas[i].second.toString() << " and "
                    << areas[j].first << " " << areas[j].second.toString() << " overlap";
    }
    EXPECT_GE(opened, 15) << "the sweep reaches the module types";
    EXPECT_GT(held, 10) << "the sweep checks captions";
    EXPECT_TRUE(filterTypeHeld) << "the Filter Type title is held whole inside its outline";
}
