// The small curve box, the line-click editing it opts into, and the enlarged window: its presets, bend handle, range
// handles and saved curves edit the attenuverter's curve, each gesture is one undo step, and an open box follows undo
// and a deleted attenuverter. docs/modules/modulation.md.

#include "ModCurveTestRig.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include "UI/ModuleViews/ModCurve/ModCurveWindow.h"

namespace {
using synth::ModCurve;
using Preset = synth::ModCurve::Preset;
} // namespace

TEST_F(ModuleComponentTest, CurveBoxPresetChipsSetTheCurve) {
    BoxRig r;
    const std::pair<int, Preset> chips[] = {
        {1, Preset::Exp}, {2, Preset::Log}, {3, Preset::SCurve}, {0, Preset::Linear}};
    for (const auto& [index, preset] : chips) {
        clickNow(r.box->presetChip(index));
        EXPECT_EQ(r.curve(), ModCurve::preset(preset)) << index;
    }
}

TEST_F(ModuleComponentTest, CurveBoxChipThatChangesNothingAddsNoUndoStep) {
    BoxRig r;
    ASSERT_FALSE(r.f.undo.canUndo());
    clickNow(r.box->presetChip(0)); // already linear
    EXPECT_FALSE(r.f.undo.canUndo());
    clickNow(r.box->presetChip(1));
    EXPECT_TRUE(r.f.undo.canUndo());
    r.f.undo.undo();
    EXPECT_TRUE(r.curve().isLinear());
    EXPECT_FALSE(r.f.undo.canUndo()) << "the preset click was exactly one step";
}

TEST_F(ModuleComponentTest, CurveBoxBendHandleDragIsOneUndoStep) {
    BoxRig r;
    const auto handle = r.geometry().bendHandlePosition(0);
    ASSERT_TRUE(handle.has_value());
    r.pressDragRelease(*handle, handle->translated(0.0f, -30.0f));
    EXPECT_FALSE(r.curve().isLinear());
    ASSERT_TRUE(r.f.undo.canUndo());
    r.f.undo.undo();
    EXPECT_TRUE(r.curve().isLinear());
    EXPECT_FALSE(r.f.undo.canUndo()) << "many drag moves, one step";
}

