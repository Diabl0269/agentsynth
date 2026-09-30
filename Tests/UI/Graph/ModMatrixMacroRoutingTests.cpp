// ModMatrixMacroRoutingTests.cpp
//
// Re-pointing a Mod Matrix row enters and leaves macros through ports, exactly like dragging the
// cable would (docs/macros/auto-ports.md#programmatic-connections): one undo step for the edge and
// the ports, the row still naming the real modules, the preference and the auto-delete sweep
// honoured. Every test drives the real comboBoxChanged path by selecting a combo id.

#include "ModMatrixCanvasHelpers.h"

#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include <gtest/gtest.h>

namespace {
juce::Component* findDescendantWithID(juce::Component& root, const juce::String& id) {
    for (auto* child : root.getChildren()) {
        if (child->getComponentID() == id)
            return child;
        if (auto* found = findDescendantWithID(*child, id))
            return found;
    }
    return nullptr;
}
} // namespace

TEST(ModMatrixMacroRouting, PointingARowAtAMemberEntersTheMacroThroughAnInletAndTheRowStillNamesTheMember) {
    MatrixCanvas c(Boxed::DestInside);
    const auto atten = c.addRow();
    ASSERT_TRUE(atten.uid != 0);
    c.pickSource(c.lfo);
    c.pickDest(c.filterIn);

    const auto inlets = c.nodesOf<MacroInletModule>();
    ASSERT_EQ(inlets.size(), 1u) << "the crossing mints one inlet";
    // The same topology a dragged cable builds: the attenuverter feeds the real destination.
    EXPECT_TRUE(c.edge(c.lfo, inlets[0]));
    EXPECT_TRUE(c.edge(inlets[0], atten));
    EXPECT_TRUE(c.edge(atten, c.filterIn, kCutoff));
    EXPECT_FALSE(c.edge(atten, inlets[0])) << "no cable cuts straight into the macro or ends on the port";
    ASSERT_EQ(c.macro().ports.size(), 1u);
    EXPECT_TRUE(c.macro().memberIsPort(c.macro().ports.front().nodeUuid));

    const auto sourceText = c.matrix().getRowSourceComboTextForTest(0);
    EXPECT_TRUE(sourceText.contains(c.nameOf(c.lfo)))
        << "the row names the LFO, not the port it enters by: " << sourceText;
    const auto destText = c.matrix().getRowDestComboTextForTest(0);
    EXPECT_TRUE(destText.contains(c.nameOf(c.filterIn))) << destText;
    EXPECT_TRUE(destText.contains("Cutoff")) << destText;
}

TEST(ModMatrixMacroRouting, OneUndoRemovesTheEdgeAndThePort) {
    MatrixCanvas c(Boxed::DestInside);
    c.addRow();
    c.pickSource(c.lfo);
    const int nodesBefore = c.engine.getGraph().getNumNodes();
    c.pickDest(c.filterIn);
    ASSERT_EQ(c.nodesOf<MacroInletModule>().size(), 1u);

    ASSERT_TRUE(c.undo.undo());

    EXPECT_TRUE(c.nodesOf<MacroInletModule>().empty());
    EXPECT_EQ(c.engine.getGraph().getNumNodes(), nodesBefore);
    EXPECT_TRUE(c.macro().ports.empty());
    for (const auto& conn : c.engine.getGraph().getConnections())
        EXPECT_NE(conn.destination.nodeID, c.filterIn) << "the member has no cable left";
}

TEST(ModMatrixMacroRouting, WithAutoPortsOffTheCableGoesStraightIn) {
    MatrixCanvas c(Boxed::DestInside);
    c.editor.setAutoCreateMacroPortsOnDragEnabled(false);
    const auto atten = c.addRow();
    c.pickSource(c.lfo);
    c.pickDest(c.filterIn);

    EXPECT_TRUE(c.nodesOf<MacroInletModule>().empty());
    EXPECT_TRUE(c.edge(c.lfo, atten));
    EXPECT_TRUE(c.edge(atten, c.filterIn, kCutoff));
    EXPECT_TRUE(c.macro().ports.empty());
}

