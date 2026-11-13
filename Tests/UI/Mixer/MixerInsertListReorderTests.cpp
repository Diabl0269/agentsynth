// MixerInsertListReorderTests.cpp (docs/mixer/mixer.md#reordering-inserts): dragging an insert row by its name,
// driven through real synthesized mouseDown/mouseDrag/mouseUp like MixerSendListDragTests.cpp. The preview at the
// last drag event must equal what the drop commits; a split chain moves a row only within its own path; the front
// of a bus's chain is not offered.
#include "MixerInsertListSplitFixture.h"

namespace {
using namespace mixer_split_test;

juce::MouseEvent listEvent(juce::Component& comp, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                           int clicks = 1) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), clicks,
                            dragged);
}

constexpr float kNameX = 5.0f;
constexpr float kRowH = 18.0f;

float rowCentre(int row) { return (float)row * kRowH + kRowH * 0.5f; }

/** Source -> Filter -> Delay -> Reverb -> strip. */
struct ChainRig : SplitListRig {
    NodeID filter, delay, reverb;
    ChainRig() {
        filter = add("Filter", 300, 400);
        delay = add("Delay", 600, 400);
        reverb = add("Reverb", 900, 400);
        wire(source, filter);
        wire(filter, delay);
        wire(delay, reverb);
        wire(reverb, strip);
        refresh();
    }

    std::vector<juce::String> order() {
        std::vector<juce::String> names;
        for (const auto& e : column().inserts)
            names.push_back(e.name);
        return names;
    }

    /** Presses row `from`'s name, drags to row `to`'s centre (plus `extra`), and returns the last preview. */
    std::vector<juce::String> dragTo(int from, float targetY, bool release) {
        const juce::Point<float> down(kNameX, rowCentre(from));
        list.mouseDown(listEvent(list, down, down, false));
        const juce::Point<float> drop(kNameX, targetY);
        list.mouseDrag(listEvent(list, drop, down, true));
        auto preview = list.getDragPreviewNamesForTest();
        if (release)
            list.mouseUp(listEvent(list, drop, down, true));
        return preview;
    }
};
} // namespace

TEST(MixerInsertListReorderTests, ARealDragReordersAsOneUndoStepAndThePreviewIsWhatTheDropCommits) {
    ChainRig rig;
    ASSERT_EQ(rig.order(), (std::vector<juce::String>{"Filter", "Delay", "Reverb"}));

    const auto preview = rig.dragTo(0, rowCentre(2) + 6.0f, true);
    EXPECT_EQ(preview, (std::vector<juce::String>{"Delay", "Reverb", "Filter"})) << "the rows made room";
    EXPECT_EQ(rig.order(), preview) << "preview and commit agree";
    EXPECT_EQ(rig.mutations, 1);

    rig.undo.undo();
    rig.refresh();
    EXPECT_EQ(rig.order(), (std::vector<juce::String>{"Filter", "Delay", "Reverb"})) << "one step undoes it";
}

TEST(MixerInsertListReorderTests, DraggingUpwardsAndToAMiddleSlotAgreeWithThePreviewToo) {
    ChainRig rig;
    const auto preview = rig.dragTo(2, rowCentre(1) - 4.0f, true);
    EXPECT_EQ(preview, (std::vector<juce::String>{"Filter", "Reverb", "Delay"}));
    EXPECT_EQ(rig.order(), preview);
}

TEST(MixerInsertListReorderTests, EscapeCancelsTheDragAndCommitsNothing) {
    ChainRig rig;
    const juce::Point<float> down(kNameX, rowCentre(0));
    rig.list.mouseDown(listEvent(rig.list, down, down, false));
    const juce::Point<float> drop(kNameX, rowCentre(2) + 6.0f);
    rig.list.mouseDrag(listEvent(rig.list, drop, down, true));
    ASSERT_TRUE(rig.list.isRowDragActiveForTest());
    EXPECT_TRUE(rig.list.sendEscapeToRowDragForTest());
    rig.list.mouseUp(listEvent(rig.list, drop, down, true));
    EXPECT_EQ(rig.mutations, 0);
    EXPECT_EQ(rig.order(), (std::vector<juce::String>{"Filter", "Delay", "Reverb"}));
}

TEST(MixerInsertListReorderTests, APressThatNeverCrossesTheThresholdIsAPlainClick) {
    ChainRig rig;
    const juce::Point<float> down(kNameX, rowCentre(0));
    rig.list.mouseDown(listEvent(rig.list, down, down, false));
    rig.list.mouseUp(listEvent(rig.list, down, down, false));
    EXPECT_EQ(rig.mutations, 0);
    EXPECT_FALSE(rig.list.isRowDragActiveForTest());
}

