// The small curve box, the point editing it shares with the enlarged window, and that window: its presets, bend handle,
// range handles and saved curves edit the attenuverter's curve, each gesture is one undo step, and an open box follows
// undo and a deleted attenuverter. docs/modules/modulation.md.

#include "../../../TestWait.h"
#include "ModCurveTestRig.h"
#include "Modules/AttenuverterModule.h"
#include "UI/Graph/GraphEditor/GraphEditorCableCurveChips.h"
#include "UI/Graph/PortPanel/PortPanelController.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
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

TEST_F(ModuleComponentTest, CurveBoxButtonsSayLoadSaveAndEnlargeAndEveryOneIsNamed) {
    BoxRig r;
    EXPECT_EQ(r.box->savedButton().getButtonText(), "Load");
    EXPECT_EQ(r.box->saveButton().getButtonText(), "Save...");
    EXPECT_EQ(r.box->enlargeButton().getButtonText(), "Enlarge");
    for (auto* b : {&r.box->savedButton(), &r.box->saveButton(), &r.box->enlargeButton()}) {
        EXPECT_FALSE(b->getTitle().isEmpty()) << b->getButtonText();
        EXPECT_FALSE(b->getTooltip().isEmpty()) << b->getButtonText();
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

// Points on the mod curve: a single click on the line adds nothing, a double-click on the line adds a point that
// stays, a double-click on a point removes it, and a right-click on a point opens the shared point menu without
// deleting. Each is driven through the editor's real mouse path and is one undo step.

using synth::ui::CurveEditorComponent;
using synth::ui::CurveEditorGeometry;
using namespace synth::ui::test;

TEST_F(ModuleComponentTest, CurveBoxSingleClickOnTheLineAddsNothing) {
    BoxRig r;
    const auto p = r.onLine(0.25);
    r.pressDragRelease(p, p);
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 2);
    EXPECT_TRUE(r.curve().isLinear());
    EXPECT_FALSE(r.f.undo.canUndo());
}

TEST_F(ModuleComponentTest, CurveBoxDoubleClickOnTheLineAddsAPointThatStaysAsOneUndoStep) {
    BoxRig r;
    r.doubleClick(r.onLine(0.25));
    synth::test::flushMessageQueue();
    ASSERT_EQ(r.editor().getModel().getNumNodes(), 3);
    EXPECT_EQ(r.curve().shape.points.size(), 3u) << "the new point reached the attenuverter";

    r.f.undo.undo();
    EXPECT_TRUE(r.curve().isLinear());
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 2) << "the editor followed the undo";
    EXPECT_FALSE(r.f.undo.canUndo()) << "the add was exactly one step";
}

TEST_F(ModuleComponentTest, CurveBoxDoubleClickOnAPointRemovesItAsOneUndoStep) {
    BoxRig r;
    r.doubleClick(r.onLine(0.25));
    synth::test::flushMessageQueue();
    ASSERT_EQ(r.editor().getModel().getNumNodes(), 3);

    r.doubleClick(r.geometry().nodePosition(1));
    synth::test::flushMessageQueue();
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 2);
    EXPECT_TRUE(r.curve().isLinear());

    r.f.undo.undo(); // the removal
    EXPECT_EQ(r.curve().shape.points.size(), 3u);
    r.f.undo.undo(); // the add
    EXPECT_TRUE(r.curve().isLinear());
    EXPECT_FALSE(r.f.undo.canUndo());
}

TEST_F(ModuleComponentTest, CurveBoxRightClickOnAPointOpensTheMenuAndDeletesNothingUntilItIsPicked) {
    BoxRig r;
    r.doubleClick(r.onLine(0.25));
    synth::test::flushMessageQueue();
    ASSERT_EQ(r.editor().getModel().getNumNodes(), 3);

    const auto node = r.geometry().nodePosition(1);
    r.editor().mouseDown(curveRightClick(r.editor(), node));
    EXPECT_EQ(r.menus, 1) << "a right-click on a point opens the point menu";
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 3) << "and removes nothing";
    EXPECT_EQ(r.curve().shape.points.size(), 3u);
    ASSERT_TRUE(r.deleteEnabled);
    ASSERT_TRUE(r.deleteAction);

    r.deleteAction();
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 2);
    EXPECT_TRUE(r.curve().isLinear());
    r.f.undo.undo(); // the removal
    EXPECT_EQ(r.curve().shape.points.size(), 3u);
}

