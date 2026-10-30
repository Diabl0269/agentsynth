// AutomationLanesUndoGlideTests.cpp -- Cmd+Z and Cmd+Shift+Z of a reorder glide like the move did (docs/layout/
// animation.md "Undo and redo glide"): a modulator row moved with Cmd+Alt+Up/Down and an automation lane moved within
// its track both slide from where they stood to their restored slots instead of snapping. The real AppUndoManager
// drives it. Headless: the animated path is forced (FadeAnimateGuard); the row glide is stepped by hand and the lane
// glide runs on a pinned clock that the test advances, never on frames.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesMenuFixture.h"
#include "AutomationLanesModulatorHost.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"

using namespace modulator_rows_test;
using synth::TrackKind;

namespace {

juce::KeyPress commandAlt(int keyCode) {
    return juce::KeyPress(keyCode,
                          juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier), 0);
}

void expectBetween(int value, int a, int b, const char* what) {
    EXPECT_GT(value, std::min(a, b)) << what;
    EXPECT_LT(value, std::max(a, b)) << what;
}

// Track "Bass" with an open cutoff lane routed from LFO 1..3.
struct RowScene {
    HostedLanes h;
    synth::TrackId bass;
    synth::LaneId cutoff;

    RowScene() {
        for (int i = 1; i <= 3; ++i)
            h.host.modulators.push_back(lfoInfo(juce::String(i)));
        bass = h.f.doc.addTrack(TrackKind::Midi, "Bass");
        cutoff = h.f.addLane(bass, "cutoff");
        h.f.panel.setTrackAutomationExpanded(bass, true);
    }
    synth::ui::TimelineAutomationLanes& lanes() { return h.f.panel.getAutomationLanes(); }
    synth::ui::ModulatorRow* row(int i) { return lanes().modulatorRowFor(cutoff, i); }
};

// Track "Bass" with three open lanes (cutoff, res, detune) and a pinned glide clock.
struct LaneScene : automation_lanes_test::LanesPanel {
    lane_menu_test::PickHost host;
    synth::TrackId bass;
    synth::LaneId cutoff, res, detune;
    double nowMs = 1000.0;

    LaneScene() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        cutoff = addLane(bass, "cutoff");
        res = addLane(bass, "res");
        detune = addLane(bass, "detune");
        panel.setTrackHeaderHost(&host);
        panel.setTrackAutomationExpanded(bass, true);
        lanes().setLaneGlideClockForTest([this] { return nowMs; });
    }
    ~LaneScene() {
        lanes().setLaneGlideClockForTest({});
        panel.setTrackHeaderHost(nullptr);
    }
    synth::ui::TimelineAutomationLanes& lanes() { return panel.getAutomationLanes(); }
    int y(synth::LaneId id) { return panel.laneHeaderForTest(id)->getY(); }
    void advance(double ms) {
        nowMs += ms;
        lanes().stepLaneGlideForTest();
    }
};

} // namespace

// Regression test for FRO883: an undo of a modulator row move went through permuteModulators and snapped the rows back,
// since only the keyboard move itself started a glide.
TEST(AutomationLanesUndoGlideTest, UndoAndRedoOfAModulatorRowMoveGlideTheRowsBackAndForth) {
    RowScene s;
    FadeAnimateGuard animate;
    auto* moved = s.row(0);
    auto* neighbour = s.row(1);
    const int top = moved->getY();
    const int second = neighbour->getY();
    ASSERT_TRUE(moved->keyPressed(commandAlt(juce::KeyPress::downKey)));
    s.lanes().stepModulatorGlideForTest(1.0f);
    ASSERT_EQ(s.row(1), moved);
    ASSERT_EQ(moved->getY(), second);

    ASSERT_TRUE(s.h.f.undo.undo());
    EXPECT_EQ(s.row(0), moved) << "the restored order";
    EXPECT_TRUE(s.lanes().isModulatorGlideAnimating());
    EXPECT_EQ(moved->getY(), second) << "starts where it stood";
    EXPECT_EQ(neighbour->getY(), top);
    s.lanes().stepModulatorGlideForTest(0.5f);
    expectBetween(moved->getY(), top, second, "the moved row glides up");
    expectBetween(neighbour->getY(), top, second, "its neighbour glides down");
    s.lanes().stepModulatorGlideForTest(1.0f);
    EXPECT_FALSE(s.lanes().isModulatorGlideAnimating());
    EXPECT_EQ(moved->getY(), top);
    EXPECT_EQ(neighbour->getY(), second);

    ASSERT_TRUE(s.h.f.undo.redo());
    EXPECT_EQ(s.row(1), moved);
    EXPECT_TRUE(s.lanes().isModulatorGlideAnimating()) << "redo glides too";
    s.lanes().stepModulatorGlideForTest(0.5f);
    expectBetween(moved->getY(), top, second, "the moved row glides down again");
    s.lanes().stepModulatorGlideForTest(1.0f);
    EXPECT_EQ(moved->getY(), second);
}