TEST(MixerInsertListReorderTests, APressOnTheBypassButtonsSideIsNotAGrab) {
    ChainRig rig;
    const juce::Point<float> down((float)rig.list.getWidth() - 5.0f, rowCentre(0));
    rig.list.mouseDown(listEvent(rig.list, down, down, false));
    rig.list.mouseDrag(listEvent(rig.list, {down.x, rowCentre(2)}, down, true));
    EXPECT_FALSE(rig.list.isRowDragActiveForTest());
}

TEST(MixerInsertListReorderTests, TheDropSettlesTheMovedRowFromWhereItWasLetGo) {
    ChainRig rig;
    int settledRow = -1;
    float settledFrom = -1.0f;
    rig.list.onSettlePending = [&](int row, float fromY) {
        settledRow = row;
        settledFrom = fromY;
    };
    rig.dragTo(0, rowCentre(2) + 6.0f, true);
    EXPECT_EQ(settledRow, 2);
    EXPECT_GT(settledFrom, 0.0f);
    rig.list.startSettleFrom(settledRow, settledFrom);
    EXPECT_EQ(rig.list.getLastSettleForTest().row, 2);
}

TEST(MixerInsertListReorderTests, TheCommitHoldsThePanelRebuildAroundTheUndoStep) {
    ChainRig rig;
    int depth = 0, maxDepth = 0;
    rig.list.holdRebuild = [&](int delta) {
        depth += delta;
        maxDepth = std::max(maxDepth, depth);
    };
    rig.dragTo(0, rowCentre(2) + 6.0f, true);
    EXPECT_EQ(maxDepth, 1);
    EXPECT_EQ(depth, 0) << "released before onMutated runs the one rebuild";
}

/** Source -> Filter -> {Delay -> Chorus | Reverb} -> VCA -> strip. */
struct PathRig : SplitListRig {
    NodeID filter, delay, chorus, reverb, vca;
    PathRig() {
        filter = add("Filter", 300, 400);
        delay = add("Delay", 600, 250);
        chorus = add("Chorus", 750, 250);
        reverb = add("Reverb", 600, 550);
        vca = add("VCA", 900, 400);
        wire(source, filter);
        wire(filter, delay);
        wire(delay, chorus);
        wire(filter, reverb);
        wire(chorus, vca);
        wire(reverb, vca);
        wire(vca, strip);
        refresh();
        list.setSplitOpen(1, true); // Filter, Split, then the paths
    }
};

TEST(MixerInsertListReorderTests, OnASplitChainARowMovesOnlyWithinItsOwnPath) {
    PathRig rig;
    // Rows: Filter, Split, Delay, Chorus, Reverb, VCA.
    ASSERT_EQ(rig.list.describeRow(2), "Insert 3, Delay, path 1");

    const juce::Point<float> down(kNameX, rowCentre(2));
    rig.list.mouseDown(listEvent(rig.list, down, down, false));
    // Far past the end of the path, into Reverb's row and below.
    const juce::Point<float> drop(kNameX, rowCentre(5));
    rig.list.mouseDrag(listEvent(rig.list, drop, down, true));
    const auto preview = rig.list.getDragPreviewNamesForTest();
    EXPECT_EQ(preview, (std::vector<juce::String>{"Filter", "2 paths", "Chorus", "Delay", "Reverb", "VCA"}))
        << "clamped to the path";
    rig.list.mouseUp(listEvent(rig.list, drop, down, true));

    EXPECT_EQ(rig.mutations, 1);
    EXPECT_TRUE(rig.wired(rig.chorus, rig.delay));
    EXPECT_TRUE(rig.wired(rig.delay, rig.vca));
    EXPECT_TRUE(rig.wired(rig.filter, rig.chorus));
    EXPECT_FALSE(rig.wired(rig.filter, rig.delay));
    EXPECT_TRUE(rig.wired(rig.filter, rig.reverb)) << "the other path is untouched";
}

TEST(MixerInsertListReorderTests, ALoneRowOnAPathAndTheSplitRowCannotBeGrabbed) {
    PathRig rig;
    for (int row : {4, 1}) { // Reverb alone on its path, then the Split row (a click on it folds the split)
        const juce::Point<float> down(kNameX, rowCentre(row));
        rig.list.mouseDown(listEvent(rig.list, down, down, false));
        rig.list.mouseDrag(listEvent(rig.list, {down.x, down.y + 30.0f}, down, true));
        EXPECT_FALSE(rig.list.isRowDragActiveForTest()) << row;
        rig.list.mouseUp(listEvent(rig.list, {down.x, down.y + 30.0f}, down, true));
    }
    EXPECT_EQ(rig.mutations, 0);
}