TEST(ModMatrixMacroRouting, PointingAwayFromTheMacroSweepsThePortItNoLongerNeeds) {
    MatrixCanvas c(Boxed::DestInside);
    const auto atten = c.addRow();
    c.pickSource(c.lfo);
    c.pickDest(c.filterIn);
    ASSERT_EQ(c.nodesOf<MacroInletModule>().size(), 1u);

    c.pickDest(c.filterOut);

    EXPECT_TRUE(c.nodesOf<MacroInletModule>().empty()) << "the orphaned inlet goes";
    EXPECT_TRUE(c.macro().ports.empty());
    EXPECT_TRUE(c.edge(c.lfo, atten));
    EXPECT_TRUE(c.edge(atten, c.filterOut, kCutoff));
    EXPECT_FALSE(c.edge(atten, c.filterIn, kCutoff));
}

TEST(ModMatrixMacroRouting, ChangingTheSourceKeepsTheRealDestinationBehindItsPort) {
    MatrixCanvas c(Boxed::DestInside);
    const auto atten = c.addRow();
    c.pickSource(c.lfo);
    c.pickDest(c.filterIn);
    const auto otherLfo = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), "LFO2", 100, 500);
    c.matrix().updateRowsFromGraph();

    c.pickSource(otherLfo);

    const auto inlets = c.nodesOf<MacroInletModule>();
    ASSERT_EQ(inlets.size(), 1u);
    EXPECT_TRUE(c.edge(otherLfo, inlets[0]));
    EXPECT_TRUE(c.edge(inlets[0], atten));
    EXPECT_TRUE(c.edge(atten, c.filterIn, kCutoff));
    EXPECT_FALSE(c.edge(c.lfo, atten));
    EXPECT_FALSE(c.edge(c.lfo, inlets[0])) << "the old feed is gone with the old port";
}

TEST(ModMatrixMacroRouting, ASourceInsideAMacroLeavesThroughAnOutlet) {
    MatrixCanvas c(Boxed::SourceInside);
    const auto atten = c.addRow();
    c.pickSource(c.lfo);
    c.pickDest(c.filterOut);

    const auto outlets = c.nodesOf<MacroOutletModule>();
    ASSERT_EQ(outlets.size(), 1u);
    EXPECT_TRUE(c.edge(c.lfo, outlets[0]));
    EXPECT_TRUE(c.edge(outlets[0], atten));
    EXPECT_TRUE(c.edge(atten, c.filterOut, kCutoff));
    ASSERT_EQ(c.macro().ports.size(), 1u);
    EXPECT_FALSE(c.macro().ports.front().isInput);
    EXPECT_TRUE(c.matrix().getRowDestComboTextForTest(0).contains(c.nameOf(c.filterOut)));
    EXPECT_EQ(c.matrix().getRowSourceComboForTest(0)->getSelectedId(), (int)(c.lfo.uid << 8))
        << "the row names the member inside, not the outlet it leaves by";
}

TEST(ModMatrixMacroRouting, BothEndsInsideTheSameMacroNeedNoPorts) {
    MatrixCanvas c(Boxed::DestInside);
    c.addRow();
    c.pickSource(c.spare);
    c.pickDest(c.filterIn);

    EXPECT_TRUE(c.nodesOf<MacroInletModule>().empty());
    EXPECT_TRUE(c.nodesOf<MacroOutletModule>().empty());
    EXPECT_TRUE(c.macro().ports.empty());
}

TEST(ModMatrixMacroRouting, AddingAnEmptyRowIsOneUndoStep) {
    MatrixCanvas c(Boxed::DestInside);
    ASSERT_TRUE(c.addRow().uid != 0);
    ASSERT_EQ(c.nodesOf<AttenuverterModule>().size(), 1u);

    ASSERT_TRUE(c.undo.undo());

    EXPECT_TRUE(c.nodesOf<AttenuverterModule>().empty());
}

TEST(ModMatrixMacroRouting, DeletingARowTakesThePortItUsedWithIt) {
    MatrixCanvas c(Boxed::DestInside);
    const auto atten = c.addRow();
    c.pickSource(c.lfo);
    c.pickDest(c.filterIn);
    ASSERT_EQ(c.nodesOf<MacroInletModule>().size(), 1u);

    auto* del = dynamic_cast<juce::Button*>(findDescendantWithID(c.matrix(), "modDelete"));
    ASSERT_NE(del, nullptr);
    del->onClick();

    EXPECT_EQ(c.engine.getGraph().getNodeForId(atten), nullptr);
    EXPECT_TRUE(c.nodesOf<MacroInletModule>().empty());
    EXPECT_TRUE(c.macro().ports.empty());

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.nodesOf<MacroInletModule>().size(), 1u) << "one undo brings back the row and its port";
}
