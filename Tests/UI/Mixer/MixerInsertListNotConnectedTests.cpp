// MixerInsertListNotConnectedTests.cpp (docs/mixer/mixer.md#not-connected-modules): a module hanging off the chain that
// leads nowhere is drawn as a dimmed row in place, named "not connected" for a screen reader, outside the row index
// space (no focus, no Alt-move, no drag lands on it) and revealed on the canvas by a double-click.
#include "MixerInsertListSplitFixture.h"

using namespace mixer_split_test;

namespace {
juce::MouseEvent mouseAt(juce::Component& component, juce::Point<int> position, int clicks = 1) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &component, &component, juce::Time::getCurrentTime(), position.toFloat(),
                            juce::Time::getCurrentTime(), clicks, false);
}

// Source -> Filter -> Chorus -> strip, with a Delay hung off the Filter that leads nowhere.
struct HungPatch : SplitListRig {
    NodeID filter, chorus, hung;
    HungPatch() {
        filter = add("Filter", 300, 400);
        chorus = add("Chorus", 600, 400);
        hung = add("Delay", 600, 600);
        wire(source, filter);
        wire(filter, chorus);
        wire(chorus, strip);
        wire(filter, hung);
        refresh();
    }
    juce::String hungUuid() const {
        return dynamic_cast<ModuleBase*>(graph.getNodeForId(hung)->getProcessor())->getNodeUuid();
    }
    juce::Image paintImage() {
        list.setSize(160, list.getPreferredHeight());
        return list.createComponentSnapshot(list.getLocalBounds(), false, 1.0f);
    }
};
} // namespace

TEST(MixerInsertListNotConnected, TheMarkedRowSitsRightAfterTheRowItHangsOffAndTakesNoRowIndex) {
    HungPatch rig;
    ASSERT_EQ(rig.list.getDanglingCount(), 1);
    // Rows: Filter, Chorus, "+ Insert": the not-connected row is none of them.
    EXPECT_EQ(rig.list.getRowCount(), 3);
    const auto filterRow = rig.list.getRowBounds(0);
    const auto hungRow = rig.list.getDanglingBounds(0);
    const auto chorusRow = rig.list.getRowBounds(1);
    EXPECT_EQ(hungRow.getY(), filterRow.getBottom()) << "in place, right below Filter";
    EXPECT_EQ(chorusRow.getY(), hungRow.getBottom()) << "Chorus makes room below it";
    EXPECT_EQ(rig.list.getPreferredHeight(), 4 * 18);
}

TEST(MixerInsertListNotConnected, TheMarkedRowIsDrawnDimmedWithADashedRailUnlikeAChainRow) {
    HungPatch rig;
    const auto image = rig.paintImage();
    const auto hung = rig.list.getDanglingBounds(0);
    const auto chain = rig.list.getRowBounds(0);
    const auto inkOf = [&](juce::Rectangle<int> row) {
        float brightest = 0.0f;
        for (int y = row.getY(); y < row.getBottom(); ++y)
            for (int x = 2; x < 100; ++x)
                brightest = juce::jmax(brightest, image.getPixelAt(x, y).getPerceivedBrightness());
        return brightest;
    };
    EXPECT_LT(inkOf(hung), inkOf(chain)) << "the name is dimmer than a chain row's";
    // The rail is dashed: some rows of pixels on its column are ink and some are the background.
    int ink = 0, gaps = 0;
    const int railX = 6;
    const auto background = image.getPixelAt(railX + 40, hung.getY() + 1);
    for (int y = hung.getY(); y < hung.getBottom(); ++y)
        (image.getPixelAt(railX, y) != background ? ink : gaps)++;
    EXPECT_GT(ink, 0);
    EXPECT_GT(gaps, 0) << "dashed, not solid";
}

TEST(MixerInsertListNotConnected, AScreenReaderHearsNotConnectedAndNeverGetsAChainRowForIt) {
    HungPatch rig;
    EXPECT_EQ(rig.list.describeDangling(0), "Delay, not connected");
    ASSERT_NE(rig.list.getDanglingAccessibilityComponentForTest(0), nullptr);
    EXPECT_EQ(rig.list.getDanglingAccessibilityComponentForTest(0)->getTitle(), "Delay, not connected");
    for (int row = 0; row < rig.list.getRowCount(); ++row)
        EXPECT_FALSE(rig.list.describeRow(row).contains("Delay")) << "focus rows skip it";
}

