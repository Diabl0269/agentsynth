// ShapeDrawGestureTests.cpp -- the shape window's Draw pen: a stroke replaces only the dragged stretch of the curve,
// Free follows the mouse and is thinned, Line ramps, Escape cancels, a stroke is one undo step, the grid snaps its
// ends (Shift frees them), a periodic pen spans the press's and the drag's levels like the lane's box, what the drag
// previews is what the release commits, and with the pen off points edit as before. Run for both the Filter's and the
// LFO's window.
#include "../../../TestSettingsHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/LFOModule.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawing.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"
#include <cmath>
#include <iostream>

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
    /** Where a press at (x, level) lands once snapped to the grid. */
    juce::Point<double> snapped(double x, float level) { return curve().drawPointAt(at(x, level), false); }

    void press(double x, float level, bool shift = false) {
        const auto p = at(x, level);
        curve().mouseDown(make(p, p, false, shift));
        anchor_ = p;
    }
    void drag(double x, float level, bool shift = false, bool alt = false) {
        curve().mouseDrag(make(at(x, level), anchor_, true, shift, alt));
        last_ = at(x, level);
    }
    void dragTo(juce::Point<float> p, bool alt) {
        curve().mouseDrag(make(p, anchor_, true, false, alt));
        last_ = p;
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
    juce::MouseEvent make(juce::Point<float> p, juce::Point<float> from, bool dragged, bool shift, bool alt = false) {
        const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier |
                                             (shift ? juce::ModifierKeys::shiftModifier : 0) |
                                             (alt ? juce::ModifierKeys::altModifier : 0));
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

TEST(ShapeDrawGesture, ALevelDragKeepsTheDefaultHeightCentredOnThePress) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    const auto range = rig.adapter->getDrawRange();
    const float half = (range.hi - range.lo) * 0.5f;
    editor.setPen(DrawShape::Sine);

    pen.press(0.25, 0.5f);
    pen.drag(0.75, 0.5f);
    EXPECT_TRUE(pen.curve().getDrawGesture().chipText().contains("cycle")) << pen.curve().getDrawGesture().chipText();
    pen.release();

    const float centre = (float)pen.snapped(0.25, 0.5f).y;
    float lo = 2.0f, hi = -1.0f;
    for (const auto& p : pen.stored().points)
        if (p.x > 0.26f && p.x < 0.74f) {
            lo = std::min(lo, p.y);
            hi = std::max(hi, p.y);
        }
    EXPECT_NEAR(lo, centre - half, 0.02f);
    EXPECT_NEAR(hi, centre + half, 0.02f);
}

// Regression test for FRO1593: a periodic pen always swung over the fixed default band whatever the drag did.
TEST(ShapeDrawGesture, APeriodicPenSpansThePressAndTheDragLevelsLikeTheLaneBox) {
    for (const auto shape : {DrawShape::Sine, DrawShape::Triangle, DrawShape::Saw, DrawShape::Square}) {
        for (const bool lfo : {false, true}) {
            for (const float endLevel : {0.9f, 0.1f, 0.35f}) {
                ShapeRig filterRig;
                LfoRig lfoRig;
                auto adapter = lfo ? std::shared_ptr<ShapeListAdapter>(lfoRig.adapter)
                                   : std::shared_ptr<ShapeListAdapter>(filterRig.adapter);
                ShapeListEditor editor(adapter, 0);
                Pen pen(editor, lfo ? lfoRig.undo : filterRig.undo, adapter);
                editor.setPen(shape);

                pen.stroke(0.25, 0.75, 0.6f, endLevel);

                const float a = (float)pen.snapped(0.25, 0.6f).y;
                const float b = (float)pen.snapped(0.75, endLevel).y;
                float lo = 2.0f, hi = -1.0f;
                for (const auto& p : pen.stored().points)
                    if (p.x > 0.25f - 1e-4f && p.x < 0.75f + 1e-4f) {
                        lo = std::min(lo, p.y);
                        hi = std::max(hi, p.y);
                    }
                const auto what = juce::String(drawShapeName(shape)) + (lfo ? " (LFO) to " : " (Filter) to ") +
                                  juce::String(endLevel);
                EXPECT_NEAR(lo, std::min(a, b), 0.02f) << what;
                EXPECT_NEAR(hi, std::max(a, b), 0.02f) << what;
                // A rising Saw or Triangle starts at the press, as the lane's does.
                if (endLevel > 0.6f && (shape == DrawShape::Saw || shape == DrawShape::Triangle)) {
                    float first = -1.0f;
                    for (const auto& p : pen.stored().points)
                        if (p.x >= 0.25f - 1e-4f) {
                            first = p.y;
                            break;
                        }
                    EXPECT_NEAR(first, a, 0.02f) << what;
                }
            }
        }
    }
}

