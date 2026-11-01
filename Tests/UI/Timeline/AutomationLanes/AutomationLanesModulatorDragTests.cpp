// AutomationLanesModulatorDragTests.cpp -- dragging a modulator row in the header column: a real press, drag and
// release on the row reorders it within its lane (the others slide aside, a line marks the slot, one undo step, Esc and
// a click change nothing) and a drop on another lane asks the host to move the modulation there (a lane that cannot
// take it is "not here" and changes nothing). The move itself runs against a real MainComponent: the routing lands on
// the new parameter, the amount lane keeps its id and points, the row sits at the drop slot, one undo puts it all back.

#include "../../../TestWait.h"
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesAmountFixture.h"
#include "AutomationLanesModulatorHost.h"

using namespace modulator_rows_test;
using synth::TrackKind;
using synth::ui::ModulatorRow;

namespace {

// A press on a modulator row's title, then drags to screen ys relative to the press; the release is the caller's.
struct RowDrag {
    ModulatorRow& row;
    juce::Point<float> pressLocal;
    juce::Point<int> pressScreen;
    int current = 0;

    explicit RowDrag(ModulatorRow& r)
        : row(r)
        , pressLocal((float)ModulatorRow::kIndent + 30.0f, 5.0f)
        , pressScreen(r.localPointToGlobal(pressLocal.toInt())) {
        row.mouseDown(makeClickEvent(row, pressLocal, leftButton()));
    }
    juce::MouseEvent eventAtScreenY(int y) {
        const auto local = row.getLocalPoint(nullptr, juce::Point<int>(pressScreen.x, y)).toFloat();
        return makeDragEvent(row, local, pressLocal, leftButton());
    }
    void moveBy(int dy, int steps = 6) {
        const int from = pressScreen.y + current;
        for (int i = 1; i <= steps; ++i)
            row.mouseDrag(eventAtScreenY(from + dy * i / steps));
        current += dy;
    }
    // Drags so the pointer ends at screen y.
    void moveToScreenY(int y) { moveBy(y - (pressScreen.y + current)); }
    void release() { row.mouseUp(eventAtScreenY(pressScreen.y + current)); }
};

// Answers every lane with the same routings and records the move calls; `allow` is what canMoveModulatorToLane says.
struct MoveHost : ModulatorHost {
    bool allow = true;
    int moves = 0;
    ModulatorInfo movedInfo;
    synth::LaneId movedTo;
    int movedIndex = -1;
    bool canMoveModulatorToLane(const ModulatorInfo&, synth::LaneId) override { return allow; }
    bool moveModulatorToLane(const ModulatorInfo& info, synth::LaneId lane, int index) override {
        ++moves;
        movedInfo = info;
        movedTo = lane;
        movedIndex = index;
        return true;
    }
};

// Track "Bass" with two open lanes (cutoff, resonance), each shown with the host's routings (LFO 1..3).
template <typename Host>
struct DragScene {
    Host host;
    LanesPanel f;
    synth::TrackId bass;
    synth::LaneId cutoff, res;

    explicit DragScene(int routings = 3) {
        for (int i = 1; i <= routings; ++i)
            host.modulators.push_back(lfoInfo(juce::String(i)));
        f.panel.setTrackHeaderHost(&host);
        bass = f.doc.addTrack(TrackKind::Midi, "Bass");
        cutoff = f.addLane(bass, "cutoff");
        res = f.addLane(bass, "resonance");
        f.panel.setTrackAutomationExpanded(bass, true);
    }
    ~DragScene() { f.panel.setTrackHeaderHost(nullptr); }

    synth::ui::TimelineAutomationLanes& lanes() { return f.panel.getAutomationLanes(); }
    ModulatorRow* row(int i, synth::LaneId lane = {}) {
        return f.panel.modulatorRowForTest(lane.isValid() ? lane : cutoff, i);
    }
    int rowHeight() { return ModulatorRow::kBaseHeight; }
    juce::StringArray sources(synth::LaneId lane = {}) {
        juce::StringArray names;
        for (int i = 0; i < lanes().modulatorCount(lane.isValid() ? lane : cutoff); ++i)
            names.add(row(i, lane)->getInfo().sourceUuid);
        return names;
    }
    const std::vector<juce::String>& stored(synth::LaneId lane) { return f.doc.getLane(lane)->modulatorOrder; }
    // Screen y of `lane`'s modulator rows' top (the slot under its lane row).
    int rowsTopOnScreen(synth::LaneId lane) {
        auto& header = *f.panel.laneHeaderForTest(lane);
        return header.localPointToGlobal(juce::Point<int>(0, header.getHeight())).y;
    }
};

const juce::StringArray kOneTwoThree{"lfo-1", "lfo-2", "lfo-3"};
} // namespace

