// MixerZonesTests.cpp: pinning mixer channels into the left and right zones -- by the header's context
// menu and by dragging a row in the side pane -- where pinned columns sit, the half-width cap, undo, and
// that column drag-to-reorder still works inside the scrolling group. Drives a real off-screen
// MainComponent with hand-built mouse events.
#include "MixerZonesTestRig.h"
#include <gtest/gtest.h>

namespace {

using synth::MixerZone;
constexpr int kPitch = 144; // column width 140 + gap 4

// Hand-built events on a zones-list row, re-deriving the row-local position from list coordinates at
// every event (the row moves under the pointer during a drag, exactly as live).
struct RowDrag {
    synth::ui::MixerZonesRow& row;
    juce::Component& list;
    float pressY;

    juce::Point<float> local(float listY) const { return row.getLocalPoint(&list, juce::Point<float>(20.0f, listY)); }
    void down() { row.mouseDown(makeClickEvent(row, local(pressY))); }
    void dragTo(float y) { row.mouseDrag(makeDragEvent(row, local(y), local(pressY))); }
    void up(float y) { row.mouseUp(makeClickEvent(row, local(y))); }
};

RowDrag dragOf(MixerZonesRig& r, const juce::String& id) {
    auto* row = r.row(id);
    auto& list = r.panel->getZonesPaneForTest().getListContentForTest();
    return {*row, list, static_cast<float>(row->getY() + row->getHeight() / 2)};
}

} // namespace

TEST(MixerZonesTests, MasterIsPinnedRightInANewProject) {
    MixerZonesRig r(2);
    EXPECT_EQ(r.panel->getViewDoc().getZone(synth::MixerViewDoc::kMasterId), MixerZone::Right);

    auto* master = r.panel->getMasterColumnForTest();
    ASSERT_NE(master, nullptr);
    EXPECT_TRUE(isUnder(master, *r.panel->getRightZoneViewportForTest().getViewedComponent()));
    EXPECT_FALSE(isUnder(master, *r.panel->getViewportForTest().getViewedComponent()))
        << "Master sits outside the scrolling area";
    EXPECT_TRUE(r.panel->getRightZoneViewportForTest().isVisible());
    EXPECT_FALSE(r.panel->getLeftZoneViewportForTest().isVisible()) << "an empty zone takes no room";
    EXPECT_EQ(r.panel->getColumnZoneForTest(r.panel->getColumnCount() - 1), MixerZone::Right);
    EXPECT_TRUE(isUnder(r.panel->getStripColumnForTest(0), *r.panel->getViewportForTest().getViewedComponent()));
}

TEST(MixerZonesTests, PinLeftFromTheHeaderMenuMovesTheColumnOutsideTheScrollingViewport) {
    MixerZonesRig r(3);
    const auto id = r.stripId(0);
    auto* column = r.panel->getStripColumnForTest(0);
    const auto node = column->getNodeId();
    r.hookMenuToPick("Pin left");

    // The real right-click path: a popup-button release over the header opens the menu.
    auto& header = column->getHeaderForTest();
    header.mouseUp(makeClickEvent(header, {5.0f, 5.0f}, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));

    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Left);
    const synth::ui::MixerColumnComponent* pinned = nullptr;
    for (int i = 0; i < 3; ++i)
        if (r.panel->getStripColumnForTest(i)->getNodeId() == node)
            pinned = r.panel->getStripColumnForTest(i);
    ASSERT_NE(pinned, nullptr);
    auto& left = r.panel->getLeftZoneViewportForTest();
    EXPECT_TRUE(isUnder(pinned, *left.getViewedComponent()));
    EXPECT_FALSE(isUnder(pinned, *r.panel->getViewportForTest().getViewedComponent()));
    EXPECT_EQ(r.panel->getColumnZoneForTest(0), MixerZone::Left) << "the left zone comes first in walk order";
    EXPECT_LT(left.getRight(), r.panel->getViewportForTest().getX())
        << "a 2 px divider sits between the left zone and the scrolling middle";
    EXPECT_EQ(r.panel->getViewportForTest().getX(), left.getRight() + 2);
}