TEST(ShapeDrawGesture, DraggingUpOrDownChangesTheWaveSizeWhileDragging) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Sine);
    const auto swing = [&pen] {
        float lo = 2.0f, hi = -1.0f;
        for (const auto& p : pen.curve().getDrawGesture().result().curve.points)
            if (p.x > 0.2f && p.x < 0.8f) {
                lo = std::min(lo, p.y);
                hi = std::max(hi, p.y);
            }
        return hi - lo;
    };

    pen.press(0.2, 0.5f);
    pen.drag(0.8, 0.7f);
    const float small = swing();
    pen.drag(0.8, 0.95f);
    const float big = swing();
    pen.drag(0.8, 0.6f);
    const float smaller = swing();
    pen.release();

    EXPECT_GT(big, small + 0.1f);
    EXPECT_LT(smaller, small);
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
    EXPECT_LT(inside, (size_t)frames) << "and is thinned below the mouse samples";
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
    editor.setGridName("1/128");
    editor.setPen(DrawShape::Sine);

    pen.press(0.0, 0.5f);
    pen.drag(1.0, 0.5f);
    const auto result = pen.curve().getDrawGesture().result();
    pen.release();

    EXPECT_TRUE(result.changed);
    EXPECT_LE((int)result.curve.points.size(), LfoCustomWave::kMaxPoints);
    EXPECT_LT(result.cycles, 128.0) << "128 cycles would not fit";
    EXPECT_EQ(pen.stored(), result.curve);
}

TEST(ShapeDrawGesture, TheCurveTooltipDescribesThePenWhileOnAndIsRestoredWhenOff) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    const auto pointTooltip = curve.getTooltip();
    ASSERT_TRUE(pointTooltip.contains("click a line to add a point"));

    editor.setPen(DrawShape::Sine);
    EXPECT_TRUE(curve.getTooltip().contains("Drag to draw a Sine"));
    EXPECT_TRUE(curve.getTooltip().contains("Esc cancels"));
    EXPECT_FALSE(curve.getTooltip().contains("add a point"));

    editor.setPen(DrawShape::Saw);
    EXPECT_TRUE(curve.getTooltip().contains("Saw")) << "switching pens renames it";

    editor.setPen(std::nullopt);
    EXPECT_EQ(curve.getTooltip(), pointTooltip);
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

namespace {

void expectDrawButtonCentredAndRowUnclipped(ShapeListEditor& editor) {
    editor.setSize(ShapeListEditor::kWidth, ShapeListEditor::kHeight);
    auto& draw = editor.getDrawPenButton();
    const auto layout = draw.contentLayout();
    const auto font = draw.getLookAndFeel().getTextButtonFont(draw, draw.getHeight());
    const float wordWidth = juce::GlyphArrangement::getStringWidth(font, "Draw");
    EXPECT_NEAR(layout.icon.getX(), (float)draw.getWidth() - layout.text.getRight(), 1.0f)
        << "equal margins left and right of the icon-and-word group";
    EXPECT_GE(layout.text.getWidth(), wordWidth) << "the word is not clipped";
    EXPECT_GE(layout.text.getX(), layout.icon.getRight()) << "the icon and the word do not overlap";

    juce::TextButton* row[] = {&editor.getAddButton(),
                               &editor.getDuplicateButton(),
                               &editor.getRenameButton(),
                               &editor.getPickShapeButton(),
                               &draw,
                               &editor.getSaveShapeButton(),
                               &editor.getDeleteButton(),
                               &editor.getRingButton()};
    for (auto* button : row) {
        if (!button->isVisible())
            continue;
        const auto labelFont = button->getLookAndFeel().getTextButtonFont(*button, button->getHeight());
        const float need =
            juce::GlyphArrangement::getStringWidth(labelFont, button->getButtonText()) + 6.0f +
            (button == &draw ? (float)ShapeDrawPenButton::kIconSize + ShapeDrawPenButton::kIconGap : 0.0f);
        EXPECT_GE((float)button->getWidth(), need) << button->getButtonText() << " is wider than its box";
        EXPECT_FALSE(button->getBounds().intersects(editor.getDeleteButton().getBounds()) &&
                     button != &editor.getDeleteButton())
            << button->getButtonText() << " runs into Delete";
    }
}

} // namespace

