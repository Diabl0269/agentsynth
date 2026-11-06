// ShapeDrawGestureTests.cpp -- the shape window's Draw pen: a stroke replaces only the dragged stretch of the curve,
// Free follows the mouse and is thinned, Line ramps, Escape cancels, a stroke is one undo step, the grid snaps its
// ends (Shift frees them), what the drag previews is what the release commits, and with the pen off points edit as
// before. Run for both the Filter's and the LFO's window.
#include "../../../TestSettingsHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/LFOModule.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawing.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"
#include <cmath>

using namespace synth::ui;
using namespace synth::ui::test;
using synth::LfoCustomWave;

namespace {

struct LfoRig {
    AudioEngine engine;
    LFOModule* lfo = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    AppUndoManager undo;
    std::shared_ptr<LfoShapesAdapter> adapter;

    LfoRig() {
        lfo = new LFOModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
        adapter = std::make_shared<LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
    }
};

/** One window plus the real-mouse helpers the tests share. */
struct Pen {
    Pen(ShapeListEditor& e, AppUndoManager& u, std::shared_ptr<ShapeListAdapter> a)
        : editor(e)
        , undo(u)
        , adapter(std::move(a)) {}
    ShapeListEditor& editor;
    AppUndoManager& undo;
    std::shared_ptr<ShapeListAdapter> adapter;

    CurveEditorComponent& curve() { return editor.getCurveEditor(); }
    LfoCustomWave stored() const { return adapter->getShapes()[0].curve; }
    juce::Point<float> at(double x, float level) { return curve().pixelForDrawPoint({x, (double)level}); }

    void press(double x, float level, bool shift = false) {
        const auto p = at(x, level);
        curve().mouseDown(make(p, p, false, shift));
        anchor_ = p;
    }
    void drag(double x, float level, bool shift = false) {
        curve().mouseDrag(make(at(x, level), anchor_, true, shift));
        last_ = at(x, level);
    }
    void release() { curve().mouseUp(make(last_, anchor_, true, false)); }
    /** Press at x0, drag to x1 in a few frames, release. */
    void stroke(double x0, double x1, float level = 0.5f, float level1 = -1.0f, bool shift = false) {
        press(x0, level, shift);
        for (int i = 1; i <= 4; ++i)
            drag(x0 + (x1 - x0) * i / 4.0, level1 < 0 ? level : level + (level1 - level) * i / 4.0f, shift);
        release();
    }

private:
    juce::MouseEvent make(juce::Point<float> p, juce::Point<float> from, bool dragged, bool shift) {
        const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier |
                                             (shift ? juce::ModifierKeys::shiftModifier : 0));
        return makeCurveMouseEvent(curve(), p, mods, dragged, from);
    }
    juce::Point<float> anchor_, last_;
};

/** The points of `curve` outside (lo, hi): what a stroke over lo..hi must leave alone. */
std::vector<LfoCustomWave::Point> outside(const LfoCustomWave& curve, double lo, double hi) {
    std::vector<LfoCustomWave::Point> out;
    for (const auto& p : curve.points)
        if (p.x < lo - 1e-6 || p.x > hi + 1e-6)
            out.push_back(p);
    return out;
}

bool samePoints(const std::vector<LfoCustomWave::Point>& a, const std::vector<LfoCustomWave::Point>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].bend != b[i].bend)
            return false;
    return true;
}

} // namespace

TEST(ShapeDrawGesture, EveryShapeReplacesOnlyTheDraggedStretch) {
    for (const auto shape :
         {DrawShape::Line, DrawShape::Sine, DrawShape::Triangle, DrawShape::Saw, DrawShape::Square}) {
        ShapeRig rig;
        ShapeListEditor editor(rig.adapter, 0);
        Pen pen(editor, rig.undo, rig.adapter);
        const auto before = pen.stored();
        editor.setPen(shape);

        pen.stroke(0.25, 0.75, 0.5f, shape == DrawShape::Line ? 0.8f : -1.0f);

        const auto after = pen.stored();
        EXPECT_NE(after, before) << drawShapeName(shape);
        EXPECT_TRUE(samePoints(outside(after, 0.25, 0.75), outside(before, 0.25, 0.75)))
            << drawShapeName(shape) << ": the curve outside the stretch is as it was";
        double first = 2.0, last = -1.0;
        for (const auto& p : after.points)
            if (p.x >= 0.25f - 1e-6f && p.x <= 0.75f + 1e-6f) {
                first = std::min(first, (double)p.x);
                last = std::max(last, (double)p.x);
            }
        EXPECT_NEAR(first, 0.25, 1e-3) << drawShapeName(shape);
        EXPECT_NEAR(last, 0.75, 1e-3) << drawShapeName(shape);
    }
}

