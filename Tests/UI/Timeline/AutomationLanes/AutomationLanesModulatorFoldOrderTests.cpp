// AutomationLanesModulatorFoldOrderTests.cpp -- the fold arrow on a modulator row (a real click, Return/Space,
// Left/Right: one line tall, the band following, the controls hidden, the names following) and the order of a lane's
// modulator rows (rows sorted by the order stored with the lane; Cmd+Alt+Up/Down through the row's, a control's and the
// band's key handlers moves a row one slot as one undo step and keeps the same row object). Against a stub host,
// headless.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesModulatorHost.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

using namespace modulator_rows_test;
using synth::TrackKind;

namespace {

// Track "Bass" with two lanes (cutoff, resonance), each shown with the host's routings (LFO 1..3), lanes open.
struct ModScene {
    HostedLanes h;
    synth::TrackId bass;
    synth::LaneId cutoff, res;

    explicit ModScene(int routings = 3) {
        for (int i = 1; i <= routings; ++i)
            h.host.modulators.push_back(lfoInfo(juce::String(i)));
        bass = h.f.doc.addTrack(TrackKind::Midi, "Bass");
        cutoff = h.f.addLane(bass, "cutoff");
        res = h.f.addLane(bass, "resonance");
        h.f.panel.setTrackAutomationExpanded(bass, true);
    }

    LanesPanel& f() { return h.f; }
    synth::ui::ModulatorRow* row(int i, synth::LaneId lane = {}) {
        return f().panel.modulatorRowForTest(lane.isValid() ? lane : cutoff, i);
    }
    synth::ui::ModulatorBand* band(int i, synth::LaneId lane = {}) {
        return f().panel.modulatorBandForTest(lane.isValid() ? lane : cutoff, i);
    }
    juce::Rectangle<int> rowBounds(int i, synth::LaneId lane = {}) {
        return f().panel.modulatorRowBoundsForTest(lane.isValid() ? lane : cutoff, i);
    }
    // The lane's rows by source, top to bottom.
    juce::StringArray sources(synth::LaneId lane = {}) {
        juce::StringArray names;
        for (int i = 0; i < f().panel.getAutomationLanes().modulatorCount(lane.isValid() ? lane : cutoff); ++i)
            names.add(row(i, lane)->getInfo().sourceUuid);
        return names;
    }
    const std::vector<juce::String>& stored(synth::LaneId lane) { return f().doc.getLane(lane)->modulatorOrder; }
};

juce::KeyPress commandAlt(int keyCode) {
    return juce::KeyPress(keyCode,
                          juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier), 0);
}
const juce::StringArray kOneTwoThree{"lfo-1", "lfo-2", "lfo-3"};
} // namespace

//==============================================================================
// Fold