TEST(ShapeDrawGesture, TheDrawButtonCentresItsIconAndWordAsOneGroup) {
    {
        LfoRig rig;
        ShapeListEditor editor(rig.adapter, 0);
        expectDrawButtonCentredAndRowUnclipped(editor);
    }
    ShapeRig rig; // the Filter has the pitch rows, so its row is the narrower Ring layout
    ShapeListEditor editor(rig.adapter, 0);
    ASSERT_NE(editor.getPitchRows(), nullptr);
    expectDrawButtonCentredAndRowUnclipped(editor);

    // At the tightest width the row can give it (wider fonts, as on Windows), the word still fits whole.
    auto& draw = editor.getDrawPenButton();
    const auto font = draw.getLookAndFeel().getTextButtonFont(draw, draw.getHeight());
    const float word = juce::GlyphArrangement::getStringWidth(font, "Draw");
    draw.setSize((int)std::ceil(word + (float)ShapeDrawPenButton::kIconSize + ShapeDrawPenButton::kIconGap +
                                2.0f * ShapeDrawPenButton::kMinSidePad),
                 draw.getHeight());
    EXPECT_GE(draw.contentLayout().text.getWidth(), word) << "the word is not clipped at the tightest width";
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

// Regression test for FRO2095: with the cap at 64 a dense stroke was thinned or cut short instead of kept whole.
TEST(ShapeDrawGesture, ADenseFreeStrokeOnAFilterShapeKeepsMoreThanSixtyFourPointsAsOneUndoStep) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Free);
    const int serial = rig.undo.getEditSerial();
    const auto before = pen.stored();

    pen.press(0.0, 0.5f);
    for (int i = 1; i <= 200; ++i)
        pen.drag(i / 200.0, i % 2 == 0 ? 0.3f : 0.7f);
    const auto result = pen.curve().getDrawGesture().result();
    pen.release();

    EXPECT_TRUE(result.refusal.isEmpty()) << result.refusal;
    EXPECT_GT((int)result.curve.points.size(), 64) << "every corner of a zigzag is kept";
    EXPECT_LE((int)result.curve.points.size(), LfoCustomWave::kMaxPoints);
    EXPECT_EQ(pen.stored(), result.curve) << "the preview is what the release commits";
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "one undo step";
    rig.undo.undo();
    EXPECT_EQ(pen.stored(), before);
}

TEST(ShapeDrawGesture, AnLfoTriangleOnAFineGridCommitsEveryCycleAboveSixtyFourPoints) {
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setGridName("1/64");
    editor.setPen(DrawShape::Triangle);
    const double wanted = 1.0 / editor.getCurveEditor().drawCellWidth();
    const int serial = rig.undo.getEditSerial();
    const auto before = pen.stored();

    pen.press(0.0, 0.5f);
    pen.drag(1.0, 0.5f);
    const auto result = pen.curve().getDrawGesture().result();
    pen.release();

    EXPECT_TRUE(result.refusal.isEmpty()) << result.refusal;
    EXPECT_NEAR(result.cycles, wanted, 0.5) << "one cycle per grid cell, none dropped";
    EXPECT_GT((int)result.curve.points.size(), 64);
    EXPECT_LE((int)result.curve.points.size(), LfoCustomWave::kMaxPoints);
    EXPECT_EQ(pen.stored(), result.curve) << "the preview is what the release commits";
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "one undo step";
    rig.undo.undo();
    EXPECT_EQ(pen.stored(), before);
}

// ---- the cycle count: sideways adds cycles at the remembered width, Option+up/down and [ ] set the count ----

namespace {
double drawnCycles(Pen& pen) { return pen.curve().getDrawGesture().result().cycles; }
} // namespace

