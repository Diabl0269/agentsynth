// PortPanelRowsTests.cpp: the port connections panel's rows and buttons: removing one connection or all of them
// (one undo step each, with the cable retracting and the rows moving), the canvas highlight, the keyboard and the
// names every control carries.

#include "PortPanelTestFixture.h"

#include "UI/Layout/ReducedMotion.h"

namespace {

using synth::ui::ExitEnterTimeline;

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

bool isScaled(const juce::Component& c) { return !c.getTransform().isIdentity(); }

} // namespace

TEST_F(GraphEditorTest, RemovingARowRemovesExactlyThatCableInOneUndoStep) {
    ReducedMotionGuard guard(false);
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    const auto middle = panel->rowAt(1)->cableId();

    clickNow(panel->rowAt(1)->removeButton());

    EXPECT_EQ(f.cablesBetweenOscAnd(0), 1);
    EXPECT_EQ(f.cablesBetweenOscAnd(1), 0) << "the clicked row's cable is gone";
    EXPECT_EQ(f.cablesBetweenOscAnd(2), 1);
    EXPECT_EQ(panel->rowCount(), 2);
    EXPECT_TRUE(panel->titleText().endsWith("(2 connections)")) << "the count follows";
    EXPECT_EQ(f.editor->getCableRetractForTest().ghosts().size(), 1u) << "the cable retracts into its jack";
    f.editor->finishCableRetractForTest();

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.totalOscCables(), 3) << "a single Cmd+Z brings the connection back";
    f.refresh(); // the editor's tick follows the graph
    ASSERT_NE(f.panel(), nullptr);
    EXPECT_EQ(f.panel()->rowCount(), 3);
    EXPECT_NE(f.panel()->rowFor(middle), nullptr) << "its row comes back";
    EXPECT_EQ(f.editor->getCableRetractForTest().numGrowing(), 1u) << "and its cable grows back out of the jack";
}

TEST_F(GraphEditorTest, ARemovedRowShrinksThenTheRowsBelowCloseTheGapAndThePanelSettles) {
    ReducedMotionGuard guard(false);
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    panel->setForceAnimateForTest(true);
    const auto first = panel->rowAt(0)->cableId();
    const auto second = panel->rowAt(1)->cableId();
    const int height = panel->getHeight();
    const int secondY = panel->rowAt(1)->getY();

    clickNow(panel->rowAt(0)->removeButton());

    EXPECT_TRUE(panel->isAnimating());
    EXPECT_TRUE(panel->timeline().hasExit);
    EXPECT_DOUBLE_EQ(panel->timeline().totalMs(), ExitEnterTimeline::kExitMs + ExitEnterTimeline::kGapMs)
        << "the shared exit and gap times";
    ASSERT_NE(panel->animatingRowFor(first), nullptr) << "its row is still on screen, shrinking";
    EXPECT_FALSE(panel->animatingRowFor(first)->isEnabled());
    panel->applyTimelineAtMs(ExitEnterTimeline::kExitMs * 0.5);
    EXPECT_TRUE(isScaled(*panel->animatingRowFor(first)));
    EXPECT_EQ(panel->slotHeightFor(first), PortConnectionRow::kHeight) << "the slot holds while the row shrinks";
    EXPECT_EQ(panel->rowFor(second)->getY(), secondY);
    EXPECT_EQ(panel->getHeight(), height);

    panel->applyTimelineAtMs(ExitEnterTimeline::kExitMs + ExitEnterTimeline::kGapMs * 0.5);
    EXPECT_LT(panel->rowFor(second)->getY(), secondY);
    EXPECT_LT(panel->getHeight(), height) << "the panel's height follows the closing gap";

    panel->applyTimelineAtMs(panel->timeline().totalMs());
    EXPECT_EQ(panel->rowFor(second)->getY(), 0);
    EXPECT_EQ(panel->getHeight(), height - PortConnectionRow::kHeight);
}

TEST_F(GraphEditorTest, ReduceMotionFadesAPanelRowInPlace) {
    ReducedMotionGuard guard(true);
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    panel->setForceAnimateForTest(true);
    const auto first = panel->rowAt(0)->cableId();
    clickNow(panel->rowAt(0)->removeButton());
    panel->applyTimelineAtMs(ExitEnterTimeline::kExitMs * 0.5);
    EXPECT_FALSE(isScaled(*panel->animatingRowFor(first)));
    EXPECT_LT(panel->animatingRowFor(first)->getAlpha(), 1.0f);
}