TEST_F(ModuleComponentTest, CurveBoxEndPointsOfferNoDelete) {
    BoxRig r;
    r.editor().mouseDown(curveRightClick(r.editor(), r.geometry().nodePosition(0)));
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 2);
    EXPECT_FALSE(r.deleteEnabled) << "an end point cannot be deleted";
    EXPECT_FALSE(r.f.undo.canUndo());
    r.doubleClick(r.geometry().nodePosition(0));
    EXPECT_EQ(r.editor().getModel().getNumNodes(), 2) << "nor removed by a double-click";
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

// Regression test for FRO1446: the enlarged curve window does what its hint says, each sentence proved through the
// real mouse path of the editor in the window (press, drag, release, right-click), with the change watchers and the
// message queue running between the steps: double-click adds a point, drag moves it, double-click on it removes it,
// right-click opens its menu, the ring on a sloping segment bends it, Snap rounds to the grid and Shift places freely.
namespace {

using synth::ui::CurveEditorComponent;
using synth::ui::CurveEditorGeometry;

struct WindowEditRig {
    Fixture f;
    std::shared_ptr<synth::ui::ModCurveEditTarget> target =
        std::make_shared<synth::ui::ModCurveEditTarget>(*f.editor, f.attenId);
    // A box open beside the window keeps its own view in step through the same watchers.
    synth::ui::ModCurveWindowContent content{target};
    int menus = 0;
    WindowEditRig() {
        content.setSize(520, 440);
        content.view().showMenu = [this](juce::PopupMenu&, juce::Rectangle<int>) { ++menus; };
    }

    CurveEditorComponent& ed() { return content.view().editor(); }
    synth::ModCurve curve() const { return target->read().value(); }
    // The mapping the editor itself hits and paints with, so a view that draws the line somewhere else fails.
    CurveEditorGeometry geometry() { return ed().getGeometryForTest(); }
    juce::Point<float> onLine(double x) { return geometry().playheadPosition(ed().getModel().playheadForX(x)); }
    void settle() { synth::test::flushMessageQueue(); }
    void press(juce::Point<float> p,
               juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
        ed().mouseDown(synth::ui::test::makeCurveMouseEvent(ed(), p, mods, false, p));
        settle();
    }
    void dragTo(juce::Point<float> to, juce::Point<float> from,
                juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
        ed().mouseDrag(synth::ui::test::makeCurveMouseEvent(ed(), to, mods, true, from));
        settle();
    }
    void release(juce::Point<float> p, juce::Point<float> from,
                 juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
        ed().mouseUp(synth::ui::test::makeCurveMouseEvent(ed(), p, mods, true, from));
        settle();
    }
    // A real double-click: press, release, press again (click count 2), then the release that raises the double-click.
    void doubleClick(juce::Point<float> p) {
        using synth::ui::test::makeCurveMouseEvent;
        const juce::ModifierKeys left(juce::ModifierKeys::leftButtonModifier);
        for (const int clicks : {1, 2}) {
            ed().mouseDown(makeCurveMouseEvent(ed(), p, left, false, p, clicks));
            settle();
            ed().mouseUp(makeCurveMouseEvent(ed(), p, left, false, p, clicks));
            settle();
        }
        ed().mouseDoubleClick(makeCurveMouseEvent(ed(), p, left, false, p, 2));
        settle();
    }
    void drag(juce::Point<float> from, juce::Point<float> to,
              juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
        press(from, mods);
        dragTo(to, from, mods);
        release(to, from, mods);
    }
};

} // namespace