TEST(AutomationLanesModulatorDragTest, DraggingARowDownPastItsNeighbourReordersItAsOneUndoStep) {
    DragScene<MoveHost> s;
    ASSERT_EQ(s.sources(), kOneTwoThree);
    const auto bandBefore = s.f.panel.getLocalArea(s.f.panel.modulatorBandForTest(s.cutoff, 0),
                                                   s.f.panel.modulatorBandForTest(s.cutoff, 0)->getLocalBounds());

    RowDrag drag(*s.row(0));
    drag.moveBy(s.rowHeight() * 3 / 2); // the lifted row's centre passes the middle of the next slot
    EXPECT_TRUE(s.lanes().isModulatorDragActive());
    EXPECT_TRUE(s.stored(s.cutoff).empty()) << "nothing is committed during the drag";
    EXPECT_LT(s.row(1)->getY(), s.row(0)->getY()) << "the neighbour slid up into the gap";
    EXPECT_NE(s.f.panel.getLocalArea(s.f.panel.modulatorBandForTest(s.cutoff, 0),
                                     s.f.panel.modulatorBandForTest(s.cutoff, 0)->getLocalBounds()),
              bandBefore)
        << "the band follows the lifted row";
    drag.release();

    EXPECT_EQ(s.sources(), (juce::StringArray{"lfo-2", "lfo-1", "lfo-3"}));
    EXPECT_FALSE(s.lanes().isModulatorDragActive());
    EXPECT_GT(s.row(1)->getY(), s.row(0)->getY()) << "the rows stand in the new order";

    ASSERT_TRUE(s.f.undo.undo());
    EXPECT_EQ(s.sources(), kOneTwoThree) << "ONE Cmd+Z puts it back";
    EXPECT_FALSE(s.f.undo.canUndo());
    EXPECT_EQ(s.host.moves, 0) << "a drop inside the lane never asks the host to move anything";
}

TEST(AutomationLanesModulatorDragTest, ADropLineMarksTheSlotAndTheLiftedRowIsDrawnRaised) {
    DragScene<MoveHost> s;
    auto& indicator = s.lanes().modulatorDropIndicator();
    EXPECT_FALSE(indicator.isVisible());

    RowDrag drag(*s.row(0));
    drag.moveBy(s.rowHeight() * 3 / 2);
    const auto drop = s.lanes().modulatorDrop();
    EXPECT_TRUE(drop.valid);
    EXPECT_EQ(drop.lane, s.cutoff);
    EXPECT_EQ(drop.index, 1);
    EXPECT_TRUE(indicator.isVisible());
    // The gap the lifted row would fill starts where the swapped neighbour's old slot ended.
    EXPECT_NEAR(indicator.getY(), s.row(1)->getY() + s.rowHeight(), 2.0);
    EXPECT_EQ(indicator.getHeight(), 2);
    drag.release();
    EXPECT_FALSE(indicator.isVisible()) << "the line goes with the drag";
}

TEST(AutomationLanesModulatorDragTest, EscapeMidDragPutsEverythingBackAndTheReleaseCommitsNothing) {
    DragScene<MoveHost> s;
    const auto revision = s.f.doc.getRevision();

    RowDrag drag(*s.row(0));
    drag.moveBy(s.rowHeight() * 2);
    ASSERT_TRUE(s.lanes().isModulatorDragActive());
    ASSERT_TRUE(s.lanes().sendModulatorDragEscapeForTest());
    EXPECT_FALSE(s.lanes().modulatorDropIndicator().isVisible());
    drag.release();

    EXPECT_EQ(s.sources(), kOneTwoThree);
    EXPECT_EQ(s.f.doc.getRevision(), revision);
    EXPECT_FALSE(s.f.undo.canUndo());
    EXPECT_FALSE(s.lanes().isModulatorDragActive());
    EXPECT_EQ(s.host.moves, 0);
}