TEST(ShapeDrawGesture, ASidewaysDragAddsCyclesAtTheRememberedWidthAndUpDownOnlySetsTheHeight) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Sine);
    pen.curve().getDrawGesture().setCycleWidth(0.1);

    pen.press(0.1, 0.5f);
    pen.drag(0.5, 0.5f);
    EXPECT_NEAR(drawnCycles(pen), 4.0, 1e-6) << "0.4 wide at 0.1 per cycle";
    pen.drag(0.9, 0.7f);
    EXPECT_NEAR(drawnCycles(pen), 8.0, 1e-6) << "sideways added cycles";
    pen.drag(0.9, 0.9f);
    EXPECT_NEAR(drawnCycles(pen), 8.0, 1e-6) << "up and down leave the count alone";
    pen.release();
}

TEST(ShapeDrawGesture, OptionDragUpOrDownPacksMoreOrFewerCyclesIntoTheSameWidthAndKeepsTheBox) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Sine);
    pen.curve().getDrawGesture().setCycleWidth(0.1);
    pen.press(0.1, 0.5f);
    pen.drag(0.5, 0.6f);
    ASSERT_NEAR(drawnCycles(pen), 4.0, 1e-6);

    pen.drag(0.5, 0.6f, false, true); // Option goes down here: nothing changes yet
    EXPECT_NEAR(drawnCycles(pen), 4.0, 1e-6);
    const auto from = pen.at(0.5, 0.6f);
    pen.dragTo({from.x + 40.0f, from.y - 3.0f * CurveDrawGesture::kCountStepPx}, true);
    EXPECT_NEAR(drawnCycles(pen), 7.0, 1e-6) << "three steps up: three more cycles in the same width";
    pen.release();
    EXPECT_NEAR(pen.curve().getDrawGesture().getCycleWidth().value(), 0.4 / 7.0, 1e-6) << "remembered";
}

TEST(ShapeDrawGesture, TheBracketKeysStepTheCountInADragAndBetweenStrokes) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Sine);
    auto& curve = pen.curve();
    curve.getDrawGesture().setCycleWidth(0.1);

    pen.press(0.1, 0.5f);
    pen.drag(0.5, 0.5f);
    EXPECT_TRUE(curve.keyPressed(juce::KeyPress(']')));
    EXPECT_NEAR(drawnCycles(pen), 5.0, 1e-6);
    EXPECT_TRUE(curve.keyPressed(juce::KeyPress('[')));
    EXPECT_TRUE(curve.keyPressed(juce::KeyPress('[')));
    EXPECT_NEAR(drawnCycles(pen), 3.0, 1e-6);
    pen.release();

    EXPECT_TRUE(curve.keyPressed(juce::KeyPress(']'))) << "idle: steps the cycles across the full width";
    // The drag left 3 cycles in 0.4 (7.5 across the full width, read as 8): one more is 9.
    EXPECT_NEAR(1.0 / curve.getDrawGesture().getCycleWidth().value(), 9.0, 1e-6);
}

TEST(ShapeDrawGesture, WithAGridOnTheCellSetsTheCountAndTheKeysSayWhy) {
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setGridName("1/16");
    editor.setPen(DrawShape::Sine);
    juce::String note;
    pen.curve().getDrawGesture().onNote = [&note](const juce::String& n) { note = n; };

    EXPECT_TRUE(pen.curve().keyPressed(juce::KeyPress(']')));
    EXPECT_TRUE(note.contains("Grid Off"));
    EXPECT_FALSE(pen.curve().getDrawGesture().getCycleWidth().has_value()) << "nothing was remembered";
    pen.press(0.0, 0.5f);
    pen.drag(0.5, 0.5f);
    pen.drag(0.5, 0.2f, false, true);
    EXPECT_TRUE(pen.curve().getDrawGesture().chipText().contains("Grid")) << pen.curve().getDrawGesture().chipText();
    pen.release();
}

TEST(ShapeDrawGesture, TheRememberedWidthIsSavedPerKindAndReadByTheNextWindow) {
    synth::test::PersistedKeysGuard keys{juce::StringArray{shapewindowprefs::cycleWidthKey("filter")}};
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    shapewindowprefs::saveText(&props, shapewindowprefs::cycleWidthKey("filter"), {});
    ShapeRig rig;
    rig.adapter->setApplicationProperties(&props);
    {
        ShapeListEditor editor(rig.adapter, 0);
        EXPECT_FALSE(editor.getCurveEditor().getDrawGesture().getCycleWidth().has_value());
        editor.setPen(DrawShape::Sine);
        EXPECT_TRUE(editor.getCurveEditor().keyPressed(juce::KeyPress(']')));
    }
    ShapeListEditor again(rig.adapter, 0);
    ASSERT_TRUE(again.getCurveEditor().getDrawGesture().getCycleWidth().has_value());
    EXPECT_NEAR(1.0 / again.getCurveEditor().getDrawGesture().getCycleWidth().value(), 4.0, 1e-4);
}