TEST_F(ModuleComponentTest, WindowSingleClickOnTheLineAddsNothing) {
    WindowEditRig r;
    const auto p = r.onLine(0.25);
    r.drag(p, p);
    EXPECT_EQ(r.ed().getModel().getNumNodes(), 2);
    EXPECT_TRUE(r.curve().isLinear());
    EXPECT_FALSE(r.f.undo.canUndo());
}

TEST_F(ModuleComponentTest, WindowDoubleClickOnTheLineAddsAPointThatStaysAndReachesTheCurve) {
    WindowEditRig r;
    const auto p = r.onLine(0.25);
    r.doubleClick(p);
    EXPECT_EQ(r.ed().getModel().getNumNodes(), 3) << "the point is in the editor";
    EXPECT_EQ(r.curve().shape.points.size(), 3u) << "and in the attenuverter's curve";
    r.settle();
    EXPECT_EQ(r.ed().getModel().getNumNodes(), 3) << "a watcher refresh did not take it away";
}

TEST_F(ModuleComponentTest, WindowAddedPointsAtEveryAlongTheLineStayAndKeepTheirPlace) {
    for (const double x : {0.1, 0.2, 0.3, 0.4, 0.6, 0.7, 0.8, 0.9}) {
        WindowEditRig r;
        const auto p = r.onLine(x);
        r.doubleClick(p);
        ASSERT_EQ(r.curve().shape.points.size(), 3u) << "x " << x;
        EXPECT_NEAR(r.curve().shape.points[1].x, x, 0.03) << "x " << x;
        EXPECT_NEAR(r.curve().shape.points[1].y, x, 0.06) << "on the line it was clicked on, x " << x;
    }
}

TEST_F(ModuleComponentTest, WindowSeveralDoubleClicksOnTheLineAddSeveralPoints) {
    WindowEditRig r;
    for (const double x : {0.2, 0.8, 0.4}) {
        const auto p = r.onLine(x);
        r.doubleClick(p);
    }
    EXPECT_EQ(r.curve().shape.points.size(), 5u);
    EXPECT_EQ(r.ed().getModel().getNumNodes(), 5);
}

TEST_F(ModuleComponentTest, WindowDragMovesAPointAndDoubleClickRemovesIt) {
    WindowEditRig r;
    r.doubleClick(r.onLine(0.25));
    ASSERT_EQ(r.ed().getModel().getNumNodes(), 3);

    const float yBefore = r.curve().shape.points[1].y;
    const auto node = r.geometry().nodePosition(1);
    const auto to = node + juce::Point<float>(60.0f, -50.0f);
    r.drag(node, to);
    const auto moved = r.curve().shape.points[1];
    EXPECT_GT(moved.x, 0.25f) << "the point followed the drag in time";
    EXPECT_GT(moved.y, yBefore + 0.05f) << "and in level";

    const auto at = r.geometry().nodePosition(1);
    r.ed().mouseDown(synth::ui::test::curveRightClick(r.ed(), at));
    r.settle();
    EXPECT_EQ(r.menus, 1) << "a right-click opens the point menu";
    EXPECT_EQ(r.ed().getModel().getNumNodes(), 3) << "and removes nothing";

    r.doubleClick(at);
    EXPECT_EQ(r.ed().getModel().getNumNodes(), 2);
    EXPECT_EQ(r.curve().shape.points.size(), 2u);
    EXPECT_TRUE(r.curve().isLinear());
}

TEST_F(ModuleComponentTest, WindowDraggingTheRingOnASlopingSegmentBendsIt) {
    WindowEditRig r;
    const auto handle = r.geometry().bendHandlePosition(0);
    ASSERT_TRUE(handle.has_value());
    r.drag(*handle, *handle + juce::Point<float>(0.0f, -40.0f));
    EXPECT_FALSE(r.curve().isLinear());
    EXPECT_GT(std::abs(r.curve().shape.points[0].bend), 0.05f);
    EXPECT_EQ(r.curve().shape.points.size(), 2u) << "bending adds no point";
}