TEST(AutomationLanesModulatorDragTest, APressThatNeverPassesTheThresholdIsAClickAndStartsNoDrag) {
    DragScene<MoveHost> s;
    const auto revision = s.f.doc.getRevision();

    RowDrag drag(*s.row(1));
    drag.moveBy(3, 3); // under the 4 px threshold
    EXPECT_FALSE(s.lanes().isModulatorDragActive());
    EXPECT_FALSE(s.lanes().modulatorDropIndicator().isVisible());
    drag.release();

    EXPECT_EQ(s.sources(), kOneTwoThree);
    EXPECT_EQ(s.f.doc.getRevision(), revision);
    EXPECT_FALSE(s.f.undo.canUndo());
}

TEST(AutomationLanesModulatorDragTest, ADropOnAnotherLaneAsksTheHostToMoveItThereAtTheSlotUnderThePointer) {
    DragScene<MoveHost> s;
    const auto mod = s.row(0)->getInfo();
    const int top = s.rowsTopOnScreen(s.res);

    RowDrag drag(*s.row(0));
    drag.moveToScreenY(top + s.rowHeight() * 5 / 4); // between the resonance lane's first and second row
    const auto drop = s.lanes().modulatorDrop();
    EXPECT_EQ(drop.lane, s.res);
    EXPECT_TRUE(drop.valid);
    EXPECT_EQ(drop.index, 1);
    EXPECT_TRUE(s.lanes().modulatorDropIndicator().isVisible());
    EXPECT_EQ(s.row(0)->getAlpha(), 1.0f);
    drag.release();

    EXPECT_EQ(s.host.moves, 1);
    EXPECT_EQ(s.host.movedInfo.key(), mod.key());
    EXPECT_EQ(s.host.movedTo, s.res);
    EXPECT_EQ(s.host.movedIndex, 1);
    EXPECT_TRUE(s.stored(s.cutoff).empty()) << "the pool writes no order itself: the host owns the whole step";
    EXPECT_FALSE(s.f.undo.canUndo());
    EXPECT_FALSE(s.lanes().isModulatorDragActive());
}

TEST(AutomationLanesModulatorDragTest, ALaneThatCannotTakeTheRowIsNotHereNoLineTheRowDimsAndTheDropChangesNothing) {
    DragScene<MoveHost> s;
    s.host.allow = false;
    const int top = s.rowsTopOnScreen(s.res);

    RowDrag drag(*s.row(0));
    drag.moveToScreenY(top + s.rowHeight() * 5 / 4);
    const auto drop = s.lanes().modulatorDrop();
    EXPECT_EQ(drop.lane, s.res);
    EXPECT_FALSE(drop.valid);
    EXPECT_FALSE(s.lanes().modulatorDropIndicator().isVisible()) << "no slot is shown";
    EXPECT_LT(s.row(0)->getAlpha(), 1.0f) << "the lifted row dims";
    drag.release();

    EXPECT_EQ(s.host.moves, 0);
    EXPECT_EQ(s.sources(), kOneTwoThree);
    EXPECT_FALSE(s.f.undo.canUndo());
    EXPECT_EQ(s.row(0)->getAlpha(), 1.0f);
}

//==============================================================================
// Against a real MainComponent: a Cutoff lane carrying an LFO (its amount lane holds drawn points) and an empty
// Resonance lane, then a lane on a parameter that has no CV jack.

namespace {
struct MoveScene : amount_test::AmountScene {
    synth::LaneId resLane;
    std::vector<synth::AutomationLane::Breakpoint> points{{0.0, 0.25, 0.0f, amount_test::kHold},
                                                          {4.0, -0.5, 0.0f, amount_test::kHold}};
    MoveScene() {
        resLane = doc().addLane(track, targetUuid, "resonance", {0.0f, 1.0f, 0.5f});
        undo().recordTimelineChange(doc(), [&] { synth::ui::writeAmountLane(doc(), track, attenUuid, points); });
        undo().clearUndoHistory();
        mc.getGraphEditor().updateComponents();
        panel().refreshModulators();
    }
    juce::String attenInto(const juce::String& source, const juce::String& paramId) {
        const auto chains = chainsInto(source, channelFor(paramId));
        return chains.size() == 1 ? juce::String(synth::AIStateMapper::ensureNodeUuid(
                                        graph().getNodeForId(chains.front().attenuverterNodeID)))
                                  : juce::String();
    }
    int rowsTopOnScreen(synth::LaneId l) {
        auto& header = *panel().laneHeaderForTest(l);
        return header.localPointToGlobal(juce::Point<int>(0, header.getHeight())).y;
    }
    void flushUi() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }
};
} // namespace