TEST(MixerInsertListNotConnected, ADoubleClickOnTheMarkedRowRevealsItOnTheCanvas) {
    HungPatch rig;
    juce::String revealed;
    rig.list.onFocusModule = [&](const juce::String& uuid) { revealed = uuid; };
    const auto hung = rig.list.getDanglingBounds(0);
    rig.list.mouseDoubleClick(mouseAt(rig.list, hung.getCentre(), 2));
    EXPECT_EQ(revealed, rig.hungUuid());
}

// The tooltip over the marked row says it is not in the sound and how to find it, never the empty-space text.
TEST(MixerInsertListNotConnected, TheTooltipOverTheMarkedRowSaysItIsNotConnected) {
    HungPatch rig;
    const auto hung = rig.list.getDanglingBounds(0);
    for (const int x : {4, 40, 120}) {
        const auto tip = rig.list.tooltipAt({x, hung.getCentreY()});
        EXPECT_TRUE(tip.contains("Delay is not connected")) << tip;
        EXPECT_TRUE(tip.contains("double-click")) << tip;
        EXPECT_FALSE(tip.contains("Insert chain")) << tip;
    }
    // A chain row beside it keeps its own tooltip.
    EXPECT_FALSE(rig.list.tooltipAt({40, rig.list.getRowBounds(0).getCentreY()}).contains("not connected"));
}

TEST(MixerInsertListNotConnected, AClickOrRightClickOnTheMarkedRowChangesNothing) {
    HungPatch rig;
    const auto hung = rig.list.getDanglingBounds(0);
    const auto before = rig.rows();
    rig.list.mouseDown(mouseAt(rig.list, hung.getCentre()));
    rig.list.mouseUp(mouseAt(rig.list, hung.getCentre()));
    EXPECT_EQ(rig.rows(), before);
    EXPECT_EQ(rig.mutations, 0);
}

// Alt+Up/Down moves the chain row past the marked row as if it were not there.
TEST(MixerInsertListNotConnected, AKeyboardMoveSkipsTheMarkedRow) {
    HungPatch rig;
    EXPECT_EQ(rig.list.keyboardMoveTarget(0, 1), 1);
    rig.list.moveRow(0, 1);
    const auto rows = rig.rows();
    ASSERT_GE(rows.size(), 2u);
    EXPECT_TRUE(rows[0].contains("Chorus"));
    EXPECT_TRUE(rows[1].contains("Filter"));
    EXPECT_EQ(rig.list.getDanglingCount(), 1) << "the Delay still hangs off the Filter";
    EXPECT_TRUE(rig.wired(rig.filter, rig.hung));
}

// The gap "+" sits on the line above the next chain row, below the not-connected row: it adds on the chain's own cable.
TEST(MixerInsertListNotConnected, TheAddGapBelowTheMarkedRowStillAddsOnTheChainCable) {
    HungPatch rig;
    const auto target = rig.list.gapTargetAt({20, rig.list.getRowBounds(1).getY()});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->predecessor, rig.filter);
    EXPECT_EQ(target->successor, rig.chorus);
}

TEST(MixerInsertListNotConnected, ANotConnectedModuleOffTheSourceSitsAboveTheFirstRow) {
    SplitListRig rig;
    const auto filter = rig.add("Filter", 300, 400);
    const auto hung = rig.add("Delay", 300, 600);
    rig.wire(rig.source, filter);
    rig.wire(filter, rig.strip);
    rig.wire(rig.source, hung);
    rig.refresh();
    ASSERT_EQ(rig.list.getDanglingCount(), 1);
    EXPECT_EQ(rig.list.getDanglingBounds(0).getY(), 0);
    EXPECT_EQ(rig.list.getRowBounds(0).getY(), 18);
    EXPECT_EQ(rig.list.danglingIndexAt({20, 5}), 0);
    EXPECT_EQ(rig.list.danglingIndexAt({20, 25}), -1);
}