TEST_F(ModuleComponentTest, WindowBendHandlesStayOnEverySlopingSegmentAfterPointsAreAdded) {
    WindowEditRig r;
    r.doubleClick(r.onLine(0.25));
    r.doubleClick(r.onLine(0.75));
    ASSERT_EQ(r.ed().getModel().getNumNodes(), 4);
    for (int seg = 0; seg < 3; ++seg)
        EXPECT_TRUE(r.geometry().bendHandlePosition(seg).has_value()) << "segment " << seg;
}

TEST_F(ModuleComponentTest, WindowSnapRoundsNewAndMovedPointsToTheGridAndShiftPlacesFreely) {
    WindowEditRig r;
    r.content.gridBox().setSelectedId(2, juce::sendNotificationSync); // 1/4
    r.content.snapButton().setToggleState(true, juce::sendNotificationSync);

    const auto p = r.onLine(0.3);
    r.doubleClick(p);
    ASSERT_EQ(r.curve().shape.points.size(), 3u);
    EXPECT_NEAR(r.curve().shape.points[1].x, 0.25f, 1e-4f) << "x snapped to a quarter";
    EXPECT_NEAR(r.curve().shape.points[1].y * 4.0f, std::round(r.curve().shape.points[1].y * 4.0f), 1e-3f);

    const auto node = r.geometry().nodePosition(1);
    const auto to = node + juce::Point<float>(23.0f, 0.0f);
    const auto shift = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    r.drag(node, to, shift);
    const float free = r.curve().shape.points[1].x;
    EXPECT_GT(std::abs(free * 4.0f - std::round(free * 4.0f)), 0.02f) << "Shift ignored the grid";

    r.content.snapButton().setToggleState(false, juce::sendNotificationSync);
    const auto q = r.onLine(0.85);
    r.doubleClick(q);
    bool offGrid = false;
    for (const auto& pt : r.curve().shape.points)
        offGrid = offGrid || std::abs(pt.x * 4.0f - std::round(pt.x * 4.0f)) > 0.02f;
    EXPECT_TRUE(offGrid) << "with Snap off a point lands where it was clicked";
}

TEST_F(ModuleComponentTest, WindowEveryHintSentenceNamesAThingTheWindowHas) {
    const auto hint = synth::ui::ModCurveWindowContent::hintText();
    for (const char* sentence :
         {"Double-click to add a point; double-click a point to remove it.",
          "Drag a point to move it, or right-click it for more.", "Drag the ring on a sloping segment to bend it.",
          "Snap rounds points to the grid; hold Shift to place them freely."})
        EXPECT_TRUE(hint.contains(sentence)) << sentence << " is proved by a test of this file";
    WindowEditRig r;
    EXPECT_EQ(r.content.snapButton().getTitle(), "Snap");
    EXPECT_EQ(r.content.gridBox().getTitle(), "Curve grid");
    EXPECT_TRUE(r.content.snapButton().getWantsKeyboardFocus());
    EXPECT_TRUE(r.content.gridBox().getWantsKeyboardFocus());
    EXPECT_FALSE(r.content.snapButton().getTooltip().isEmpty());
    EXPECT_FALSE(r.content.gridBox().getTooltip().isEmpty());
}

TEST_F(ModuleComponentTest, WindowGridBoxNamesAreCellsAcrossTheRange) {
    EXPECT_EQ(synth::ui::ModCurveWindowContent::gridDivisions("1/8"), 8);
    EXPECT_EQ(synth::ui::ModCurveWindowContent::gridDivisions("1/2"), 2);
    EXPECT_EQ(synth::ui::ModCurveWindowContent::gridDivisions("Grid 1/16"), 16) << "an old saved name still reads";
    WindowEditRig r;
    EXPECT_EQ(r.content.gridBox().getText(), "1/8");
    for (const auto& name : r.content.gridBox().getChoices())
        EXPECT_FALSE(name.containsIgnoreCase("grid")) << "the word Grid is the label's, once";
    EXPECT_FALSE(r.content.snapButton().getToggleState()) << "Snap starts off: nothing moves until it is asked to";
}