// ---- the count never outruns what is drawn (a Sine thins its points to reach far more than 15 cycles) ----

namespace {

constexpr DrawShape kPeriodicPens[] = {DrawShape::Sine, DrawShape::Triangle, DrawShape::Saw, DrawShape::Square};

/** The most cycles the pen can draw over x0..x1 of the rig's curve. */
int mostFor(ShapeListEditor& editor, DrawShape shape, double x0, double x1) {
    ShapeDrawStroke s;
    s.shape = shape;
    s.x0 = x0;
    s.x1 = x1;
    s.lo = 0.5f;
    s.hi = 0.9f;
    return maxDrawableCycles(waveFromCurveModel(editor.getCurveEditor().getModel()), s);
}

/** Option goes down at the pointer's place; then each move puts the pointer `steps` count steps up (negative: down). */
struct OptionCount {
    OptionCount(Pen& p, double x, float level)
        : pen(p)
        , from(p.at(x, level)) {
        pen.dragTo(from, true);
    }
    void moveSteps(int steps) { pen.dragTo({from.x, from.y - (float)steps * CurveDrawGesture::kCountStepPx}, true); }
    Pen& pen;
    juce::Point<float> from;
};

} // namespace

TEST(ShapeDrawGesture, MostDrawableCyclesAreMeasuredPerPenAndASineGoesFarPastFifteen) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    for (const auto shape : kPeriodicPens) {
        const int most = mostFor(editor, shape, 0.0, 1.0);
        EXPECT_GE(most, 1);
        if (shape == DrawShape::Sine)
            EXPECT_GE(most, 31) << "a Sine thins to 8 then 4 points a cycle before it gives a count up";
    }
}

TEST(ShapeDrawGesture, TheDrawnCyclesAreTheCountAskedOrTheClampAndTheChipSaysWhichForEveryCountToTheMaximum) {
    for (const auto shape : kPeriodicPens) {
        for (const auto& range : {std::pair{0.0, 1.0}, std::pair{0.2, 0.7}, std::pair{0.3, 0.4}}) {
            ShapeRig rig;
            ShapeListEditor editor(rig.adapter, 0);
            Pen pen(editor, rig.undo, rig.adapter);
            editor.setPen(shape);
            const int most = mostFor(editor, shape, range.first, range.second);
            pen.curve().getDrawGesture().setCycleWidth(range.second - range.first);
            pen.press(range.first, 0.5f);
            pen.drag(range.second, 0.5f);
            OptionCount count(pen, range.second, 0.5f);
            const auto note = juce::String(drawShapeName(shape)) + " " + juce::String(range.first) + ".." +
                              juce::String(range.second);
            for (int n = 1; n <= CurveDrawGesture::kMaxCycles; ++n) {
                count.moveSteps(n - 1);
                const auto drawn = pen.curve().getDrawGesture().result();
                const int expected = juce::jmin(n, most);
                ASSERT_TRUE(drawn.changed) << note << " asked " << n;
                ASSERT_NEAR(drawn.cycles, (double)expected, 1e-6) << note << " asked " << n << " most " << most;
                const auto chip = pen.curve().getDrawGesture().chipText();
                ASSERT_EQ(chip.contains("most that fit"), n > most) << note << " asked " << n << ": " << chip;
                ASSERT_TRUE(chip.startsWith(juce::String(expected))) << note << ": " << chip;
            }
            if (most < CurveDrawGesture::kMaxCycles) { // the limit is real: one more would not be drawn as that many
                ShapeDrawStroke s;
                s.shape = shape;
                s.x0 = range.first;
                s.x1 = range.second;
                s.cycleWidth = (range.second - range.first) / (double)(most + 1);
                const auto over = drawOnCurve(waveFromCurveModel(editor.getCurveEditor().getModel()), s);
                EXPECT_FALSE(over.changed && std::abs(over.cycles - (most + 1)) < 1e-6) << note;
            }
            pen.release();
        }
    }
}

