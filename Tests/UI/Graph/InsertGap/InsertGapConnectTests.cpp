// InsertGapConnectTests.cpp
//
// A module released in the gap between two connected cards wires itself in series: source -> module -> target, with
// the direct cable replaced (docs/layout/smart-connections.md#insert-in-series). Real library drag calls on a real
// canvas, the gap opening on the way exactly as it does for a user.

#include "InsertGapTestFixture.h"

using namespace insert_gap_test;

namespace {
/** Wires every audio jack of `a` to the same jack of `b`, the way a user patches a stereo chain. */
void wire(Canvas& c, NodeID a, NodeID b) {
    const int jacks = c.card(a)->getModule()->getTotalNumOutputChannels();
    for (int j = 0; j < jacks; ++j)
        c.editor.connectPorts(a, j, b, j, false, false);
}
int linksBetween(Canvas& c, NodeID a, NodeID b) {
    int n = 0;
    for (const auto& conn : c.engine.getGraph().getConnections())
        if (conn.source.nodeID == a && conn.destination.nodeID == b && conn.source.channelIndex < 4)
            ++n;
    return n;
}

void expectSeriesInsert(Canvas& c, NodeID from, NodeID to, NodeID added) {
    ASSERT_NE(added.uid, 0u);
    EXPECT_GT(linksBetween(c, from, added), 0) << "source feeds the new module";
    EXPECT_GT(linksBetween(c, added, to), 0) << "the new module feeds the target";
    EXPECT_EQ(linksBetween(c, from, to), 0) << "the direct cable is replaced";
}
} // namespace

class InsertGapConnect : public ::testing::TestWithParam<int> {}; // spacing between the wired cards

TEST_P(InsertGapConnect, ADropBetweenTwoWiredCardsWiresItInSeries) {
    Canvas c;
    const auto row = c.filterRow(2, 400, 400, GetParam());
    wire(c, row[0], row[1]);
    ASSERT_GT(linksBetween(c, row[0], row[1]), 0);

    const auto p = between(c.rect(row[0]), c.rect(row[1]));
    c.libraryEnter("Delay", p);
    EXPECT_GT(c.editor.getSmartConnections().getSmartSuggestionCount(), 0) << "the hover already offers the insert";
    const auto added = c.libraryDrop("Delay", p);
    c.editor.finishCardGlideForTest();
    expectSeriesInsert(c, row[0], row[1], added);
}

TEST_P(InsertGapConnect, ACanvasCardDroppedBetweenTwoWiredCardsWiresItInSeries) {
    Canvas c;
    const auto row = c.filterRow(2, 400, 400, GetParam());
    const auto extra = c.filter(400, 2400);
    wire(c, row[0], row[1]);
    c.editor.finishCardGlideForTest();

    c.press(extra);
    c.dragCentreTo(extra, between(c.rect(row[0]), c.rect(row[1])));
    c.release(extra);
    c.editor.finishCardGlideForTest();
    expectSeriesInsert(c, row[0], row[1], extra);
}

INSTANTIATE_TEST_SUITE_P(Spacing, InsertGapConnect, ::testing::Values(12, 40, 80, 200, 320, 400));