TEST_F(ModuleComponentTest, WindowSnapButtonFlipsOnAClickAndOnReturnAndShowsItsWord) {
    WindowEditRig r;
    auto& snap = r.content.snapButton();
    EXPECT_EQ(snap.getButtonText(), "Snap") << "icon and word, like the timeline's";
    EXPECT_EQ(snap.getComponentID(), "modCurveSnap");
    EXPECT_EQ(snap.getIcon(), synth::theme::Icon::ToolbarSnap) << "the one shared Snap button";
    snap.triggerClick();
    r.settle();
    EXPECT_TRUE(snap.getToggleState());
    EXPECT_TRUE(static_cast<juce::Component&>(snap).keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    r.settle();
    EXPECT_FALSE(snap.getToggleState());
}

namespace {
void expectBottomRowClear(synth::ui::ModCurveWindowContent& content, int w, int h) {
    content.setSize(w, h);
    auto& reset = content.resetButton();
    auto& done = content.doneButton();
    auto& snap = content.snapButton();
    auto& label = content.gridLabel();
    auto& box = content.gridBox();
    const std::vector<std::pair<const char*, juce::Component*>> row{
        {"Snap", &snap}, {"Grid label", &label}, {"Grid box", &box}, {"Reset", &reset}, {"Done", &done}};
    for (size_t i = 0; i < row.size(); ++i) {
        EXPECT_GT(row[i].second->getWidth(), 0) << row[i].first << " at " << w << "x" << h;
        EXPECT_TRUE(content.getLocalBounds().contains(row[i].second->getBounds())) << row[i].first;
        for (size_t j = i + 1; j < row.size(); ++j)
            EXPECT_FALSE(row[i].second->getBounds().intersects(row[j].second->getBounds()))
                << row[i].first << " overlaps " << row[j].first << " at " << w << "x" << h;
    }
    EXPECT_LE(label.getRight(), box.getX()) << "the label sits left of its box";
    EXPECT_GE(label.getX(), snap.getRight());
}
} // namespace

TEST_F(ModuleComponentTest, WindowGridLabelSitsLeftOfItsBoxAndNothingInTheBottomRowOverlaps) {
    WindowEditRig r;
    EXPECT_EQ(r.content.gridLabel().getText(), "Grid");
    EXPECT_TRUE(r.content.gridLabel().isShowing() || r.content.gridLabel().isVisible());
    EXPECT_FALSE(r.content.gridLabel().getWantsKeyboardFocus()) << "the label is not a Tab stop";
    expectBottomRowClear(r.content, 520, 440);
    expectBottomRowClear(r.content, synth::ui::ModCurveWindowContent::kMinWidth,
                         synth::ui::ModCurveWindowContent::kMinHeight);
}

// Regression test for FRO1449: a click on the anchor of an open panel closes it, and never re-opens it. A press on its
// own anchor does not dismiss the panel (the frame leaves it to the owner), so each anchor's own click handler has to
// treat the open panel as a toggle. One guard walks every anchor family that opens a panel in its own frame: the mod
// dot, the port jack, the curve chip in a dot row and the curve chip on a cable.
namespace {

using Cable = graph_editor_types::VisibleCable;

struct HeldBox {
    std::unique_ptr<synth::ui::ModCurveBox> box;
    int opened = 0;
    HeldBox() {
        synth::ui::test_hooks::modCurvePopoverLauncherForTest() = [this](std::unique_ptr<synth::ui::ModCurveBox> b,
                                                                         juce::Component&) {
            box = std::move(b);
            ++opened;
        };
    }
    ~HeldBox() {
        box.reset();
        synth::ui::test_hooks::modCurvePopoverLauncherForTest() = nullptr;
        synth::ui::closeModCurvePopover();
    }
};

} // namespace

TEST_F(ModuleComponentTest, ACurveChipOnACableTogglesItsBoxOnEveryPressOnIt) {
    Fixture f;
    HeldBox held;
    f.refresh();
    f.editor->showCurvesOnCables = true;
    const Cable* chain = nullptr;
    const auto cables = f.editor->buildVisibleCables();
    for (const auto& c : cables)
        if (c.kind == Cable::Kind::AttenuverterChain)
            chain = &c;
    ASSERT_NE(chain, nullptr);
    const auto centre = synth::ui::cable_curve_chips::chipRect(*chain).getCentre();

    EXPECT_TRUE(synth::ui::cable_curve_chips::press(*f.editor, *f.editor, centre));
    EXPECT_TRUE(synth::ui::isModCurvePopoverOpen());
    EXPECT_EQ(held.opened, 1);

    EXPECT_TRUE(synth::ui::cable_curve_chips::press(*f.editor, *f.editor, centre)) << "the press is consumed";
    EXPECT_FALSE(synth::ui::isModCurvePopoverOpen()) << "a second press on the chip closes the box";
    EXPECT_EQ(held.opened, 1) << "and does not open it again";

    EXPECT_TRUE(synth::ui::cable_curve_chips::press(*f.editor, *f.editor, centre));
    EXPECT_TRUE(synth::ui::isModCurvePopoverOpen()) << "a third opens it once more";
    EXPECT_EQ(held.opened, 2);
}

TEST_F(ModuleComponentTest, ACurveChipInADotRowTogglesItsBoxOnEveryClick) {
    Fixture f;
    HeldBox held;
    synth::ui::ModCurveChip chip;
    synth::ui::wireModCurveChip(chip, *f.editor, f.attenId, "LFO");
    clickNow(chip);
    EXPECT_TRUE(synth::ui::isModCurvePopoverOpen());
    clickNow(chip);
    EXPECT_FALSE(synth::ui::isModCurvePopoverOpen());
    EXPECT_EQ(held.opened, 1);
}

TEST_F(ModuleComponentTest, TheModDotAndThePortJackToggleTheirPanelOnASecondClick) {
    Fixture f;
    ASSERT_NE(f.clickDot(), nullptr);
    EXPECT_NE(f.editor->getModDot().getPopover(), nullptr);
    f.clickDot();
    EXPECT_EQ(f.editor->getModDot().getPopover(), nullptr) << "a second click on the dot closes its panel";

    std::unique_ptr<juce::Component> heldPanel;
    f.editor->getPortPanel().panelLauncher = [&heldPanel](std::unique_ptr<juce::Component> content, juce::Component&,
                                                          juce::Rectangle<int>) { heldPanel = std::move(content); };
    f.editor->setDoubleClickPortDisconnectEnabled(false);
    Cable cable;
    for (const auto& c : f.editor->buildVisibleCables())
        if (c.id.attenUid != 0)
            cable = c;
    auto* vca = dynamic_cast<ModuleBase*>(f.engine.getGraph().getNodeForId(f.vcaId)->getProcessor());
    const synth::ui::PortRef jack{f.vcaId, vca->mapInputChannel(cable.id.dstPort).visibleJackIndex, true, false};
    const juce::Rectangle<int> area(0, 0, 10, 10);
    const auto click = synth::ui::test::curveLeftClick(*f.editor, {5.0f, 5.0f});

    f.editor->getPortPanel().jackReleased(*f.editor, area, jack, click);
    EXPECT_TRUE(f.editor->getPortPanel().isOpenFor(jack));
    f.editor->getPortPanel().jackReleased(*f.editor, area, jack, click);
    EXPECT_FALSE(f.editor->getPortPanel().isOpenFor(jack)) << "a second click on the jack closes its panel";
    heldPanel.reset();
}