TEST_F(ModuleComponentTest, CurveBoxRangeHandlesEditTheRangeOneStepPerGesture) {
    BoxRig r;
    auto& max = r.box->maxHandle();
    EXPECT_TRUE(max.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_NEAR(r.curve().rangeMax, 0.99f, 1e-4f);
    EXPECT_TRUE(
        r.box->minHandle().keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(r.curve().rangeMin, 0.1f, 1e-4f);

    // A mouse drag: down at the top of the track, up near the bottom.
    auto& min = r.box->minHandle();
    const auto make = [&](juce::Point<float> p, bool dragged) {
        return synth::ui::test::makeCurveMouseEvent(min, p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                                                    dragged, {5.0f, 10.0f});
    };
    min.mouseDown(make({5.0f, 10.0f}, false));
    min.mouseDrag(make({5.0f, 100.0f}, true));
    min.mouseDrag(make({5.0f, 150.0f}, true));
    min.mouseUp(make({5.0f, 150.0f}, true));
    EXPECT_LT(r.curve().rangeMin, 0.4f);

    r.f.undo.undo(); // the drag
    EXPECT_NEAR(r.curve().rangeMin, 0.1f, 1e-4f);
    r.f.undo.undo(); // the Shift+Up
    EXPECT_NEAR(r.curve().rangeMin, 0.0f, 1e-4f);
    EXPECT_NEAR(r.curve().rangeMax, 0.99f, 1e-4f);
}

TEST_F(ModuleComponentTest, CurveBoxFollowsUndoWhileOpen) {
    BoxRig r;
    clickNow(r.box->presetChip(3));
    EXPECT_GT(r.editor().getModel().getNumNodes(), 2);
    r.f.undo.undo();
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 2);
    EXPECT_FLOAT_EQ(r.editor().getModel().getBend(0), 0.0f);
    EXPECT_TRUE(r.box->presetChip(0).getToggleState()) << "the Linear chip lights again";
    r.f.undo.redo();
    EXPECT_TRUE(r.box->presetChip(3).getToggleState());
}

TEST_F(ModuleComponentTest, CurveBoxClosesSafelyWhenTheAttenuverterIsDeleted) {
    BoxRig r;
    int closed = 0;
    r.box->onClose = [&] { ++closed; };
    r.f.engine.removeModRouting(r.f.attenId);
    r.box->tick();
    EXPECT_EQ(closed, 1);
    EXPECT_FALSE(r.target->exists());
    clickNow(r.box->presetChip(1)); // a late click on a vanished node is a no-op
    clickNow(r.box->enlargeButton());
    EXPECT_TRUE(r.box->maxHandle().keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    r.box->tick();
    EXPECT_EQ(closed, 1) << "reported once";
}

TEST_F(ModuleComponentTest, CurveWindowChipsFollowUndo) {
    Fixture f;
    auto target = std::make_shared<synth::ui::ModCurveEditTarget>(*f.editor, f.attenId);
    synth::ui::ModCurveWindowContent content(target);
    clickNow(content.presetChip(1));
    EXPECT_TRUE(content.presetChip(1).getToggleState());
    f.undo.undo();
    EXPECT_TRUE(content.presetChip(0).getToggleState()) << "the Linear chip lights again";
    EXPECT_FALSE(content.presetChip(1).getToggleState());
}

TEST_F(ModuleComponentTest, CurveBoxRangeHandlesNeverCross) {
    BoxRig r;
    auto c = r.curve();
    c.rangeMin = 0.3f;
    c.rangeMax = 0.6f;
    r.target->setOnce(c);

    EXPECT_TRUE(r.box->minHandle().keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    EXPECT_NEAR(r.curve().rangeMin, 0.6f, 1e-4f);
    EXPECT_NEAR(r.box->minHandle().value(), 0.6f, 1e-4f) << "the handle stops at the other one";

    // Pushing again changes nothing in the curve, and the handle still goes back.
    EXPECT_TRUE(r.box->minHandle().keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    EXPECT_NEAR(r.box->minHandle().value(), 0.6f, 1e-4f);

    EXPECT_TRUE(r.box->maxHandle().keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
    EXPECT_NEAR(r.curve().rangeMax, 0.6f, 1e-4f);
    EXPECT_NEAR(r.box->maxHandle().value(), 0.6f, 1e-4f);
}

TEST_F(ModuleComponentTest, CurveBoxSavePromptAnsweredAfterTheBoxClosedDoesNotCrash) {
    BoxRig r;
    std::function<void(const juce::String&)> pending;
    r.box->namePrompt = [&pending](std::function<void(const juce::String&)> done) { pending = std::move(done); };
    clickNow(r.box->saveButton());
    ASSERT_TRUE(pending);
    r.box.reset();
    pending("Late");
    EXPECT_TRUE(synth::ui::ModCurvePresetStore(r.folder.dir).userPresets().empty());
}

TEST_F(ModuleComponentTest, CurveBoxSavedCurvesRoundTripAndShowInASecondBox) {
    BoxRig r;
    clickNow(r.box->presetChip(2));
    r.box->namePrompt = [](std::function<void(const juce::String&)> done) { done("Snappy"); };
    clickNow(r.box->saveButton());
    ASSERT_EQ(r.box->savedCurves().size(), 1u);

    synth::ui::ModCurveBox second(r.target, synth::ui::ModCurvePresetStore(r.folder.dir));
    ASSERT_EQ(second.savedCurves().size(), 1u) << "a second box sees the newly saved curve";
    clickNow(second.presetChip(0));
    EXPECT_TRUE(r.curve().isLinear());
    EXPECT_TRUE(second.applySavedCurve("Snappy"));
    EXPECT_EQ(r.curve(), ModCurve::preset(Preset::Log));
    EXPECT_FALSE(second.applySavedCurve("Nope"));
}

// The Free-mode line-click opt-in on the shared curve editor and the box's use of it: a press on the line adds a point,
// a right-click on a point removes it, and an editor that has not opted in (the LFO and envelope cards) is unchanged.

using synth::ui::CurveEditorComponent;
using synth::ui::CurveEditorGeometry;
using namespace synth::ui::test;

namespace {
synth::ui::CurveModel diagonalModel() { return synth::ui::curveModelFromWave(synth::ModCurve::straightShape()); }

juce::Point<float> lineAt(CurveEditorComponent& ed, double x) {
    const CurveEditorGeometry g(ed.getModel(), ed.getLocalBounds().toFloat());
    return g.playheadPosition(ed.getModel().playheadForX(x));
}
} // namespace

TEST(CurveEditorLineClick, OptInPressOnTheLineAddsAPointInOneGesture) {
    CurveEditorComponent ed;
    ed.setSize(300, 200);
    ed.setModel(diagonalModel());
    ed.setLineClickEditing(true);
    int starts = 0, ends = 0;
    ed.onGestureStart = [&] { ++starts; };
    ed.onGestureEnd = [&] { ++ends; };

    const auto p = lineAt(ed, 0.25);
    ed.mouseDown(curveLeftClick(ed, p));
    ed.mouseDrag(curveLeftDrag(ed, p.translated(0.0f, -20.0f), p));
    ed.mouseUp(curveLeftClick(ed, p.translated(0.0f, -20.0f)));
    EXPECT_EQ(ed.getModel().getNumNodes(), 3);
    EXPECT_EQ(starts, 1);
    EXPECT_EQ(ends, 1) << "the add and the drag that follows it are one gesture";
}

TEST(CurveEditorLineClick, OptInPressAwayFromTheLineAddsNothing) {
    CurveEditorComponent ed;
    ed.setSize(300, 200);
    ed.setModel(diagonalModel());
    ed.setLineClickEditing(true);
    const auto p = lineAt(ed, 0.25);
    ed.mouseDown(curveLeftClick(ed, p.translated(0.0f, 50.0f)));
    ed.mouseUp(curveLeftClick(ed, p.translated(0.0f, 50.0f)));
    EXPECT_EQ(ed.getModel().getNumNodes(), 2);
}

TEST(CurveEditorLineClick, DefaultEditorIgnoresALineClickAndKeepsDoubleClickAdding) {
    CurveEditorComponent ed;
    ed.setSize(300, 200);
    ed.setModel(diagonalModel());
    const auto p = lineAt(ed, 0.25);
    ed.mouseDown(curveLeftClick(ed, p));
    ed.mouseUp(curveLeftClick(ed, p));
    EXPECT_EQ(ed.getModel().getNumNodes(), 2) << "the LFO and envelope cards do not add on a single click";
    ed.mouseDoubleClick(curveDoubleClick(ed, p.translated(40.0f, -40.0f)));
    EXPECT_EQ(ed.getModel().getNumNodes(), 3);
}

TEST_F(ModuleComponentTest, CurveBoxLineClickAddsAndRightClickRemovesEachAsOneUndoStep) {
    BoxRig r;
    const auto p = r.onLine(0.25);
    r.pressDragRelease(p, p);
    ASSERT_EQ(r.editor().getModel().getNumNodes(), 3);
    EXPECT_EQ(r.curve().shape.points.size(), 3u) << "the new point reached the attenuverter";

    const auto node = r.geometry().nodePosition(1);
    r.editor().mouseDown(curveRightClick(r.editor(), node));
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 2);
    EXPECT_TRUE(r.curve().isLinear());

    r.f.undo.undo(); // the removal
    EXPECT_EQ(r.curve().shape.points.size(), 3u);
    r.f.undo.undo(); // the add
    EXPECT_TRUE(r.curve().isLinear());
    EXPECT_FALSE(r.f.undo.canUndo());
}

TEST_F(ModuleComponentTest, CurveBoxEndPointsCannotBeRemovedByRightClick) {
    BoxRig r;
    r.editor().mouseDown(curveRightClick(r.editor(), r.geometry().nodePosition(0)));
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 2);
    EXPECT_FALSE(r.f.undo.canUndo());
}

// The enlarged curve window's content: Escape and Done close it through one path, Reset returns to a straight line,
// and a preset chip edits the same attenuverter curve the small box does.

namespace {
struct WindowRig {
    Fixture f;
    std::shared_ptr<synth::ui::ModCurveEditTarget> target =
        std::make_shared<synth::ui::ModCurveEditTarget>(*f.editor, f.attenId);
    synth::ui::ModCurveWindowContent content{target};
    int closed = 0;
    WindowRig() {
        content.setSize(560, 420);
        content.onClose = [this] { ++closed; };
    }
};
} // namespace

TEST_F(ModuleComponentTest, CurveWindowEscapeAndDoneCloseAndReportOnce) {
    WindowRig r;
    EXPECT_TRUE(r.content.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(r.closed, 1);
    clickNow(r.content.doneButton());
    EXPECT_EQ(r.closed, 2);
}

TEST_F(ModuleComponentTest, CurveWindowTitleNamesSourceAndDestination) {
    WindowRig r;
    EXPECT_TRUE(r.content.windowTitle().startsWith("Curve: "));
    EXPECT_TRUE(r.content.windowTitle().contains(" to "));
}

TEST_F(ModuleComponentTest, CurveWindowPresetThenResetIsTwoUndoStepsAndEndsLinear) {
    WindowRig r;
    clickNow(r.content.presetChip(1)); // Exp
    EXPECT_FALSE(r.target->read()->isLinear());
    clickNow(r.content.resetButton());
    EXPECT_TRUE(r.target->read()->isLinear());
    r.f.undo.undo();
    EXPECT_FALSE(r.target->read()->isLinear());
    r.f.undo.undo();
    EXPECT_TRUE(r.target->read()->isLinear());
}

TEST_F(ModuleComponentTest, CurveWindowClosesWhenItsAttenuverterIsDeleted) {
    WindowRig r;
    r.f.engine.getGraph().removeNode(r.f.attenId);
    r.content.view().refreshFromTarget(false);
    r.content.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    EXPECT_GE(r.closed, 1);
}
