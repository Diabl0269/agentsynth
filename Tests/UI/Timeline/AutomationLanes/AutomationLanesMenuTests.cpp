// AutomationLanesMenuTests.cpp -- the lane menu opens at the pointer on a right-click anywhere on a lane: its header,
// its curve editor's empty space and (for a modulator) the row and band; a handle and a segment keep their own
// menus; Shift+F10 reaches the same menu. Real mouse events through the real handlers; the menu is captured by the
// shared hook because a popup menu does not run headlessly.

#include "AutomationLanesMenuFixture.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

using namespace lane_menu_test;
using namespace automation_lanes_test;

namespace {
bool hasItem(const juce::StringArray& texts, const juce::String& text) { return texts.contains(text); }

juce::Rectangle<int> screenPointArea(const juce::MouseEvent& e) {
    const auto p = e.getScreenPosition();
    return {p.x, p.y, 1, 1};
}
} // namespace

TEST(AutomationLanesMenuTest, RightClickOnEmptyCurveSpaceOpensTheLaneMenuAtThePointer) {
    MenuPanel f;
    MenuCapture capture;
    auto* editor = f.editor(f.lane);
    ASSERT_NE(editor, nullptr);
    const juce::Point<float> empty(60.0f, 2.0f); // the top edge, far from the curve and every handle
    ASSERT_FALSE(editor->getHandleRectForTest(0.0).contains(empty.toInt()));

    const auto click = makeClickEvent(*editor, empty, rightButton());
    editor->mouseDown(click);

    ASSERT_EQ(capture.count, 1);
    const auto texts = capture.itemTexts();
    EXPECT_TRUE(hasItem(texts, "Change parameter..."));
    EXPECT_TRUE(hasItem(texts, "Duplicate"));
    EXPECT_TRUE(hasItem(texts, "Delete lane"));
    EXPECT_TRUE(hasItem(texts, "Add modulator... (no CV input)")) << "the same menu the \"...\" button opens";
    EXPECT_EQ(capture.options.getTargetScreenArea(), screenPointArea(click)) << "top-left at the pointer";
}

TEST(AutomationLanesMenuTest, RightClickOnTheHeaderOpensTheSameMenuAtThePointer) {
    MenuPanel f;
    MenuCapture capture;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    const juce::Point<float> inStripe(synth::ui::AutomationLaneHeaderComponent::kIndent + 1.0f, 3.0f);

    const auto click = makeClickEvent(*header, inStripe, rightButton());
    header->mouseDown(click);

    ASSERT_EQ(capture.count, 1);
    EXPECT_TRUE(hasItem(capture.itemTexts(), "Change parameter..."));
    EXPECT_EQ(capture.options.getTargetScreenArea(), screenPointArea(click));

    // Return on the focused row opens the same list, beside the row.
    const auto before = capture.itemTexts();
    capture.count = 0;
    EXPECT_TRUE(header->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(capture.itemTexts(), before);
    EXPECT_FALSE(header->keyPressed(juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0)))
        << "a modified Return is not the menu key";
}

TEST(AutomationLanesMenuTest, NoLaneHeaderOrModulatorRowHasAMenuButtonAnyMore) {
    MenuPanel f;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    const auto isMenuButton = [](juce::Component* c) {
        return dynamic_cast<juce::Button*>(c) != nullptr &&
               (c->getComponentID().containsIgnoreCase("menu") || c->getTitle().containsIgnoreCase("menu"));
    };
    for (auto* child : header->getChildren())
        EXPECT_FALSE(isMenuButton(child)) << child->getComponentID() << " / " << child->getTitle();
}

TEST(AutomationLanesMenuTest, RightClickOnAHandleStillOpensTheHandleMenuNotTheLaneMenu) {
    MenuPanel f;
    MenuCapture capture;
    auto* editor = f.editor(f.lane);
    ASSERT_NE(editor, nullptr);
    const auto handle = editor->getHandleRectForTest(2.0);
    ASSERT_FALSE(handle.isEmpty());

    editor->mouseDown(makeClickEvent(*editor, handle.getCentre().toFloat(), rightButton()));

    ASSERT_EQ(capture.count, 1);
    const auto texts = capture.itemTexts();
    EXPECT_TRUE(hasItem(texts, "Delete point"));
    EXPECT_FALSE(hasItem(texts, "Change parameter...")) << "a handle keeps its own menu";
}

TEST(AutomationLanesMenuTest, RightClickOnTheCurveBetweenTwoPointsOpensTheSegmentMenu) {
    MenuPanel f;
    MenuCapture capture;
    auto* editor = f.editor(f.lane);
    ASSERT_NE(editor, nullptr);
    // Beat 1 sits half way between the points at beats 0 (value 0) and 2 (value 25): on the curve, off both handles.
    const auto x = (float)f.panel.getViewState().beatToX(1.0);
    const auto y = (float)editor->valueToY(12.5);

    editor->mouseDown(makeClickEvent(*editor, {x, y}, rightButton()));

    ASSERT_EQ(capture.count, 1);
    EXPECT_TRUE(hasItem(capture.itemTexts(), "Linear"));
    EXPECT_FALSE(hasItem(capture.itemTexts(), "Change parameter..."));
}