TEST_F(TimelinePanelIntegrationTest, DroppingTheLfoRowOnTheResonanceLaneMovesTheModulationThereAndOneUndoRestoresIt) {
    MoveScene s;
    ASSERT_EQ(s.panel().getAutomationLanes().modulatorCount(s.lane), 1);
    const auto cutoff = s.channelFor("cutoff");
    const auto resonance = s.channelFor("resonance");
    const auto oldAtten = s.attenUuid;
    const auto amountId = s.amountLane()->id;
    ASSERT_GE(resonance, 0);

    RowDrag drag(*s.row());
    drag.moveToScreenY(s.rowsTopOnScreen(s.resLane) - 8); // inside the resonance lane's own row: the first slot
    EXPECT_EQ(s.panel().getAutomationLanes().modulatorDrop().lane, s.resLane);
    EXPECT_TRUE(s.panel().getAutomationLanes().modulatorDrop().valid);
    drag.release();
    synth::test::waitUntil([&] { return (s.chainsInto(s.lfoUuid, resonance).size()) == (1u); });

    EXPECT_EQ(s.chainsInto(s.lfoUuid, resonance).size(), 1u) << "the LFO now drives Resonance";
    EXPECT_EQ(s.chainsInto(s.lfoUuid, cutoff).size(), 0u);
    const auto newAtten = s.attenInto(s.lfoUuid, "resonance");
    ASSERT_TRUE(newAtten.isNotEmpty());
    const auto* amount = synth::ui::amountLaneFor(s.doc(), newAtten);
    ASSERT_NE(amount, nullptr);
    EXPECT_EQ(amount->id, amountId) << "the amount lane keeps its id";
    ASSERT_EQ(amount->points.size(), s.points.size());
    EXPECT_DOUBLE_EQ(amount->points[1].value, s.points[1].value);
    EXPECT_EQ(s.panel().getAutomationLanes().modulatorCount(s.lane), 0);
    ASSERT_EQ(s.panel().getAutomationLanes().modulatorCount(s.resLane), 1);
    EXPECT_EQ(s.panel().modulatorRowForTest(s.resLane, 0)->getInfo().sourceUuid, s.lfoUuid);
    EXPECT_EQ(s.doc().getLane(s.resLane)->modulatorOrder, (std::vector<juce::String>{newAtten}));

    ASSERT_TRUE(s.undo().undo());
    EXPECT_FALSE(s.undo().canUndo()) << "ONE step";
    synth::test::waitUntil([&] { return (s.chainsInto(s.lfoUuid, cutoff).size()) == (1u); });
    EXPECT_EQ(s.chainsInto(s.lfoUuid, cutoff).size(), 1u);
    EXPECT_EQ(s.chainsInto(s.lfoUuid, resonance).size(), 0u);
    EXPECT_EQ(s.panel().getAutomationLanes().modulatorCount(s.lane), 1);
    EXPECT_TRUE(s.doc().getLane(s.resLane)->modulatorOrder.empty());
    const auto* restored = synth::ui::amountLaneFor(s.doc(), s.attenInto(s.lfoUuid, "cutoff"));
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->points.size(), s.points.size());
    (void)oldAtten;
}