TEST(MixerInsertListReorderTests, TheFrontOfABusChainIsNeverOffered) {
    ChainRig rig;
    const auto column = rig.column();
    // A bus chain has no source feeding it: nothing to splice a new first module against.
    rig.list.setEntries(column.inserts, true, NodeID{}, rig.strip);
    EXPECT_FALSE(rig.list.canMoveRowForTest(0, 1)) << "the first row cannot come out";
    EXPECT_FALSE(rig.list.canMoveRowForTest(1, -1)) << "nor can another go in front of it";
    EXPECT_TRUE(rig.list.canMoveRowForTest(1, 1));

    const juce::Point<float> down(kNameX, rowCentre(2));
    rig.list.mouseDown(listEvent(rig.list, down, down, false));
    const juce::Point<float> drop(kNameX, -20.0f);
    rig.list.mouseDrag(listEvent(rig.list, drop, down, true));
    EXPECT_EQ(rig.list.getDragPreviewNamesForTest(), (std::vector<juce::String>{"Filter", "Reverb", "Delay"}))
        << "the top of the drag stops at the second place";
    rig.list.mouseUp(listEvent(rig.list, drop, down, true));
}

TEST(MixerInsertListReorderTests, KeyboardMoveTargetsFollowTheSameRules) {
    ChainRig rig;
    EXPECT_EQ(rig.list.keyboardMoveTarget(0, -1), -1);
    EXPECT_EQ(rig.list.keyboardMoveTarget(0, 1), 1);
    EXPECT_EQ(rig.list.keyboardMoveTarget(2, 1), -1) << "the + Insert row is not a place to move to";
    EXPECT_TRUE(rig.list.moveRowTo(0, 1));
    EXPECT_EQ(rig.order(), (std::vector<juce::String>{"Delay", "Filter", "Reverb"}));
}

// ---- Show on canvas (docs/mixer/edit-on-canvas.md#insert-and-send-rows) ----

TEST(MixerInsertListReorderTests, DoubleClickingARowAsksToShowItsModule) {
    ChainRig rig;
    juce::String shown;
    rig.list.onFocusModule = [&](const juce::String& uuid) { shown = uuid; };
    const juce::Point<float> at(kNameX, rowCentre(1));
    rig.list.mouseDown(listEvent(rig.list, at, at, false, 2));
    rig.list.mouseUp(listEvent(rig.list, at, at, false, 2));
    rig.list.mouseDoubleClick(listEvent(rig.list, at, at, false, 2));
    EXPECT_EQ(shown, rig.column().inserts[1].uuid);
    EXPECT_FALSE(shown.isEmpty());
    EXPECT_EQ(rig.mutations, 0) << "a double-click moves nothing";
}

TEST(MixerInsertListReorderTests, EveryModuleRowHasAShowButtonThatDoesTheSame) {
    ChainRig rig;
    juce::StringArray shown;
    rig.list.onFocusModule = [&](const juce::String& uuid) { shown.add(uuid); };
    const auto column = rig.column();
    for (int row = 0; row < 3; ++row) {
        auto* button = rig.list.getShowButtonForTest(row);
        ASSERT_NE(button, nullptr) << row;
        EXPECT_EQ(button->getTitle(), "Show " + column.inserts[(size_t)row].name + " on canvas");
        EXPECT_FALSE(button->getTooltip().isEmpty());
        EXPECT_TRUE(button->getWantsKeyboardFocus());
        EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    }
    ASSERT_EQ(shown.size(), 3);
    EXPECT_EQ(shown[2], column.inserts[2].uuid);
}

TEST(MixerInsertListReorderTests, TheRowButtonsNeverOverlapEachOtherOrTheNameAtTheColumnWidth) {
    ChainRig rig;
    for (int row = 0; row < 3; ++row) {
        const auto show = rig.list.getShowButtonForTest(row)->getBounds();
        const auto bypass = rig.list.getBypassButtonForTest(row)->getBounds();
        EXPECT_FALSE(show.intersects(bypass)) << row;
        EXPECT_LE(bypass.getRight(), rig.list.getWidth());
        EXPECT_GE(show.getX(), 40) << "the name keeps room to read";
        EXPECT_EQ(show.getY() / 18, bypass.getY() / 18) << "same row";
    }
}

TEST(MixerInsertListReorderTests, SplitAndDryRowsHaveNoShowButton) {
    PathRig rig;
    EXPECT_EQ(rig.list.getShowButtonForTest(1), nullptr);
    EXPECT_NE(rig.list.getShowButtonForTest(2), nullptr);
}
