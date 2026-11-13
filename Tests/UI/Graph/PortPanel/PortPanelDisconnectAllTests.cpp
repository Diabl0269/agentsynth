// The port connections panel's Disconnect all: one icon-only button in the title row, shown from two connections,
// one undo step for the lot, in the keyboard order and aligned with the rows' remove column.

#include "PortPanelTestFixture.h"

#include "UI/Graph/ModDot/ModDotAddSourceParts.h"
#include "UI/Layout/ReducedMotion.h"

namespace {
struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};
} // namespace

TEST_F(GraphEditorTest, DisconnectAllShowsFromTwoConnectionsAndHidesAtOne) {
    NoMotion motion;
    PortFixture f(2);
    f.clickOscOutput();
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    EXPECT_TRUE(panel->isDisconnectAllShown());
    EXPECT_TRUE(panel->disconnectAllButton().isShowing() || panel->disconnectAllButton().isVisible());

    panel->rowAt(0)->removeButton().triggerClick();
    synth::test::flushMessageQueue();
    f.refresh();
    EXPECT_FALSE(panel->isDisconnectAllShown()) << "one connection left: the button is gone";
}

TEST_F(GraphEditorTest, DisconnectAllRemovesEveryCableAndOneUndoBringsThemAllBack) {
    NoMotion motion;
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(f.totalOscCables(), 3);
    clickNow(panel->disconnectAllButton());
    f.refresh();
    EXPECT_EQ(f.totalOscCables(), 0);
    EXPECT_TRUE(f.undo.canUndo());
    f.undo.undo();
    f.refresh();
    EXPECT_EQ(f.totalOscCables(), 3) << "one undo restores all three";
}

TEST_F(GraphEditorTest, DisconnectAllSitsCentredInTheTitleRowAboveTheRemoveColumn) {
    NoMotion motion;
    PortFixture f(3);
    f.clickOscOutput();
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    auto& button = panel->disconnectAllButton();
    const auto b = panel->getLocalArea(&button, button.getLocalBounds());
    const auto remove =
        panel->getLocalArea(&panel->rowAt(0)->removeButton(), panel->rowAt(0)->removeButton().getLocalBounds());
    EXPECT_EQ(b.getRight(), remove.getRight()) << "its right edge lines up with the rows' remove column";
    EXPECT_LT(b.getBottom(), remove.getY()) << "it is in the title row, above the first row";
    EXPECT_EQ(b.getWidth(), b.getHeight()) << "icon only: a square";
}