TEST(MixerZonesTests, MastersAndDirectsHeadersOfferTheSamePinMenu) {
    MixerZonesRig r(1);
    r.hookMenuToPick("Pin left");
    auto& masterHeader = r.panel->getMasterColumnForTest()->getHeaderForTest();
    masterHeader.mouseUp(
        makeClickEvent(masterHeader, {5.0f, 5.0f}, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    EXPECT_EQ(r.panel->getViewDoc().getZone(synth::MixerViewDoc::kMasterId), MixerZone::Left);
    EXPECT_EQ(r.panel->getColumnZoneForTest(0), MixerZone::Left) << "Master is now the first column";

    r.hookMenuToPick("Pin right");
    auto& directHeader = r.panel->getDirectColumnForTest()->getHeaderForTest();
    directHeader.mouseUp(
        makeClickEvent(directHeader, {5.0f, 5.0f}, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    EXPECT_EQ(r.panel->getViewDoc().getZone(synth::MixerViewDoc::kDirectId), MixerZone::Right);
}

TEST(MixerZonesTests, PinRightAndUnpinFromTheMenuOnlyOfferUnpinForAPinnedColumn) {
    MixerZonesRig r(2);
    const auto id = r.stripId(0);

    std::vector<std::pair<juce::String, bool>> items;
    r.panel->setShowZoneMenuHookForTest([&](juce::PopupMenu& menu) {
        items.clear();
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            items.emplace_back(it.getItem().text, it.getItem().isEnabled);
    });
    r.panel->showChannelMenuForTest(id);
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].first, "Pin left");
    EXPECT_EQ(items[1].first, "Pin right");
    EXPECT_EQ(items[2].first, "Unpin");
    EXPECT_FALSE(items[2].second) << "nothing to unpin yet";

    r.panel->pinChannel(id, MixerZone::Right);
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Right);
    r.panel->showChannelMenuForTest(id);
    EXPECT_TRUE(items[2].second);

    r.hookMenuToPick("Unpin");
    r.panel->showChannelMenuForTest(id);
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Scrolling);
}

TEST(MixerZonesTests, DraggingARowToAnotherGroupPinsTheChannel) {
    MixerZonesRig r(2);
    const auto id = r.stripId(0);
    auto drag = dragOf(r, id);

    drag.down();
    drag.dragTo(drag.pressY + 400.0f); // far below the last row: the Right group
    EXPECT_TRUE(r.panel->getZonesPaneForTest().isDragActiveForTest());
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Scrolling) << "nothing is committed until the drop";
    drag.up(drag.pressY + 400.0f);

    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Right);
    EXPECT_FALSE(r.panel->getZonesPaneForTest().isDragActiveForTest());
    ASSERT_NE(r.row(id), nullptr);
    EXPECT_GT(r.row(id)->getY(), r.panel->getZonesPaneForTest().getGroupHeaderForTest(MixerZone::Right).getY())
        << "the row now sits under the Right zone heading";

    // ...and back above the first heading: the Left group.
    auto again = dragOf(r, id);
    again.down();
    again.dragTo(-200.0f);
    again.up(-200.0f);
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Left);
}

TEST(MixerZonesTests, EscapeDuringARowDragCommitsNothing) {
    MixerZonesRig r(2);
    const auto id = r.stripId(0);
    const auto serial = r.mc.getUndoManager().getEditSerial();
    auto drag = dragOf(r, id);
    drag.down();
    drag.dragTo(drag.pressY + 400.0f);
    ASSERT_TRUE(r.panel->getZonesPaneForTest().isDragActiveForTest());
    // No window exists headlessly, so the Esc listener is not attached to one; the seam sends the same key.
    r.panel->getZonesPaneForTest().sendEscapeToDragForTest();
    drag.up(drag.pressY + 400.0f);

    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Scrolling);
    EXPECT_EQ(r.mc.getUndoManager().getEditSerial(), serial);
}