TEST_F(GraphEditorTest, UndoBringsARowBackWithRoomMadeFirstAndAnOutlineAfter) {
    ReducedMotionGuard guard(false);
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    panel->setForceAnimateForTest(true);
    const auto first = panel->rowAt(0)->cableId();
    clickNow(panel->rowAt(0)->removeButton());
    panel->applyTimelineAtMs(panel->timeline().totalMs());

    ASSERT_TRUE(f.undo.undo());
    f.refresh();

    ASSERT_EQ(panel->rowCount(), 3);
    ASSERT_NE(panel->animatingRowFor(first), nullptr);
    EXPECT_TRUE(panel->timeline().hasEnter);
    EXPECT_EQ(panel->slotHeightFor(first), 0) << "the gap opens first";
    panel->applyTimelineAtMs(ExitEnterTimeline::kGapMs);
    EXPECT_EQ(panel->slotHeightFor(first), PortConnectionRow::kHeight);
    panel->applyTimelineAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs +
                             ExitEnterTimeline::kOutlineMs * 0.5);
    EXPECT_NEAR(panel->outlineAlphaFor(first), 0.5f, 0.01f);
    EXPECT_FALSE(isScaled(*panel->animatingRowFor(first)));
}

TEST_F(GraphEditorTest, RemovingTheLastConnectionKeepsThePanelOpenSayingNoConnectionsYet) {
    PortFixture f(1);
    f.editor->setDoubleClickPortDisconnectEnabled(false);
    f.clickOscOutput();
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);

    clickNow(panel->rowAt(0)->removeButton());

    ASSERT_EQ(f.panel(), panel) << "still the same open panel";
    EXPECT_EQ(panel->rowCount(), 0);
    EXPECT_TRUE(panel->titleText().endsWith("(No connections yet)"));
    EXPECT_EQ(f.totalOscCables(), 0);
}

TEST_F(GraphEditorTest, DisconnectAllRemovesEveryCableInOneUndoStepAndKeepsThePanelOpen) {
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(panel->isDisconnectAllShown());

    clickNow(panel->disconnectAllButton());

    EXPECT_EQ(f.totalOscCables(), 0);
    ASSERT_EQ(f.panel(), panel);
    EXPECT_EQ(panel->rowCount(), 0);
    EXPECT_FALSE(panel->isDisconnectAllShown());
    EXPECT_TRUE(panel->titleText().endsWith("(No connections yet)"));
    EXPECT_EQ(f.editor->getCableRetractForTest().ghosts().size(), 3u) << "every cable retracts";
    f.editor->finishCableRetractForTest();

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.totalOscCables(), 3) << "one Cmd+Z restores them all";
    f.refresh();
    EXPECT_EQ(f.panel()->rowCount(), 3);
}

TEST_F(GraphEditorTest, DisconnectAllShrinksTheRowsTogether) {
    ReducedMotionGuard guard(false);
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    panel->setForceAnimateForTest(true);
    clickNow(panel->disconnectAllButton());
    ASSERT_TRUE(panel->isAnimating());
    panel->applyTimelineAtMs(ExitEnterTimeline::kExitMs * 0.5);
    int scaled = 0;
    for (int i = 0; i < 3; ++i)
        scaled += isScaled(*panel->animatingRowFor(f.editor->getCableRetractForTest().ghosts()[(size_t)i].id)) ? 1 : 0;
    EXPECT_EQ(scaled, 3) << "all three shrink at once";
}

TEST_F(GraphEditorTest, ThePanelFollowsTheGraphWhileOpen) {
    PortFixture f(2, 1);
    f.clickOscOutput();
    ASSERT_EQ(f.panel()->rowCount(), 2);

    f.editor->connectPorts(f.oscId, 0, f.vcaIds[2], 0, false, true);
    f.refresh();

    EXPECT_EQ(f.panel()->rowCount(), 3);
    EXPECT_TRUE(f.panel()->isDisconnectAllShown());
    EXPECT_TRUE(f.panel()->titleText().contains("(3 connections)"));
}

TEST_F(GraphEditorTest, HoveringARowHighlightsItsCableAndLeavingClearsIt) {
    PortFixture f(3);
    f.clickOscOutput();
    auto* row = f.panel()->rowAt(1);
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(f.controller().highlightedCable().has_value());

    row->mouseEnter(portEvent(*row, {20, 10}, {20, 10}));
    ASSERT_TRUE(f.controller().highlightedCable().has_value());
    EXPECT_EQ(*f.controller().highlightedCable(), row->cableId());

    f.panel()->rowAt(2)->mouseEnter(portEvent(*f.panel()->rowAt(2), {20, 10}, {20, 10}));
    EXPECT_EQ(*f.controller().highlightedCable(), f.panel()->rowAt(2)->cableId()) << "the pointer moved to another row";

    row->mouseExit(portEvent(*row, {20, 10}, {20, 10}));
    EXPECT_TRUE(f.controller().highlightedCable().has_value()) << "leaving a row that is not the highlighted one";
    auto* last = f.panel()->rowAt(2);
    last->mouseExit(portEvent(*last, {20, 10}, {20, 10}));
    EXPECT_FALSE(f.controller().highlightedCable().has_value());
}