TEST(AutomationLanesModulatorFoldTest, ClickingTheArrowFoldsTheRowAndItsBandToTheTitleLineAndAgainOpensThem) {
    ModScene s;
    auto* row = s.row(0);
    ASSERT_NE(row, nullptr);
    auto& arrow = row->getFoldArrow();
    const int full = synth::ui::ModulatorRow::kBaseHeight;
    const int line = full / 3;
    const int resTop = s.f().panel.laneRowBoundsForTest(s.res).getY();

    EXPECT_TRUE(arrow.getWantsKeyboardFocus()) << "a Tab stop";
    EXPECT_TRUE(arrow.isExpanded());
    EXPECT_EQ(arrow.getTitle(), "Collapse LFO 1 on Cutoff");
    EXPECT_TRUE(arrow.getTooltip().startsWith("Collapse LFO 1 on Cutoff"));
    EXPECT_TRUE(arrow.getTooltip().contains("Left arrow")) << "the tooltip names the key";
    EXPECT_FALSE(row->isCollapsed());
    EXPECT_EQ(s.rowBounds(0).getHeight(), full);

    clickButton(arrow); // a real press and release on the arrow
    EXPECT_TRUE(row->isCollapsed());
    EXPECT_TRUE(s.f().panel.getAutomationLanes().isModulatorCollapsed(s.cutoff, 0));
    EXPECT_FALSE(s.f().panel.getAutomationLanes().isModulatorCollapsed(s.cutoff, 1)) << "only that routing";
    EXPECT_EQ(s.rowBounds(0).getHeight(), line) << "one title line";
    EXPECT_EQ(row->getHeight(), line);
    EXPECT_EQ(s.f().panel.getLocalArea(s.band(0), s.band(0)->getLocalBounds()), s.rowBounds(0)) << "the band follows";
    EXPECT_EQ(s.rowBounds(1).getY(), s.rowBounds(0).getBottom()) << "the next row closes up";
    EXPECT_EQ(s.f().panel.laneRowBoundsForTest(s.res).getY(), resTop - (full - line)) << "and so does the next lane";
    EXPECT_FALSE(row->getShapeCombo().isVisible());
    EXPECT_FALSE(row->getSyncToggle().isVisible());
    EXPECT_FALSE(row->getSyncRateCombo().isVisible());
    EXPECT_FALSE(row->getRateSlider().isVisible());
    EXPECT_FALSE(row->getShapeIcon().isVisible());
    EXPECT_EQ(row->getAmountText(), "0%") << "the amount readout stays, on the title line";
    EXPECT_TRUE(arrow.isVisible());
    EXPECT_FALSE(arrow.isExpanded());
    EXPECT_EQ(arrow.getTitle(), "Expand LFO 1 on Cutoff");
    EXPECT_TRUE(arrow.getTooltip().startsWith("Expand LFO 1 on Cutoff"));
    EXPECT_TRUE(arrow.getTooltip().contains("Right arrow"));
    EXPECT_TRUE(s.f().panel.getAutomationLanes().isModulatorCollapsed(s.res, 0))
        << "the stub offers both lanes one routing";
    EXPECT_EQ(s.f().panel.getClipLaneArea().getRowLayout().trackExtraHeight(0), 2 * (40 + 3 * full - (full - line)))
        << "the track's extra height counts the folded row at its folded height";

    clickButton(arrow);
    EXPECT_FALSE(row->isCollapsed());
    EXPECT_EQ(s.rowBounds(0).getHeight(), full);
    EXPECT_EQ(s.f().panel.laneRowBoundsForTest(s.res).getY(), resTop);
    EXPECT_TRUE(row->getShapeCombo().isVisible());
    EXPECT_TRUE(row->getSyncToggle().isVisible());
    EXPECT_TRUE(row->getShapeIcon().isVisible());
    EXPECT_EQ(arrow.getTitle(), "Collapse LFO 1 on Cutoff");
}