TEST_F(TimelinePanelIntegrationTest, TheMovedRowTakesTheSlotItWasDroppedInAmongTheLanesOwnRows) {
    MoveScene s;
    auto adsr = modulator_test::addNodeWithUuid(s.mc, "ADSR");
    s.mc.getGraphEditor().updateComponents();
    s.mc.getGraphEditor().connectModulationSource(adsr->nodeID, 0, s.byUuid(s.targetUuid)->nodeID,
                                                  s.channelFor("resonance"), 0.4f,
                                                  /*recordUndo=*/false);
    s.panel().refreshModulators();
    s.undo().clearUndoHistory();
    ASSERT_EQ(s.panel().getAutomationLanes().modulatorCount(s.resLane), 1);
    const auto adsrUuid = s.panel().modulatorRowForTest(s.resLane, 0)->getInfo().sourceUuid;

    RowDrag drag(*s.row());
    drag.moveToScreenY(s.rowsTopOnScreen(s.resLane) + 4); // above the middle of Resonance's one row: slot 0
    EXPECT_EQ(s.panel().getAutomationLanes().modulatorDrop().index, 0);
    drag.release();
    synth::test::waitUntil([&] { return (s.panel().getAutomationLanes().modulatorCount(s.resLane)) == (2); });

    ASSERT_EQ(s.panel().getAutomationLanes().modulatorCount(s.resLane), 2);
    EXPECT_EQ(s.panel().modulatorRowForTest(s.resLane, 0)->getInfo().sourceUuid, s.lfoUuid) << "at the drop slot";
    EXPECT_EQ(s.panel().modulatorRowForTest(s.resLane, 1)->getInfo().sourceUuid, adsrUuid);
    ASSERT_TRUE(s.undo().undo());
    synth::test::waitUntil([&] { return (s.panel().getAutomationLanes().modulatorCount(s.resLane)) == (1); });
    EXPECT_EQ(s.panel().getAutomationLanes().modulatorCount(s.resLane), 1);
    EXPECT_EQ(s.panel().modulatorRowForTest(s.resLane, 0)->getInfo().sourceUuid, adsrUuid);
}

TEST_F(TimelinePanelIntegrationTest, ALaneWhoseParameterHasNoCvJackIsNotHereAndTheDropChangesNothing) {
    MoveScene s;
    auto adsr = modulator_test::addNodeWithUuid(s.mc, "ADSR");
    const auto adsrUuid = adsr->properties["uuid"].toString();
    s.mc.getGraphEditor().updateComponents();
    const auto bare = s.doc().addLane(s.track, adsrUuid, "attackCurve", {0.0f, 1.0f, 0.5f});
    ASSERT_LT(s.mc.getGraphEditor().modulationChannelFor(adsr->nodeID, "attackCurve"), 0) << "the premise";
    s.panel().refreshModulators();
    s.undo().clearUndoHistory();
    const auto cutoff = s.channelFor("cutoff");
    const auto revision = s.doc().getRevision();

    RowDrag drag(*s.row());
    drag.moveToScreenY(s.rowsTopOnScreen(bare) - 8);
    EXPECT_EQ(s.panel().getAutomationLanes().modulatorDrop().lane, bare);
    EXPECT_FALSE(s.panel().getAutomationLanes().modulatorDrop().valid);
    EXPECT_FALSE(s.panel().getAutomationLanes().modulatorDropIndicator().isVisible());
    drag.release();
    synth::test::waitUntil([&] { return (s.chainsInto(s.lfoUuid, cutoff).size()) == (1u); });

    EXPECT_EQ(s.chainsInto(s.lfoUuid, cutoff).size(), 1u);
    EXPECT_EQ(s.doc().getRevision(), revision);
    EXPECT_FALSE(s.undo().canUndo());
    EXPECT_EQ(s.panel().getAutomationLanes().modulatorCount(s.lane), 1);
}

TEST_F(TimelinePanelIntegrationTest, ALaneThatAlreadyHasThisSourceOnItsJackIsNotHere) {
    MoveScene s;
    // The same LFO cabled into Resonance too, as a second routing of the same source.
    ASSERT_TRUE(s.mc.getGraphEditor().connectExistingModulator(s.byUuid(s.lfoUuid)->nodeID, 0,
                                                               s.byUuid(s.targetUuid)->nodeID, "resonance"));
    s.panel().refreshModulators();
    s.undo().clearUndoHistory();
    ASSERT_EQ(s.panel().getAutomationLanes().modulatorCount(s.resLane), 1);

    RowDrag drag(*s.row());
    drag.moveToScreenY(s.rowsTopOnScreen(s.resLane) - 8);
    EXPECT_EQ(s.panel().getAutomationLanes().modulatorDrop().lane, s.resLane);
    EXPECT_FALSE(s.panel().getAutomationLanes().modulatorDrop().valid);
    drag.release();
    s.flushUi();
    EXPECT_EQ(s.panel().getAutomationLanes().modulatorCount(s.lane), 1) << "it stays where it was";
    EXPECT_FALSE(s.undo().canUndo());
}