TEST(ShapeDrawGesture, ASineReadsTheCyclesInItsChipAndStaysInTheDefaultBand) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    const auto range = rig.adapter->getDrawRange();
    editor.setPen(DrawShape::Sine);

    pen.press(0.25, 0.5f);
    pen.drag(0.75, 0.5f);
    EXPECT_TRUE(pen.curve().getDrawGesture().chipText().contains("cycle")) << pen.curve().getDrawGesture().chipText();
    pen.release();

    for (const auto& p : pen.stored().points)
        if (p.x > 0.26f && p.x < 0.74f) {
            EXPECT_GE(p.y, range.lo - 1e-3f);
            EXPECT_LE(p.y, range.hi + 1e-3f);
        }
}

TEST(ShapeDrawGesture, FreeFollowsTheMouseAndIsThinnedToAHandfulOfPoints) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    const auto before = pen.stored();
    editor.setPen(DrawShape::Free);

    pen.press(0.3, 0.5f);
    const int frames = 80;
    for (int i = 1; i <= frames; ++i)
        pen.drag(0.3 + 0.4 * i / frames, 0.5f + 0.25f * std::sin(i * 0.25f));
    EXPECT_FALSE(pen.curve().getDrawGesture().result().curve == before) << "the preview follows the mouse";
    pen.release();

    const auto after = pen.stored();
    const auto inside = after.points.size() - outside(after, 0.3, 0.7).size();
    EXPECT_GE(inside, 6u) << "a squiggle keeps its corners";
    EXPECT_LT(inside, (size_t)frames * 3 / 4) << "and is thinned below the mouse samples";
    EXPECT_LE(after.points.size(), (size_t)LfoCustomWave::kMaxPoints)
        << "a long squiggle fits the cap instead of being refused";
    EXPECT_TRUE(samePoints(outside(after, 0.3, 0.7), outside(before, 0.3, 0.7)));
}

TEST(ShapeDrawGesture, LineRampsFromThePressToTheRelease) {
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Line);

    pen.stroke(0.2, 0.8, 0.2f, 0.9f);

    const auto after = pen.stored();
    std::vector<LfoCustomWave::Point> inside;
    for (const auto& p : after.points)
        if (p.x >= 0.2f - 1e-4f && p.x <= 0.8f + 1e-4f)
            inside.push_back(p);
    ASSERT_EQ(inside.size(), 2u);
    EXPECT_NEAR(inside.front().y, 0.2f, 0.03f);
    EXPECT_NEAR(inside.back().y, 0.9f, 0.03f);
}

TEST(ShapeDrawGesture, EscapeCancelsADragAndWritesNothing) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    const auto before = pen.stored();
    const int serial = rig.undo.getEditSerial();
    editor.setPen(DrawShape::Sine);

    pen.press(0.25, 0.5f);
    pen.drag(0.75, 0.5f);
    ASSERT_TRUE(pen.curve().isDrawing());
    EXPECT_TRUE(pen.curve().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(pen.curve().isDrawing());
    pen.release();

    EXPECT_EQ(pen.stored(), before);
    EXPECT_EQ(rig.undo.getEditSerial(), serial);
}

TEST(ShapeDrawGesture, AStrokeIsOneUndoStepAndUndoRestoresTheCurve) {
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    const auto before = pen.stored();
    const int serial = rig.undo.getEditSerial();
    editor.setPen(DrawShape::Triangle);

    pen.stroke(0.25, 0.75);

    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    EXPECT_NE(pen.stored(), before);
    rig.undo.undo();
    EXPECT_EQ(pen.stored(), before);
}

TEST(ShapeDrawGesture, APlainClickWithThePenOnChangesNothing) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    const auto before = pen.stored();
    const int serial = rig.undo.getEditSerial();
    editor.setPen(DrawShape::Sine);

    pen.press(0.4, 0.5f);
    pen.release();

    EXPECT_EQ(pen.stored(), before);
    EXPECT_EQ(rig.undo.getEditSerial(), serial);
}