TEST(AutomationLanesModulatorFoldTest, ReturnAndSpaceOnTheArrowAndLeftAndRightOnTheRowFoldAndOpen) {
    ModScene s;
    auto* row = s.row(1);
    auto& arrow = row->getFoldArrow();

    EXPECT_TRUE(arrow.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(row->isCollapsed());
    EXPECT_TRUE(arrow.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_FALSE(row->isCollapsed());

    EXPECT_FALSE(row->keyPressed(juce::KeyPress(juce::KeyPress::rightKey))) << "Right on an open row does nothing";
    EXPECT_TRUE(row->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_TRUE(row->isCollapsed());
    EXPECT_FALSE(row->keyPressed(juce::KeyPress(juce::KeyPress::leftKey))) << "nor Left on a folded one";
    EXPECT_TRUE(row->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_FALSE(row->isCollapsed());
    EXPECT_FALSE(s.f().panel.getAutomationLanes().isModulatorCollapsed(s.cutoff, 0));
}

TEST(AutomationLanesModulatorFoldTest,
     ARoutingWhoseSourceChangedStaysFoldedARemovedOneComesBackOpenAndAFoldIsNeverSaved) {
    ModScene s(1);
    s.row(0)->getFoldArrow().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    ASSERT_TRUE(s.row(0)->isCollapsed());
    const int folded = s.rowBounds(0).getHeight();

    s.h.host.modulators[0].sourceUuid = "lfo-9"; // Change source: a new key, in the same slot
    s.f().panel.refreshModulators();
    ASSERT_NE(s.row(0), nullptr);
    EXPECT_TRUE(s.row(0)->isCollapsed()) << "the fold goes with the slot";
    EXPECT_EQ(s.rowBounds(0).getHeight(), folded);

    // Removed, then added back in a later change: a new routing, which starts open.
    const auto routing = s.h.host.modulators[0];
    s.h.host.modulators.clear();
    s.f().panel.refreshModulators();
    s.h.host.modulators.push_back(routing);
    s.f().panel.refreshModulators();
    ASSERT_NE(s.row(0), nullptr);
    EXPECT_FALSE(s.row(0)->isCollapsed());
    EXPECT_EQ(s.rowBounds(0).getHeight(), synth::ui::ModulatorRow::kBaseHeight);

    s.row(0)->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    ASSERT_TRUE(s.row(0)->isCollapsed());
    const auto saved = juce::JSON::toString(s.f().doc.toVar()); // no fold flag anywhere in the file
    EXPECT_FALSE(saved.containsIgnoreCase("collapsed"));
    EXPECT_FALSE(saved.containsIgnoreCase("fold"));
}

TEST(AutomationLanesModulatorFoldTest, ANonLfoRowFoldsToo) {
    ModScene s(0);
    synth::ui::ModulatorInfo env;
    env.sourceUuid = "env";
    env.sourceTitle = "Filter Env";
    env.attenuverterUuid = "atten-env";
    s.h.host.modulators = {env};
    s.f().panel.refreshModulators();
    auto* row = s.row(0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->getFoldArrow().getTitle(), "Collapse Filter Env on Cutoff");
    row->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    EXPECT_EQ(s.rowBounds(0).getHeight(), synth::ui::ModulatorRow::kBaseHeight / 3);
}

TEST(AutomationLanesModulatorFoldTest, TheHeightTweensWhenTheTimelineIsShowingAndLandsAtOnceUnderReduceMotion) {
    {
        FadeAnimateGuard animate;
        ModScene s;
        const int full = synth::ui::ModulatorRow::kBaseHeight;
        const int line = full / 3;
        s.row(0)->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
        auto& lanes = s.f().panel.getAutomationLanes();
        EXPECT_TRUE(lanes.isModulatorFoldAnimating());
        EXPECT_TRUE(s.row(0)->isCollapsed());
        EXPECT_EQ(s.rowBounds(0).getHeight(), full) << "starts from the full height";
        EXPECT_TRUE(s.row(0)->getShapeCombo().isVisible()) << "the controls stay until it lands";

        lanes.stepModulatorFoldForTest(0.5f);
        EXPECT_GT(s.rowBounds(0).getHeight(), line);
        EXPECT_LT(s.rowBounds(0).getHeight(), full);
        EXPECT_EQ(s.f().panel.getLocalArea(s.band(0), s.band(0)->getLocalBounds()), s.rowBounds(0));

        lanes.stepModulatorFoldForTest(1.0f);
        EXPECT_FALSE(lanes.isModulatorFoldAnimating());
        EXPECT_EQ(s.rowBounds(0).getHeight(), line);
        EXPECT_FALSE(s.row(0)->getShapeCombo().isVisible());
    }
    {
        FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
        ModScene s;
        s.row(0)->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
        EXPECT_FALSE(s.f().panel.getAutomationLanes().isModulatorFoldAnimating());
        EXPECT_EQ(s.rowBounds(0).getHeight(), synth::ui::ModulatorRow::kBaseHeight / 3);
    }
}

//==============================================================================
// Order

TEST(AutomationLanesModulatorOrderTest, RowsFollowTheOrderStoredWithTheLaneListedKeysFirstAndStaleKeysIgnored) {
    ModScene s;
    EXPECT_EQ(s.sources(), kOneTwoThree) << "no stored order: graph order";

    ASSERT_TRUE(s.f().doc.setModulatorOrder(s.cutoff, {"atten-3", "atten-gone", "atten-1"}));
    EXPECT_EQ(s.sources(), (juce::StringArray{"lfo-3", "lfo-1", "lfo-2"})) << "listed first, the rest in graph order";
    EXPECT_EQ(s.band(0)->getInfo().sourceUuid, "lfo-3") << "the band follows its row";
    EXPECT_EQ(s.rowBounds(0).getY(), s.f().panel.laneRowBoundsForTest(s.cutoff).getBottom());
    EXPECT_EQ(s.sources(s.res), kOneTwoThree) << "another lane keeps its own order";
    EXPECT_EQ(s.stored(s.cutoff).size(), 3u) << "a stale key is ignored, not pruned on read";
}

TEST(AutomationLanesModulatorOrderTest, CmdAltDownOnTheRowMovesItOneSlotKeepsTheSameRowAndIsOneUndoStep) {
    ModScene s;
    auto* moved = s.row(0);
    auto* movedBand = s.band(0);
    auto* other = s.row(1);
    const auto otherLane = s.sources(s.res);

    EXPECT_TRUE(moved->keyPressed(commandAlt(juce::KeyPress::downKey)));
    EXPECT_EQ(s.sources(), (juce::StringArray{"lfo-2", "lfo-1", "lfo-3"}));
    EXPECT_EQ(s.row(1), moved) << "the same row object (and so its keyboard focus) took the new slot";
    EXPECT_EQ(s.band(1), movedBand);
    EXPECT_EQ(s.row(0), other);
    EXPECT_EQ(s.rowBounds(1).getY(), s.rowBounds(0).getBottom());
    EXPECT_EQ(moved->getY(), s.rowBounds(1).getY() - s.f().panel.getClipLaneArea().getY());
    EXPECT_EQ(s.stored(s.cutoff), (std::vector<juce::String>{"atten-2", "atten-1", "atten-3"}));
    EXPECT_EQ(s.sources(s.res), otherLane) << "it never leaves its lane";
    EXPECT_TRUE(s.stored(s.res).empty());

    ASSERT_TRUE(s.f().undo.undo());
    EXPECT_EQ(s.sources(), kOneTwoThree) << "one undo step";
    EXPECT_TRUE(s.stored(s.cutoff).empty());
    EXPECT_FALSE(s.f().undo.canUndo());
    ASSERT_TRUE(s.f().undo.redo());
    EXPECT_EQ(s.sources(), (juce::StringArray{"lfo-2", "lfo-1", "lfo-3"})) << "and redo";
}

TEST(AutomationLanesModulatorOrderTest, TheFirstRowUpAndTheLastRowDownDoNothingButStillOwnTheKey) {
    ModScene s;
    EXPECT_TRUE(s.row(0)->keyPressed(commandAlt(juce::KeyPress::upKey)));
    EXPECT_TRUE(s.row(2)->keyPressed(commandAlt(juce::KeyPress::downKey)));
    EXPECT_EQ(s.sources(), kOneTwoThree);
    EXPECT_FALSE(s.f().undo.canUndo()) << "nothing moved, nothing recorded";
    EXPECT_TRUE(s.stored(s.cutoff).empty());
}

TEST(AutomationLanesModulatorOrderTest, TheKeyWorksFromARowsControlsAndFromItsBandAndLeavesOtherArrowKeysAlone) {
    ModScene s;
    auto* row = s.row(1);
    // A combo would take Up/Down itself: the row's key listener is offered the key first.
    EXPECT_TRUE(row->keyPressed(commandAlt(juce::KeyPress::upKey), &row->getShapeCombo()));
    EXPECT_EQ(s.sources(), (juce::StringArray{"lfo-2", "lfo-1", "lfo-3"}));
    EXPECT_EQ(s.row(0), row);

    EXPECT_TRUE(s.band(0)->keyPressed(commandAlt(juce::KeyPress::downKey)))
        << "from the band, where a bare Up/Down nudges the amount";
    EXPECT_EQ(s.sources(), kOneTwoThree);

    EXPECT_FALSE(row->keyPressed(juce::KeyPress(juce::KeyPress::upKey), &row->getShapeCombo()))
        << "a bare arrow is the control's own";
    EXPECT_EQ(s.sources(), kOneTwoThree);
}

TEST(AutomationLanesModulatorOrderTest, TheMoveKeysAreTheRebindableLaneMoveActions) {
    ModScene s;
    ShortcutManager shortcuts;
    s.f().panel.setShortcutManager(&shortcuts);
    shortcuts.setBinding("timelineMoveLaneDown", juce::KeyPress('j', juce::ModifierKeys::altModifier, 0));
    EXPECT_FALSE(s.row(0)->keyPressed(commandAlt(juce::KeyPress::downKey))) << "the default no longer moves";
    EXPECT_TRUE(s.row(0)->keyPressed(juce::KeyPress('j', juce::ModifierKeys::altModifier, 0)));
    EXPECT_EQ(s.sources(), (juce::StringArray{"lfo-2", "lfo-1", "lfo-3"}));
    s.f().panel.setShortcutManager(nullptr);
}

TEST(AutomationLanesModulatorOrderTest, TheFoldStateTravelsWithTheRowWhenItMoves) {
    ModScene s;
    s.row(0)->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    auto* folded = s.row(0);
    folded->keyPressed(commandAlt(juce::KeyPress::downKey));
    EXPECT_EQ(s.row(1), folded);
    EXPECT_TRUE(s.f().panel.getAutomationLanes().isModulatorCollapsed(s.cutoff, 1));
    EXPECT_FALSE(s.f().panel.getAutomationLanes().isModulatorCollapsed(s.cutoff, 0));
    EXPECT_EQ(s.rowBounds(1).getHeight(), synth::ui::ModulatorRow::kBaseHeight / 3);
    EXPECT_EQ(s.rowBounds(0).getHeight(), synth::ui::ModulatorRow::kBaseHeight);
}

TEST(AutomationLanesModulatorOrderTest, TheOrderIsOneReusableWriteAndSurvivesAReload) {
    ModScene s;
    auto& lanes = s.f().panel.getAutomationLanes();
    EXPECT_TRUE(lanes.moveModulatorInOrder(s.cutoff, "atten-3", 0));
    EXPECT_EQ(s.sources(), (juce::StringArray{"lfo-3", "lfo-1", "lfo-2"}));
    EXPECT_FALSE(lanes.moveModulatorInOrder(s.cutoff, "atten-3", 0)) << "already there";
    EXPECT_FALSE(lanes.moveModulatorInOrder(s.cutoff, "atten-missing", 1));

    synth::TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(s.f().doc.toVar()));
    EXPECT_EQ(loaded.getLane(s.cutoff)->modulatorOrder, (std::vector<juce::String>{"atten-3", "atten-1", "atten-2"}));
}

TEST(AutomationLanesModulatorFoldTest, ALeftOrRightKeyFromAControlOfTheRowNeverFoldsIt) {
    ModScene s;
    auto* row = s.row(0);
    // The key listener path a control's keys take: it is only offered the move keys.
    EXPECT_FALSE(row->keyPressed(juce::KeyPress(juce::KeyPress::leftKey), &row->getSyncToggle()));
    EXPECT_FALSE(row->keyPressed(juce::KeyPress(juce::KeyPress::leftKey), &row->getShapeCombo()));
    EXPECT_FALSE(row->isCollapsed());
    // Headless, no component can hold focus; a focused control would be refused by the row's own handler (it folds only
    // when nothing else, or the row, is the focused component).
    EXPECT_EQ(juce::Component::getCurrentlyFocusedComponent(), nullptr);
    EXPECT_TRUE(row->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_TRUE(row->isCollapsed());
}

TEST(AutomationLanesModulatorOrderTest,
     TheMovedRowAndItsNeighbourGlideFromTheirOldSlotsAndLandAtOnceUnderReduceMotion) {
    {
        FadeAnimateGuard animate;
        ModScene s;
        auto& lanes = s.f().panel.getAutomationLanes();
        auto* moved = s.row(0);
        auto* neighbour = s.row(1);
        auto* movedBand = s.band(0);
        const int oldMoved = moved->getY();
        const int oldNeighbour = neighbour->getY();
        const int newMoved = oldNeighbour;

        EXPECT_TRUE(moved->keyPressed(commandAlt(juce::KeyPress::downKey)));
        EXPECT_TRUE(lanes.isModulatorGlideAnimating());
        EXPECT_EQ(s.row(1), moved);
        EXPECT_EQ(moved->getY(), oldMoved) << "starts where it stood";
        EXPECT_EQ(neighbour->getY(), oldNeighbour);
        const int bandStart = movedBand->getY();

        lanes.stepModulatorGlideForTest(0.5f);
        EXPECT_GT(moved->getY(), oldMoved);
        EXPECT_LT(moved->getY(), newMoved);
        EXPECT_LT(neighbour->getY(), oldNeighbour + 1);
        EXPECT_GT(movedBand->getY(), bandStart);

        lanes.stepModulatorGlideForTest(1.0f);
        EXPECT_FALSE(lanes.isModulatorGlideAnimating());
        EXPECT_EQ(moved->getY(), newMoved);
        EXPECT_EQ(neighbour->getY(), oldMoved);
        EXPECT_EQ(s.rowBounds(1).getY() - s.f().panel.getClipLaneArea().getY(), moved->getY());
    }
    {
        FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
        ModScene s;
        auto* moved = s.row(0);
        const int newY = s.row(1)->getY();
        EXPECT_TRUE(moved->keyPressed(commandAlt(juce::KeyPress::downKey)));
        EXPECT_FALSE(s.f().panel.getAutomationLanes().isModulatorGlideAnimating());
        EXPECT_EQ(moved->getY(), newY);
    }
}