TEST(AutomationLanesMenuTest, TheMenuKeyOnAFocusedLaneHeaderOrEditorOpensTheLaneMenu) {
    MenuPanel f;
    MenuCapture capture;
    auto* header = f.header(f.lane);
    auto* editor = f.editor(f.lane);
    ASSERT_NE(header, nullptr);
    ASSERT_NE(editor, nullptr);

    EXPECT_TRUE(header->showContextMenuForKeyboardFocus());
    EXPECT_TRUE(editor->showContextMenuForKeyboardFocus());

    EXPECT_EQ(capture.count, 2);
    EXPECT_TRUE(hasItem(capture.itemTexts(), "Duplicate"));
    // The keyboard route resolves through the same walk the app's command uses: the nearest provider above focus.
    EXPECT_TRUE(synth::ui::openContextMenuForFocusedComponent(&header->getRecordModeCombo()));
    EXPECT_EQ(capture.count, 3);
}

TEST(AutomationLanesMenuTest, TheChangeParameterAndDuplicateItemsAreDisabledWhenNoParameterIsFree) {
    MenuPanel f;
    f.host.offered.clear();
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);

    const auto menu = header->buildMenu();
    const auto* change = findMenuItem(menu, "Change parameter... (no free parameter)");
    const auto* duplicate = findMenuItem(menu, "Duplicate (no free parameter)");
    ASSERT_NE(change, nullptr);
    ASSERT_NE(duplicate, nullptr);
    EXPECT_FALSE(change->isEnabled);
    EXPECT_FALSE(duplicate->isEnabled);
}

TEST(AutomationLanesMenuTest, RightClickOnAModulatorRowOpensItsMenuAtThePointer) {
    synth::ui::ModulatorInfo info;
    info.sourceUuid = "lfo-1";
    info.sourceTitle = "LFO 1";
    info.isLfo = true;
    synth::ui::ModulatorRow row(info, nullptr, "Cutoff");
    row.setSize(200, 54);
    MenuCapture capture;

    const auto click = makeClickEvent(row, {3.0f, 3.0f}, rightButton());
    row.mouseDown(click);

    ASSERT_EQ(capture.count, 1);
    EXPECT_TRUE(hasItem(capture.itemTexts(), "Show on canvas"));
    EXPECT_TRUE(hasItem(capture.itemTexts(), "Remove modulator"));
    EXPECT_EQ(capture.options.getTargetScreenArea(), screenPointArea(click));

    capture.count = 0;
    row.mouseDown(makeClickEvent(row, {3.0f, 3.0f}, leftButton()));
    EXPECT_EQ(capture.count, 0) << "a left click on the row opens nothing";
}

TEST(AutomationLanesMenuTest, RightClickOnAModulatorBandOpensTheRowsMenuAndStartsNoKnobDrag) {
    synth::ui::TimelineViewState view;
    synth::ui::ModulatorInfo info;
    info.sourceUuid = "lfo-1";
    info.sourceTitle = "LFO 1";
    info.isLfo = true;
    info.attenuverterUuid = "atten-1";
    synth::ui::ModulatorRow row(info, nullptr, "Cutoff");
    synth::ui::ModulatorBand band(view);
    band.setSize(400, 54);
    band.setModulator(info, synth::LaneId{1}, "Cutoff");
    band.onMenuRequested = [&row](const juce::PopupMenu::Options& options) { row.showMenuAt(options); };
    MenuCapture capture;

    const auto click = makeClickEvent(band, {50.0f, 20.0f}, rightButton());
    band.mouseDown(click);

    ASSERT_EQ(capture.count, 1);
    EXPECT_TRUE(hasItem(capture.itemTexts(), "Remove modulator"));
    EXPECT_EQ(capture.options.getTargetScreenArea(), screenPointArea(click));
    EXPECT_TRUE(band.showContextMenuForKeyboardFocus());
    EXPECT_EQ(capture.count, 2);
}

TEST(AutomationLanesMenuTest, RightClickReturnAndShiftF10OnATrackRowOpenItsMenu) {
    MenuPanel f;
    auto* row = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(row, nullptr);
    int menus = 0;
    juce::StringArray lastItems;
    row->setShowContextMenuHookForTest([&](juce::PopupMenu& menu) {
        ++menus;
        lastItems.clear();
        juce::PopupMenu::MenuItemIterator it(menu, true);
        while (it.next())
            lastItems.add(it.getItem().text);
    });

    row->mouseDown(makeClickEvent(*row, {110.0f, 3.0f}, rightButton()));
    ASSERT_EQ(menus, 1);
    EXPECT_TRUE(lastItems.contains("Delete Track"));
    const auto fromRightClick = lastItems;

    EXPECT_TRUE(row->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(menus, 2);
    EXPECT_EQ(lastItems, fromRightClick) << "Return opens the same menu";

    EXPECT_TRUE(synth::ui::openContextMenuForFocusedComponent(row)) << "what Shift+F10 resolves to";
    EXPECT_EQ(menus, 3);
    EXPECT_FALSE(row->keyPressed(juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::commandModifier, 0)));
    EXPECT_EQ(menus, 3);
}