TEST(MixerZonesTests, PinningIsOneUndoStepAndMarksTheProjectEdited) {
    MixerZonesRig r(2);
    const auto id = r.stripId(0);
    const auto serial = r.mc.getUndoManager().getEditSerial();

    r.panel->pinChannel(id, MixerZone::Left);
    EXPECT_GT(r.mc.getUndoManager().getEditSerial(), serial) << "the edit serial is the dirty-state funnel";

    ASSERT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Scrolling);
    EXPECT_FALSE(r.panel->getLeftZoneViewportForTest().isVisible()) << "undo rebuilds the mixer";
    ASSERT_TRUE(r.mc.getUndoManager().redo());
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Left);
    EXPECT_TRUE(r.panel->getLeftZoneViewportForTest().isVisible());
}

TEST(MixerZonesTests, ZonesTakeAtMostHalfTheColumnsWidthAndScrollInsideThemselves) {
    MixerZonesRig r(8, 1000);
    std::vector<juce::String> ids;
    for (int i = 0; i < 8; ++i)
        ids.push_back(r.stripId(i));
    for (const auto& id : ids)
        r.panel->pinChannel(id, MixerZone::Left);

    // The side pane takes 200 px, leaving 800 for the columns: half of that is 400, shared with Master's
    // right zone (one column) so the two together stay at the cap.
    const int available = r.panel->getWidth() - r.panel->getSidePane().getOccupiedWidth();
    auto& left = r.panel->getLeftZoneViewportForTest();
    auto& right = r.panel->getRightZoneViewportForTest();
    EXPECT_EQ(left.getWidth() + right.getWidth(), available / 2);
    EXPECT_EQ(right.getWidth(), kPitch) << "the small zone keeps what it needs; the rest goes to the big one";
    EXPECT_GT(left.getViewedComponent()->getWidth(), left.getWidth()) << "the capped zone scrolls inside itself";
    EXPECT_GT(r.panel->getViewportForTest().getWidth(), 0);
    EXPECT_LE(left.getRight(), r.panel->getViewportForTest().getX())
        << "the middle's scrollbar belongs to the middle viewport only";
}

TEST(MixerZonesTests, ColumnDragToReorderStillWorksInsideTheScrollingGroup) {
    MixerZonesRig r(3);
    const auto original = r.trackOrder();
    ASSERT_EQ(original.size(), 3u);
    r.panel->pinChannel(r.stripId(0), MixerZone::Left); // track 0 leaves the scrolling group

    // The scrolling group now holds tracks 1 and 2, in slots 0 and 1.
    synth::ui::MixerColumnComponent* first = nullptr;
    for (int i = 0; i < 3; ++i)
        if (isUnder(r.panel->getStripColumnForTest(i), *r.panel->getViewportForTest().getViewedComponent()) &&
            (first == nullptr || r.panel->getStripColumnForTest(i)->getX() < first->getX()))
            first = r.panel->getStripColumnForTest(i);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->getX(), 0);
    auto& header = first->getHeaderForTest();
    auto& content = *first->getParentComponent();
    const int pressX = first->getX() + header.getX() + 3;
    auto at = [&](int x) { return header.getLocalPoint(&content, juce::Point<float>((float)x, 12.0f)); };

    header.mouseDown(makeClickEvent(header, at(pressX)));
    header.mouseDrag(makeDragEvent(header, at(pressX + kPitch + 30), at(pressX)));
    EXPECT_TRUE(r.panel->isColumnReorderActiveForTest());
    header.mouseUp(makeClickEvent(header, at(pressX + kPitch + 30)));

    const std::vector<synth::TrackId> moved{original[0], original[2], original[1]};
    EXPECT_EQ(r.trackOrder(), moved) << "tracks 1 and 2 swapped; the pinned track 0 stayed put";
}

TEST(MixerZonesTests, AColumnPinnedInAZoneHasNoReorderHooks) {
    MixerZonesRig r(2);
    r.panel->pinChannel(r.stripId(0), MixerZone::Left);
    for (int i = 0; i < 2; ++i) {
        auto* column = r.panel->getStripColumnForTest(i);
        const bool pinned = isUnder(column, *r.panel->getLeftZoneViewportForTest().getViewedComponent());
        EXPECT_EQ(static_cast<bool>(column->getHeaderForTest().reorderHooks.onGrab), !pinned);
    }
}