TEST(ShapeDrawGesture, EveryBracketStepBelowTheLimitChangesTheCurveAndStepsBackReturnToTheStart) {
    for (const auto shape : kPeriodicPens) {
        ShapeRig rig;
        ShapeListEditor editor(rig.adapter, 0);
        Pen pen(editor, rig.undo, rig.adapter);
        auto& curve = pen.curve();
        editor.setPen(shape);
        curve.getDrawGesture().setCycleWidth(1.0 / 3.0);
        pen.press(0.0, 0.5f);
        pen.drag(1.0, 0.5f);
        const auto name = juce::String(drawShapeName(shape));
        const double start = drawnCycles(pen);
        ASSERT_NEAR(start, 3.0, 1e-6) << name;

        int ups = 0;
        for (int i = 0; i < CurveDrawGesture::kMaxCycles + 5; ++i) {
            const auto before = curve.getDrawGesture().result().curve;
            const double was = drawnCycles(pen);
            ASSERT_TRUE(curve.keyPressed(juce::KeyPress(']')));
            if (drawnCycles(pen) <= was)
                break;
            ++ups;
            ASSERT_NE(curve.getDrawGesture().result().curve, before) << name << " step " << i;
        }
        const double densest = drawnCycles(pen);
        EXPECT_GT(densest, start) << name;
        EXPECT_TRUE(curve.getDrawGesture().chipText().contains("most that fit")) << name;
        EXPECT_NEAR(densest, start + ups, 1e-6) << name << ": one cycle a step, no hidden step";

        for (int i = 0; i < ups; ++i) {
            const auto before = curve.getDrawGesture().result().curve;
            const double was = drawnCycles(pen);
            ASSERT_TRUE(curve.keyPressed(juce::KeyPress('[')));
            ASSERT_NEAR(drawnCycles(pen), was - 1.0, 1e-6) << name << " (the first [ after the densest lowers it)";
            ASSERT_NE(curve.getDrawGesture().result().curve, before) << name << " step " << i;
        }
        EXPECT_NEAR(drawnCycles(pen), start, 1e-6) << name;
        pen.release();
    }
}

TEST(ShapeDrawGesture, OptionDraggingUpPastTheMaximumThenDownOneStepLowersTheCountAtOnce) {
    for (const auto shape : kPeriodicPens) {
        ShapeRig rig;
        ShapeListEditor editor(rig.adapter, 0);
        Pen pen(editor, rig.undo, rig.adapter);
        editor.setPen(shape);
        const int most = mostFor(editor, shape, 0.0, 1.0);
        pen.curve().getDrawGesture().setCycleWidth(1.0 / 3.0);
        pen.press(0.0, 0.5f);
        pen.drag(1.0, 0.5f);
        OptionCount count(pen, 1.0, 0.5f);
        const auto name = juce::String(drawShapeName(shape));

        count.moveSteps(most + 20);
        ASSERT_NEAR(drawnCycles(pen), (double)most, 1e-6) << name;
        EXPECT_TRUE(pen.curve().getDrawGesture().chipText().contains("most that fit")) << name;
        count.moveSteps(most + 19);
        EXPECT_NEAR(drawnCycles(pen), (double)most - 1.0, 1e-6) << name << ": one step back lowers the count at once";
        EXPECT_FALSE(pen.curve().getDrawGesture().chipText().contains("most that fit")) << name;
        count.moveSteps(most + 20);
        EXPECT_NEAR(drawnCycles(pen), (double)most, 1e-6) << name << ": and one step up is the maximum again";
        pen.release();
    }
}

TEST(ShapeDrawGesture, ASineBeyondFifteenCyclesIsOneUndoStepAndThePreviewIsTheCommitAtTheLimit) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    Pen pen(editor, rig.undo, rig.adapter);
    editor.setPen(DrawShape::Sine);
    const auto before = pen.stored();
    const int serial = rig.undo.getEditSerial();
    pen.curve().getDrawGesture().setCycleWidth(1.0 / 3.0);

    pen.press(0.0, 0.5f);
    pen.drag(1.0, 0.5f);
    OptionCount count(pen, 1.0, 0.5f);
    count.moveSteps(100);
    const auto preview = pen.curve().getDrawGesture().result();
    ASSERT_GE(preview.cycles, 31.0);
    EXPECT_LE((int)preview.curve.points.size(), LfoCustomWave::kMaxPoints);
    pen.release();

    EXPECT_EQ(pen.stored(), preview.curve) << "the preview is what the release commits";
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "one undo step";
    rig.undo.undo();
    EXPECT_EQ(pen.stored(), before);
}