TEST(AutomationLanesUndoGlideTest, UnderReduceMotionAnUndoneModulatorRowMoveLandsAtOnce) {
    RowScene s;
    FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
    auto* moved = s.row(0);
    const int top = moved->getY();
    ASSERT_TRUE(moved->keyPressed(commandAlt(juce::KeyPress::downKey)));
    ASSERT_TRUE(s.h.f.undo.undo());
    EXPECT_FALSE(s.lanes().isModulatorGlideAnimating());
    EXPECT_EQ(moved->getY(), top);
}

// Regression test for FRO883: an undo of a lane move snapped the lanes back.
TEST(AutomationLanesUndoGlideTest, UndoAndRedoOfALaneMoveGlideTheLanesBackAndForth) {
    LaneScene f;
    FadeAnimateGuard animate;
    auto& header = *f.panel.laneHeaderForTest(f.cutoff);
    const int top = f.y(f.cutoff);
    const int second = f.y(f.res);
    ASSERT_TRUE(header.keyPressed(commandAlt(juce::KeyPress::downKey)));
    f.advance(1000.0);
    ASSERT_EQ(f.y(f.cutoff), second);
    ASSERT_EQ(f.y(f.res), top);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getTrack(f.bass)->lanes[0].id, f.cutoff) << "the restored order";
    EXPECT_TRUE(f.lanes().isLaneReorderActive()) << "a glide is running";
    EXPECT_EQ(f.y(f.cutoff), second) << "starts where it stood";
    EXPECT_EQ(f.y(f.res), top);
    f.advance(70.0);
    expectBetween(f.y(f.cutoff), top, second, "the lane glides up");
    expectBetween(f.y(f.res), top, second, "its neighbour glides down");
    f.advance(1000.0);
    EXPECT_FALSE(f.lanes().isLaneReorderActive());
    EXPECT_EQ(f.y(f.cutoff), top);
    EXPECT_EQ(f.y(f.res), second);

    ASSERT_TRUE(f.undo.redo());
    EXPECT_TRUE(f.lanes().isLaneReorderActive()) << "redo glides too";
    EXPECT_EQ(f.y(f.cutoff), top);
    f.advance(70.0);
    expectBetween(f.y(f.cutoff), top, second, "the lane glides down again");
    f.advance(1000.0);
    EXPECT_EQ(f.y(f.cutoff), second);
    EXPECT_EQ(f.y(f.res), top);
}

TEST(AutomationLanesUndoGlideTest, UnderReduceMotionAnUndoneLaneMoveLandsAtOnce) {
    LaneScene f;
    FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
    const int top = f.y(f.cutoff);
    ASSERT_TRUE(f.panel.laneHeaderForTest(f.cutoff)->keyPressed(commandAlt(juce::KeyPress::downKey)));
    ASSERT_TRUE(f.undo.undo());
    EXPECT_FALSE(f.lanes().isLaneReorderActive());
    EXPECT_EQ(f.y(f.cutoff), top);
}

// Regression test for FRO1197: the lane's curve is drawn through the tweened points, so a glide frame that repainted
// only the box around the moving dot left the line segments to its far neighbours (and the old dot) stale.
TEST(AutomationLanesUndoGlideTest, UndoOfAMovedPointRepaintsTheSegmentsToItsFarNeighbours) {
    automation_lanes_test::LanesPanel p;
    FadeAnimateGuard animate;
    const auto bass = p.doc.addTrack(TrackKind::Midi, "Bass");
    const auto lane = p.addLane(bass, "cutoff");
    p.panel.setTrackAutomationExpanded(bass, true);
    auto* editor = p.panel.laneEditorForTest(lane);
    editor->setTool(synth::ui::AutomationLaneEditor::Tool::Pointer);
    for (const auto& [beat, value] : {std::pair{0.5, 20.0}, {2.0, 60.0}, {3.5, 40.0}})
        p.doc.addBreakpoint(lane, beat, value);
    editor->getPointTweenForTest().forceAnimateForTest(true);

    const auto& view = p.panel.getViewState();
    const juce::Point<float> from{(float)view.beatToX(2.0), (float)editor->valueToY(60.0)};
    automation_lanes_test::dragAcross(*editor, from, {from.x, (float)editor->valueToY(10.0)}, 6);
    editor->getPointTweenForTest().stepForTest(1000.0);

    juce::RectangleList<int> dirty;
    synth::ui::setKeyedTweenDirtyProbeForTest([&](juce::Component& owner, juce::Rectangle<int> area) {
        if (&owner == editor)
            dirty.add(area);
    });
    ASSERT_TRUE(p.undo.undo());
    editor->getPointTweenForTest().stepForTest(32.0);
    synth::ui::setKeyedTweenDirtyProbeForTest({});

    for (const double beat : {0.5, 3.5}) {
        const auto neighbour =
            juce::Point<int>((int)view.beatToX(beat), (int)editor->valueToY(beat < 1.0 ? 20.0 : 40.0));
        EXPECT_TRUE(dirty.containsPoint(neighbour)) << "the far neighbour at beat " << beat << " is repainted";
    }
}
