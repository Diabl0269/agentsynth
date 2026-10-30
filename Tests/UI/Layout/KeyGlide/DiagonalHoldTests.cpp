// DiagonalHoldTests.cpp -- two arrows held at once move a point diagonally in every point editor (automation lane,
// curve editor, morph map): time and value together while both are down, only the other axis once one is let go, and
// the whole hold is still one undo step. The arrows are read through each editor's injected key host: synthetic key
// events cannot set the OS key state, so these are the only proof of the diagonal hold.
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "../../Timeline/AutomationLanes/AutomationLanesTestFixture.h"
#include "KeyGlideTestHost.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/MorphMap/MorphMapGlide.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"

namespace {

using synth::ui::MorphMap;
constexpr int kAlt = juce::ModifierKeys::altModifier;
const int kRight = juce::KeyPress::rightKey;
const int kUp = juce::KeyPress::upKey;
const int kDown = juce::KeyPress::downKey;

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

struct LaneHold : automation_lanes_test::LanesPanel {
    synth::TrackId track;
    synth::LaneId lane;
    synth::ui::AutomationLaneEditor* editor = nullptr;
    key_glide_test::FakeKeys keys;

    LaneHold() {
        track = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = addLane(track, "cutoff");
        panel.setTrackAutomationExpanded(track, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(synth::ui::AutomationLaneEditor::Tool::Pointer);
        doc.addBreakpoint(lane, 2.0, 40.0);
        editor->getKeyHoldForTest().host = keys.host();
        editor->getPointSelection().setSelection({2.0});
        editor->getPointSelection().setCursor(2.0);
    }
    void frame(double glideMs) {
        keys.now = 1000.0 + synth::ui::lane_key::kTapMs + glideMs;
        editor->getKeyHoldForTest().tick();
    }
    double beat() const { return editor->getPreviewBeatForTest(); }
    double value() const { return editor->getPreviewValueForTest(); }
};

void expectLaneDiagonal(bool snapped) {
    SCOPED_TRACE(snapped ? "snapped" : "free");
    LaneHold f;
    f.panel.getViewState().snapEnabled = snapped;
    const auto rev = f.doc.getRevision();

    f.keys.alt = true;
    f.keys.down = {kRight};
    ASSERT_TRUE(f.editor->keyPressed(key(kRight, kAlt)));
    f.frame(400.0);
    const double beatAlone = f.beat();
    EXPECT_GT(beatAlone, 2.0) << "Alt+Right moves the point in time";
    EXPECT_DOUBLE_EQ(f.value(), 40.0) << "and only in time";

    f.keys.down = {kRight, kUp}; // Up joins the hold (the OS sends Alt+Up while Alt is down)
    EXPECT_TRUE(f.editor->keyPressed(key(kUp, kAlt)));
    f.frame(1400.0);
    f.frame(2400.0);
    const double beatBoth = f.beat();
    const double valueBoth = f.value();
    EXPECT_GT(beatBoth, beatAlone) << "time keeps moving while Right is down";
    EXPECT_GT(valueBoth, 40.0) << "and the value rises at the same time";
    EXPECT_EQ(f.doc.getRevision(), rev) << "nothing is committed while a key is down";

    f.keys.down = {kUp}; // Right let go: the hold carries on, on the value axis only
    f.editor->keyStateChanged(false);
    f.frame(3400.0);
    EXPECT_DOUBLE_EQ(f.beat(), beatBoth) << "time stops with its key";
    EXPECT_GT(f.value(), valueBoth) << "the value keeps rising";

    f.keys.down.clear();
    f.editor->keyStateChanged(false);
    EXPECT_FALSE(f.editor->getKeyHoldForTest().active());
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "the whole diagonal hold is one edit";
    f.undo.undo();
    const auto& points = f.doc.getLane(f.lane)->points;
    ASSERT_EQ(points.size(), 1u);
    EXPECT_DOUBLE_EQ(points.front().beat, 2.0) << "one undo takes it all back";
    EXPECT_DOUBLE_EQ(points.front().value, 40.0);
}

synth::ui::CurveModel freeModel() {
    synth::ui::CurveModel model(synth::ui::CurveMode::Free);
    model.setNodes({synth::ui::CurveNode{0.0, 0.5f, false, true}, synth::ui::CurveNode{0.25, 0.5f, true, true},
                    synth::ui::CurveNode{0.5, 0.3f, true, true}, synth::ui::CurveNode{0.75, 0.5f, true, true},
                    synth::ui::CurveNode{1.0, 0.5f, false, true}});
    return model;
}

} // namespace

TEST(DiagonalHold, LaneAltRightThenUpMovesTimeAndValueAndReleasingRightKeepsTheValueGoing) {
    expectLaneDiagonal(false);
    expectLaneDiagonal(true);
}

TEST(DiagonalHold, CurveEditorAltRightThenUpMovesTimeAndLevelAndReleasingRightKeepsTheLevelGoing) {
    synth::ui::CurveEditorComponent editor;
    editor.setBounds(0, 0, 400, 160);
    editor.setModel(freeModel());
    int starts = 0, ends = 0;
    editor.onGestureStart = [&] { ++starts; };
    editor.onGestureEnd = [&] { ++ends; };
    key_glide_test::FakeKeys keys;
    editor.getKeyHoldForTest().host = keys.host();
    const auto node = [&] { return editor.getModel().getNode(2); };
    const auto at =
        synth::ui::CurveEditorGeometry(editor.getModel(), editor.getLocalBounds().toFloat()).nodePosition(2);
    editor.mouseDown(synth::ui::test::curveLeftClick(editor, at));
    editor.mouseUp(synth::ui::test::curveLeftClick(editor, at));
    ASSERT_EQ(editor.getSelectedIndex(), 2);
    const auto frame = [&](double ms) {
        keys.now = 1000.0 + synth::ui::lane_key::kTapMs + ms;
        editor.getKeyHoldForTest().tick();
    };

    keys.alt = true;
    keys.down = {kRight};
    ASSERT_TRUE(editor.keyPressed(key(kRight, kAlt)));
    frame(300.0);
    const double xAlone = node().x;
    EXPECT_GT(xAlone, 0.5);
    EXPECT_FLOAT_EQ(node().y, 0.3f) << "Alt+Right moves only in time";

    keys.down = {kRight, kUp};
    EXPECT_TRUE(editor.keyPressed(key(kUp, kAlt)));
    frame(600.0);
    frame(900.0);
    const double xBoth = node().x;
    const float yBoth = node().y;
    EXPECT_GT(xBoth, xAlone) << "time keeps moving while Right is down";
    EXPECT_GT(yBoth, 0.3f) << "and the level rises at the same time";

    keys.down = {kUp};
    editor.keyStateChanged(false);
    frame(1200.0);
    EXPECT_DOUBLE_EQ(node().x, xBoth) << "time stops with its key";
    EXPECT_GT(node().y, yBoth) << "the level keeps rising";

    keys.down.clear();
    editor.keyStateChanged(false);
    EXPECT_FALSE(editor.getKeyHoldForTest().active());
    EXPECT_EQ(starts, 1) << "the whole diagonal hold is one gesture";
    EXPECT_EQ(ends, 1);
}

// The morph map already read every arrow each frame; this keeps it that way beside the other two editors.
TEST(DiagonalHold, MorphMapDownThenRightMovesDiagonallyAndReleasingDownKeepsGoingRight) {
    synth::ui::test::ShapeRig rig;
    synth::ui::ShapeListEditor editor{rig.adapter, 0};
    auto& map = editor.getMap();
    key_glide_test::FakeKeys keys;
    map.setGlideHostForTest(keys.host());
    const auto shape = [&] { return rig.filter->getCustomShapes().shapes[1].position; };
    const int serial = rig.undo.getEditSerial();
    const float x0 = shape()[0], y0 = shape()[1];
    const auto frame = [&](double ms) {
        keys.now = 1000.0 + synth::ui::morph_glide::kTapMs + ms;
        map.glideTickForTest();
    };

    keys.down = {kDown};
    map.getDotStop(1)->keyPressed(key(kDown));
    frame(300.0);
    EXPECT_GT(shape()[1], y0);
    EXPECT_FLOAT_EQ(shape()[0], x0) << "Down moves only down";

    keys.down = {kDown, kRight};
    map.getDotStop(1)->keyPressed(key(kRight));
    frame(600.0);
    const float xBoth = shape()[0], yBoth = shape()[1];
    EXPECT_GT(xBoth, x0) << "both arrows: diagonal";

    keys.down = {kRight};
    map.keyStateChanged(false);
    frame(900.0);
    EXPECT_FLOAT_EQ(shape()[1], yBoth) << "down stops with its key";
    EXPECT_GT(shape()[0], xBoth) << "right carries on";

    keys.down.clear();
    map.keyStateChanged(false);
    EXPECT_FALSE(map.isGliding());
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "one undo step";
}
