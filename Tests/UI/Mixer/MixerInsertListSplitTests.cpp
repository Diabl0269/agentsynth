// MixerInsertListSplitTests.cpp (docs/mixer/mixer.md#parallel-paths): a split chain in the mixer's insert list -- one
// split row that folds open, its tooltip, bypassing every path at once, moving and removing inside a path, and the
// open state surviving the column's rebuild.
#include "MixerInsertListSplitFixture.h"

using namespace mixer_split_test;

namespace {
juce::MouseEvent clickAt(juce::Component& component, juce::Point<int> position) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &component, &component, juce::Time::getCurrentTime(), position.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}
} // namespace

TEST(MixerInsertListSplitTests, TheSplitIsOneFoldedRowWithItsPathsHidden) {
    TicketPatch p;
    EXPECT_TRUE(p.list.isEditable());
    EXPECT_EQ(p.rows(), (std::vector<juce::String>{"Insert 1, Filter", "Insert 2, split into 2 paths, folded",
                                                   "Insert 3, VCA", "Add an insert"}));
    EXPECT_TRUE(p.list.isSplitRow(1));
    EXPECT_FALSE(p.list.isSplitOpen(1));
    EXPECT_TRUE(p.list.getRowUuid(1).isEmpty()) << "a split row is not a module to select";
}

TEST(MixerInsertListSplitTests, AClickOnTheSplitRowOpensItsPathsAndTheOpenStateSurvivesARebuild) {
    TicketPatch p;
    p.list.mouseDown(clickAt(p.list, p.list.getRowBounds(1).getCentre()));
    ASSERT_TRUE(p.list.isSplitOpen(1));
    EXPECT_EQ(p.rows(), (std::vector<juce::String>{"Insert 1, Filter", "Insert 2, split into 2 paths, open",
                                                   "Insert 3, Delay, path 1", "Insert 4, Reverb, path 2",
                                                   "Insert 5, VCA", "Add an insert"}));
    EXPECT_EQ(p.list.getParentSplitRow(3), 1);
    EXPECT_EQ(p.list.getParentSplitRow(4), -1);

    p.refresh(); // what any recorded edit does to the column
    EXPECT_TRUE(p.list.isSplitOpen(1)) << "the open state is the split's, not this list's";

    synth::ui::MixerInsertList other; // the detached mixer's copy of the same column
    const auto c = p.column();
    other.setEntries(c.inserts, false, {}, c.sourceNodeId, c.nodeId);
    EXPECT_TRUE(other.isSplitOpen(1));

    p.list.setSplitOpen(1, false);
    EXPECT_EQ(p.list.getRowCount(), 4);
}

TEST(MixerInsertListSplitTests, TheSplitRowTooltipListsEveryPathAndTheIconSaysSplit) {
    TicketPatch p;
    const auto row = p.list.getRowBounds(1);
    EXPECT_EQ(p.list.tooltipAt(row.getCentre().withX(row.getWidth() - 40)),
              "Split into 2 paths\nPath 1: Delay\nPath 2: Reverb");
    // The split icon sits right after the fold arrow.
    EXPECT_EQ(p.list.tooltipAt({2 + 12 + 6, row.getCentreY()}), "Split");
}

TEST(MixerInsertListSplitTests, ADryPathReadsAsDryInTheTooltip) {
    SplitListRig r;
    const auto reverb = r.add("Reverb", 300, 250);
    const auto vca = r.add("VCA", 600, 400);
    r.wire(r.source, reverb);
    r.wire(r.source, vca);
    r.wire(reverb, vca);
    r.wire(vca, r.strip);
    r.refresh();
    ASSERT_TRUE(r.list.isSplitRow(0));
    EXPECT_EQ(r.list.tooltipAt(r.list.getRowBounds(0).getCentre().withX(100)),
              "Split into 2 paths\nPath 1: Reverb\nPath 2: dry, no effects");
}