TEST_F(GraphEditorTest, KeyboardFocusOnARowsButtonHighlightsItsCableToo) {
    PortFixture f(2);
    f.clickOscOutput();
    auto* row = f.panel()->rowAt(0);
    row->removeButton().focusGained(juce::Component::focusChangedByTabKey);
    ASSERT_TRUE(f.controller().highlightedCable().has_value());
    EXPECT_EQ(*f.controller().highlightedCable(), row->cableId());
    row->removeButton().focusLost(juce::Component::focusChangedByTabKey);
    EXPECT_FALSE(f.controller().highlightedCable().has_value());
}

TEST_F(GraphEditorTest, TheHighlightClearsWhenThePanelClosesOrItsRowGoes) {
    PortFixture f(3);
    f.clickOscOutput();
    auto* row = f.panel()->rowAt(0);
    row->mouseEnter(portEvent(*row, {20, 10}, {20, 10}));
    ASSERT_TRUE(f.controller().highlightedCable().has_value());
    clickNow(row->removeButton());
    EXPECT_FALSE(f.controller().highlightedCable().has_value()) << "the highlighted cable was removed";

    auto* other = f.panel()->rowAt(0);
    other->mouseEnter(portEvent(*other, {20, 10}, {20, 10}));
    ASSERT_TRUE(f.controller().highlightedCable().has_value());
    f.controller().close();
    EXPECT_FALSE(f.controller().highlightedCable().has_value());
    EXPECT_EQ(f.panel(), nullptr);
}

TEST_F(GraphEditorTest, EscapeClosesThePanel) {
    PortFixture f(3);
    f.clickOscOutput();
    ASSERT_NE(f.panel(), nullptr);
    EXPECT_TRUE(f.panel()->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(f.panel(), nullptr);
}

TEST_F(GraphEditorTest, TabReachesEveryRowsRemoveButtonThenTheSplitButtonThenDisconnectAll) {
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    auto traverser = panel->createFocusTraverser();
    std::vector<juce::Component*> stops; // the controls that take the keys, in traversal order
    for (auto* c : traverser->getAllComponents(panel))
        if (c->getWantsKeyboardFocus() && c != panel)
            stops.push_back(c);
    ASSERT_EQ(stops.size(), 6u);
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(stops[(size_t)i], &panel->rowAt(i)->removeButton()) << "stop " << i;
    EXPECT_EQ(stops[3], &panel->splitButton().leftHalf()) << "Add connection";
    EXPECT_EQ(stops[4], &panel->splitButton().rightHalf()) << "Pick on canvas";
    EXPECT_EQ(stops[5], &panel->disconnectAllButton());
}

TEST_F(GraphEditorTest, ReturnAndSpaceRemoveAConnectionFromTheKeyboard) {
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();

    pressKey(panel->rowAt(0)->removeButton(), juce::KeyPress(juce::KeyPress::returnKey));
    EXPECT_EQ(f.cablesBetweenOscAnd(0), 0);
    EXPECT_EQ(f.totalOscCables(), 2);

    pressKey(panel->rowAt(0)->removeButton(), juce::KeyPress(juce::KeyPress::spaceKey));
    EXPECT_EQ(f.totalOscCables(), 1);
}

TEST_F(GraphEditorTest, EveryControlInThePanelHasANameAndATooltip) {
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    EXPECT_TRUE(panel->getTitle().isNotEmpty());
    for (int i = 0; i < 3; ++i) {
        auto* row = panel->rowAt(i);
        EXPECT_TRUE(row->getTitle().isNotEmpty()) << "row " << i;
        auto& remove = row->removeButton();
        EXPECT_TRUE(remove.getTitle().contains(row->connection().label)) << "row " << i;
        EXPECT_TRUE(remove.getTooltip().isNotEmpty()) << "row " << i;
        EXPECT_TRUE(remove.getWantsKeyboardFocus()) << "row " << i;
    }
    auto& all = panel->disconnectAllButton();
    EXPECT_TRUE(all.getTitle().isNotEmpty());
    EXPECT_TRUE(all.getTooltip().isNotEmpty());
    EXPECT_TRUE(all.getWantsKeyboardFocus());
    EXPECT_NE(all.getTitle(), all.getTitle().toUpperCase()) << "no all-caps text";
}

TEST_F(GraphEditorTest, ARowsSwatchIsItsCablesColour) {
    PortFixture f(2);
    f.clickOscOutput();
    for (int i = 0; i < 2; ++i) {
        const auto* row = f.panel()->rowAt(i);
        bool found = false;
        for (const auto& cable : f.editor->buildVisibleCables())
            if (cable.id == row->cableId()) {
                EXPECT_EQ(row->swatchColour(), f.editor->colourForCable(cable));
                found = true;
            }
        EXPECT_TRUE(found);
    }
}
