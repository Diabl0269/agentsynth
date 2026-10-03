// OnCardPanelOnlyTests.cpp -- the controls of the footer and of the selected tab in the on-card editor:
// outlined like any control, but with no grip and nothing to drag or nudge; their options panel opens as for
// any other control, a tab switch re-reads the outlines, and Hide and Label write through the same layout edit.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"

using namespace oncard_test;

namespace {

NodeID addWavetable(OnCardRig& rig) { return rig.add(synth::AIStateMapper::createModule("Wavetable")); }

// The first control of tab `tab` of the card's one tab strip.
juce::String firstControlOfTab(ModuleComponent& card, int tab) {
    const auto& plan = card.getCardBody()->getPlan();
    const auto& section = plan.sections[(size_t)plan.tabGroups[0].sections[(size_t)tab]];
    return plan.items[(size_t)section.items.front()].param->paramID;
}

void selectTab(ModuleComponent& card, int tab) {
    auto* strip = dynamic_cast<synth::ui::CardSegmentedSwitch*>(card.getCardBody()->getTabStrip(0));
    ASSERT_NE(strip, nullptr);
    strip->setSelectedIndex(tab, juce::sendNotificationSync);
}

} // namespace

TEST(OnCardPanelOnly, ASelectedTabsControlsGetOutlinesWithNoGripAndTheirOwnTooltip) {
    OnCardRig rig;
    const auto id = addWavetable(rig);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto control = firstControlOfTab(*rig.card(id), 0);
    auto* outline = editor->getOutlineForTest(control);
    ASSERT_NE(outline, nullptr);
    EXPECT_TRUE(outline->isPanelOnly());
    EXPECT_EQ(outline->getTooltip(), "Right-click for options");
    EXPECT_EQ(outline->getTitle(),
              cardlayouteditor_test::captionOf(*rig.card(id), control) + ", layout: Return for options");
    EXPECT_TRUE(outline->getWantsKeyboardFocus());
    EXPECT_FALSE(editor->getOutlineForTest("position")->isPanelOnly()) << "a control above the tabs still moves";
}

TEST(OnCardPanelOnly, ATabControlCannotBeDraggedOrNudged) {
    OnCardRig rig;
    const auto id = addWavetable(rig);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto control = firstControlOfTab(*rig.card(id), 0);
    const auto start = editor->getCellRectForTest(control);

    Pointer pointer(*editor, control);
    pointer.moveBy({40, 30}, juce::ModifierKeys::commandModifier);
    EXPECT_FALSE(editor->isDraggingForTest());
    pointer.release();
    editor->getOutlineForTest(control)->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    EXPECT_FALSE(editor->hasPendingNudgeForTest());
    EXPECT_EQ(editor->getCellRectForTest(control), start);
    EXPECT_FALSE(rig.storedLayout(id).has_value()) << "nothing was written";
}

TEST(OnCardPanelOnly, ATabControlCanBeRelabelledAndHiddenThroughThePanelAndTheTabsStayTabs) {
    OnCardRig rig;
    const auto id = addWavetable(rig);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto control = firstControlOfTab(*rig.card(id), 0);
    ASSERT_EQ(rig.card(id)->getCardBody()->getPlan().tabGroups.size(), 1u);

    auto* panel = openControlPanel(rig, *editor, control);
    ASSERT_NE(panel, nullptr) << "Return opens the panel as for any control";
    panel->getLabelEditorForTest().setText("Pitch", false);
    panel->commitLabelForTest();
    EXPECT_EQ(cardlayouteditor_test::captionOf(*rig.card(id), control), "Pitch");
    EXPECT_EQ(storedItem(*rig.storedLayout(id), control)->label, std::optional<juce::String>("Pitch"));

    hideThroughPanel(rig, *editor, control);
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains(control));
    EXPECT_TRUE(rig.card(id)->getCardBody()->hasMoreRow()) << "a hidden control goes to the More row";
    EXPECT_FALSE(widgetOf(*rig.card(id), control)->isVisible());
    EXPECT_EQ(editor->getOutlineForTest(control), nullptr);
    EXPECT_EQ(rig.card(id)->getCardBody()->getPlan().tabGroups.size(), 1u) << "one strip throughout";
    int tabSections = 0;
    for (const auto& section : rig.card(id)->getCardBody()->getPlan().sections)
        tabSections += section.tabGroup >= 0 ? 1 : 0;
    EXPECT_EQ(tabSections, 5) << "an edit keeps every tab section a tab";
}

TEST(OnCardPanelOnly, SwitchingTheTabResyncsTheOutlinesToTheNewTab) {
    OnCardRig rig;
    const auto id = addWavetable(rig);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto first = firstControlOfTab(*rig.card(id), 0);
    const auto second = firstControlOfTab(*rig.card(id), 1);
    ASSERT_NE(editor->getOutlineForTest(first), nullptr);
    ASSERT_EQ(editor->getOutlineForTest(second), nullptr);

    selectTab(*rig.card(id), 1);
    editor->runQueuedSyncForTest();
    EXPECT_EQ(editor->getOutlineForTest(first), nullptr);
    ASSERT_NE(editor->getOutlineForTest(second), nullptr);
    EXPECT_TRUE(editor->getOutlineForTest(second)->isPanelOnly());
    EXPECT_EQ(editor->getOutlineForTest(second)->getBounds(),
              editor->getCellRectForTest(second).expanded(CardLayoutOutline::kPad));
    EXPECT_FALSE(editor->isClosed());
}

TEST(OnCardPanelOnly, AFooterControlOpensThePanelAndCannotBeDragged) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* outline = editor->getOutlineForTest("poly");
    ASSERT_NE(outline, nullptr);
    EXPECT_TRUE(outline->isPanelOnly());
    const auto start = editor->getCellRectForTest("poly");

    Pointer pointer(*editor, "poly");
    pointer.moveBy({-30, -40}, juce::ModifierKeys::commandModifier);
    pointer.release();
    EXPECT_EQ(editor->getCellRectForTest("poly"), start);
    EXPECT_FALSE(rig.storedLayout(id).has_value());

    auto* panel = openControlPanel(rig, *editor, "poly");
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->getOptions().paramId, "poly");
    panel->getLabelEditorForTest().setText("Voices", false);
    panel->commitLabelForTest();
    ASSERT_TRUE(rig.storedLayout(id).has_value()) << "an item the footer drew by itself is listed to carry the label";
    EXPECT_EQ(storedItem(*rig.storedLayout(id), "poly")->label, std::optional<juce::String>("Voices"));
    EXPECT_EQ(editor->getControlPanelForTest(), panel) << "the panel stays open";

    hideThroughPanel(rig, *editor, "poly");
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("poly"));
    EXPECT_EQ(editor->getOutlineForTest("poly"), nullptr);
}