TEST(MixerInsertListSplitTests, TheSplitRowBypassesEveryPathAsOneUndoStep) {
    TicketPatch p;
    p.list.toggleBypassForRow(1);
    EXPECT_TRUE(p.bypassed(p.delay));
    EXPECT_TRUE(p.bypassed(p.reverb));
    EXPECT_FALSE(p.bypassed(p.filter)) << "only the modules under the split";
    EXPECT_EQ(p.rows()[1], "Insert 2, split into 2 paths, folded, bypassed");

    ASSERT_TRUE(p.undo.undo());
    EXPECT_FALSE(p.bypassed(p.delay));
    EXPECT_FALSE(p.bypassed(p.reverb)) << "one undo turns both back on";

    // With one path already bypassed, the split row bypasses the rest rather than flipping each.
    p.refresh();
    p.list.setSplitOpen(1, true);
    p.list.toggleBypassForRow(2); // Delay
    ASSERT_TRUE(p.bypassed(p.delay));
    p.list.toggleBypassForRow(1);
    EXPECT_TRUE(p.bypassed(p.delay));
    EXPECT_TRUE(p.bypassed(p.reverb));
    p.list.toggleBypassForRow(1);
    EXPECT_FALSE(p.bypassed(p.delay));
    EXPECT_FALSE(p.bypassed(p.reverb));
}

TEST(MixerInsertListSplitTests, ARowMovesOnlyWithinItsOwnPath) {
    TicketPatch p;
    const auto chorus = p.add("Chorus", 750, 250);
    // Delay -> Chorus on path 1.
    p.graph.removeConnection({{p.delay, 0}, {p.vca, 0}});
    for (const auto& c : p.graph.getConnections())
        if (c.source.nodeID == p.delay && c.destination.nodeID == p.vca)
            p.graph.removeConnection(c);
    p.wire(p.delay, chorus);
    p.wire(chorus, p.vca);
    p.refresh();
    p.list.setSplitOpen(1, true);
    ASSERT_EQ(p.rows()[3], "Insert 4, Chorus, path 1");

    p.list.moveRow(2, -1); // Delay up: the split row is above it
    p.list.moveRow(4, -1); // Reverb up: Chorus is on the other path
    EXPECT_EQ(p.mutations, 0);

    p.list.moveRow(3, -1); // Chorus up past Delay
    ASSERT_EQ(p.mutations, 1);
    EXPECT_TRUE(p.wired(p.filter, chorus));
    EXPECT_TRUE(p.wired(chorus, p.delay));
    EXPECT_TRUE(p.wired(p.delay, p.vca));
    EXPECT_EQ(p.rows()[2], "Insert 3, Chorus, path 1");

    ASSERT_TRUE(p.undo.undo());
    EXPECT_TRUE(p.wired(p.delay, chorus)) << "one undo restores the order";
}

TEST(MixerInsertListSplitTests, RemovingAPathModuleRewiresItsPathAndTheSplitRowCannotBeRemoved) {
    TicketPatch p;
    p.list.removeRow(1);
    EXPECT_EQ(p.mutations, 0) << "a split row is not a node";

    p.list.setSplitOpen(1, true);
    p.list.removeRow(2); // Delay
    ASSERT_EQ(p.mutations, 1);
    EXPECT_EQ(p.graph.getNodeForId(p.delay), nullptr);
    EXPECT_TRUE(p.wired(p.filter, p.vca)) << "path 1 is now a dry path";
    EXPECT_TRUE(p.list.isSplitRow(1)) << "still two paths: one dry, one through the reverb";
}

TEST(MixerInsertListSplitTests, ABranchTheMixerCannotListStaysReadOnly) {
    SplitListRig r;
    const auto delay = r.add("Delay", 300, 250);
    const auto reverb = r.add("Reverb", 300, 550);
    const auto elsewhere = r.add("Audio Output", 600, 550);
    r.wire(r.source, delay);
    r.wire(r.source, reverb);
    r.wire(delay, r.strip);
    r.graph.addConnection({{reverb, 0}, {elsewhere, 0}});
    r.refresh();
    EXPECT_FALSE(r.list.isEditable());
    EXPECT_FALSE(r.list.hasAddRow());
    EXPECT_EQ(r.list.getAddRowIndex(), -1);
}