TEST(ShapeDrawGesture, PreviewAtTheLastDragEqualsTheCommit) {
    for (const auto shape : kAllDrawShapes) {
        for (const bool lfo : {false, true}) {
            ShapeRig filterRig;
            LfoRig lfoRig;
            auto adapter = lfo ? std::shared_ptr<ShapeListAdapter>(lfoRig.adapter)
                               : std::shared_ptr<ShapeListAdapter>(filterRig.adapter);
            ShapeListEditor editor(adapter, 0);
            Pen pen(editor, lfo ? lfoRig.undo : filterRig.undo, adapter);
            editor.setPen(shape);

            pen.press(0.2, 0.4f);
            for (int i = 1; i <= 30; ++i)
                pen.drag(0.2 + 0.55 * i / 30.0, 0.4f + 0.3f * std::sin(i * 0.5f));
            const auto preview = pen.curve().getDrawGesture().result().curve;
            pen.release();

            EXPECT_EQ(pen.stored(), preview) << drawShapeName(shape) << (lfo ? " (LFO)" : " (Filter)");
        }
    }
}

TEST(ShapeDrawGesture, ThePreviewIsPaintedWhileDraggingAndGoneAfterTheRelease) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Sine);
    auto& curve = pen.curve();
    auto render = [&] {
        juce::Image image(juce::Image::ARGB, curve.getWidth(), curve.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        curve.paintEntireComponent(g, false);
        return image;
    };
    auto same = [](const juce::Image& a, const juce::Image& b) {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                    return false;
        return true;
    };
    const auto rest = render();
    pen.press(0.25, 0.5f);
    pen.drag(0.75, 0.5f);
    const auto during = render();
    EXPECT_FALSE(same(rest, during)) << "a preview and its chip are drawn";
    pen.curve().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    EXPECT_TRUE(same(rest, render())) << "a cancelled drag leaves the curve as it was";
}

TEST(ShapeDrawGesture, TheGridSnapsTheStretchAndShiftFreesIt) {
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setGridName("1/16");
    editor.setPen(DrawShape::Line);

    pen.press(0.27, 0.5f);
    pen.drag(0.73, 0.5f);
    auto stroke = pen.curve().getDrawGesture().result().curve;
    pen.curve().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    bool snapped = false;
    for (const auto& p : stroke.points)
        snapped = snapped || std::abs(p.x - 0.25f) < 1e-4f;
    EXPECT_TRUE(snapped) << "the start lands on the 1/16 line";

    pen.press(0.27, 0.5f, true);
    pen.drag(0.73, 0.5f, true);
    stroke = pen.curve().getDrawGesture().result().curve;
    pen.curve().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    bool freeStart = false;
    for (const auto& p : stroke.points)
        freeStart = freeStart || std::abs(p.x - 0.27f) < 2e-3f;
    EXPECT_TRUE(freeStart) << "with Shift it starts where the mouse is";
}

TEST(ShapeDrawGesture, ASineOnAFineGridShrinksToTheCyclesThatFit) {
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setGridName("1/16");
    editor.setPen(DrawShape::Sine);

    pen.press(0.0, 0.5f);
    pen.drag(1.0, 0.5f);
    const auto result = pen.curve().getDrawGesture().result();
    pen.release();

    EXPECT_TRUE(result.changed);
    EXPECT_LE((int)result.curve.points.size(), LfoCustomWave::kMaxPoints);
    EXPECT_LT(result.cycles, 16.0) << "sixteen cycles would not fit";
    EXPECT_EQ(pen.stored(), result.curve);
}

TEST(ShapeDrawGesture, WithThePenOffPointsEditAsBefore) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Sine);
    editor.setPen(std::nullopt);
    const auto before = pen.stored();
    const auto& model = editor.getCurveEditor().getModel();
    const auto from = pen.curve().pixelForDrawPoint({model.getNode(3).x, (double)model.getNode(3).y});
    const auto to = from + juce::Point<float>(0.0f, -30.0f);

    pen.curve().mouseDown(curveLeftClick(pen.curve(), from));
    pen.curve().mouseDrag(curveLeftDrag(pen.curve(), to, from));
    pen.curve().mouseUp(curveLeftDrag(pen.curve(), to, from));

    EXPECT_NE(pen.stored(), before);
    EXPECT_EQ(pen.stored().points.size(), before.points.size()) << "a point moved, nothing was drawn";
}

TEST(ShapeDrawGesture, TheDrawButtonPicksAPenShowsItAndLeavesDrawModeOnTheSecondPress) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    std::unique_ptr<DrawShapeFlyout> flyout;
    test_hooks::drawShapeFlyoutHookForTest() = [&](std::unique_ptr<DrawShapeFlyout> f) { flyout = std::move(f); };

    editor.getDrawButton().onClick();
    test_hooks::drawShapeFlyoutHookForTest() = nullptr;
    ASSERT_NE(flyout, nullptr);
    flyout->pick(DrawShape::Saw);

    EXPECT_EQ(editor.getPen(), DrawShape::Saw);
    EXPECT_EQ(editor.getCurveEditor().getDrawPen(), DrawShape::Saw);
    EXPECT_TRUE(editor.getDrawButton().getToggleState());
    EXPECT_EQ(editor.getDrawPenButton().getShownShape(), DrawShape::Saw);
    EXPECT_TRUE(editor.getDrawButton().getTitle().contains("Saw"));
    EXPECT_FALSE(editor.getDrawButton().getTooltip().isEmpty());

    editor.getDrawButton().onClick();
    EXPECT_FALSE(editor.getPen().has_value());
    EXPECT_FALSE(editor.getDrawButton().getToggleState());
    EXPECT_EQ(editor.getDrawButton().getTitle(), "Draw");
}

TEST(ShapeDrawGesture, ThePenIsRememberedPerKindAcrossWindows) {
    synth::test::PersistedKeysGuard keys{
        juce::StringArray{shapewindowprefs::penKey("lfo"), shapewindowprefs::penKey("filter")}};
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    shapewindowprefs::saveText(&props, shapewindowprefs::penKey("lfo"), {});
    shapewindowprefs::saveText(&props, shapewindowprefs::penKey("filter"), {});
    LfoRig lfo;
    ShapeRig filter;
    lfo.adapter->setApplicationProperties(&props);
    filter.adapter->setApplicationProperties(&props);
    {
        ShapeListEditor editor(lfo.adapter, 0);
        EXPECT_FALSE(editor.getPen().has_value()) << "off until chosen";
        editor.setPen(DrawShape::Square);
    }
    {
        ShapeListEditor editor(filter.adapter, 0);
        EXPECT_FALSE(editor.getPen().has_value()) << "the LFO's pen is not the Filter's";
    }
    {
        ShapeListEditor again(lfo.adapter, 0);
        EXPECT_EQ(again.getPen(), DrawShape::Square);
        EXPECT_EQ(again.getCurveEditor().getDrawPen(), DrawShape::Square);
        again.setPen(std::nullopt);
    }
    ShapeListEditor off(lfo.adapter, 0);
    EXPECT_FALSE(off.getPen().has_value());
    EXPECT_EQ(off.getDrawPenButton().getShownShape(), DrawShape::Square) << "leaving keeps the pen shown on the button";
}

TEST(ShapeDrawGesture, EnterAppliesThePenToTheSelectedStretchAsOneUndoStep) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    auto& curve = pen.curve();
    const auto before = pen.stored();
    const int serial = rig.undo.getEditSerial();
    editor.setPen(DrawShape::Sine);

    curve.selectNodeRelative(1); // the first movable point
    curve.extendSelectionRelative(1);
    curve.extendSelectionRelative(1);
    ASSERT_GE(curve.getSelectedIndices().size(), 3u);
    const double x0 = curve.getModel().getNode(curve.getSelectedIndices().front()).x;
    const double x1 = curve.getModel().getNode(curve.getSelectedIndices().back()).x;

    EXPECT_TRUE(curve.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));

    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    EXPECT_TRUE(samePoints(outside(pen.stored(), x0, x1), outside(before, x0, x1))) << "only the selected stretch";
    EXPECT_NE(pen.stored(), before);
    rig.undo.undo();
    EXPECT_EQ(pen.stored(), before);
}

TEST(ShapeDrawGesture, EnterWithNothingSelectedAppliesThePenToTheWholeCurve) {
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Square);
    pen.curve().setModel(pen.curve().getModel()); // no selection

    EXPECT_TRUE(pen.curve().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));

    auto expected = pen.stored();
    EXPECT_EQ(expected,
              curveForDrawShape(DrawShape::Square, LfoCustomWave::defaultWave(), rig.adapter->getDrawRange()));
}

TEST(ShapeDrawGesture, EnterWithTheFreePenSaysItDrawsWithTheMouse) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setPen(DrawShape::Free);
    EXPECT_TRUE(editor.getCurveEditor().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(editor.getStatusText().contains("mouse"));
}

TEST(ShapeDrawGesture, AStrokeThatCannotFitUnderThePointCapIsRefusedWithAReason) {
    LfoCustomWave full; // already at the cap: no room for even one sine cycle
    for (int i = 0; i < LfoCustomWave::kMaxPoints; ++i)
        full.points.push_back({i / (float)(LfoCustomWave::kMaxPoints - 1), 0.5f, 0.0f});
    ShapeDrawStroke s;
    s.shape = DrawShape::Sine;
    s.x0 = 0.4;
    s.x1 = 0.4001;
    s.cycleWidth = 0.0001;

    const auto result = drawOnCurve(full, s);

    EXPECT_FALSE(result.changed);
    EXPECT_TRUE(result.refusal.isNotEmpty());
    EXPECT_EQ(result.curve, full);
}
