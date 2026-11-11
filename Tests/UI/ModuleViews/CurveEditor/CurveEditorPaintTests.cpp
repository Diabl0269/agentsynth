// CurveEditorPaintTests.cpp
// Paint smoke tests for synth::ui::CurveEditorComponent: renders under the real app
// LookAndFeel and asserts the curve actually shows up (accent-coloured pixels along the drawn
// path, background elsewhere), plus an opt-in PNG dump of an envelope-shaped curve for visual
// inspection (same pattern as ADSR_CARD_PNG in
// Tests/UI/Graph/ModuleComponent/ModuleComponentLayoutTests.cpp).

#include "../../Graph/ModuleComponent/ModuleComponentTestFixture.h"
#include "../ModCurve/ModCurveTestRig.h"
#include "../ShapeEditor/ShapeEditorTestHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "CurveEditorTestHelpers.h"
#include "Modules/ADSRModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include "UI/ModuleViews/ModCurve/ModCurveWindow.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr int kWidth = 400;
constexpr int kHeight = 200;

bool colourClose(juce::Colour a, juce::Colour b, int tolerance) {
    return std::abs((int)a.getRed() - (int)b.getRed()) <= tolerance &&
           std::abs((int)a.getGreen() - (int)b.getGreen()) <= tolerance &&
           std::abs((int)a.getBlue() - (int)b.getBlue()) <= tolerance;
}

bool hasOpaquePixel(const juce::Image& img) {
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                return true;
    return false;
}

} // namespace

TEST(CurveEditorPaintTest, PaintDrawsCurveInAccentColourOverBackgroundElsewhere) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    EnvelopeShape shape;
    shape.decayBend = 0.4f;
    comp.setModel(buildEnvelopeModel(shape));

    synth::theme::AppLookAndFeel lf;
    comp.setLookAndFeel(&lf);

    // SoftwareImageType(): on Windows the default (native) image type is Direct2D-backed, and
    // painting into it then reading pixels back on a GPU-less CI runner yields an all-zero image
    // Force a software-backed bitmap so getPixelAt() reads what paint() actually drew.
    juce::Image img(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));

    const auto& colors = lf.getTheme().colors;
    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());

    // Find a pixel column clear of BOTH the curve/fill (right of the last node) and any grid
    // tick, so it can only be background -- more robust than guessing a spot by eye, since the
    // curve's own shape (and the "nice" grid step) both depend on the model's exact durations.
    const float lastNodeX = geometry.nodePosition(comp.getModel().getNumNodes() - 1).x;
    const auto ticks = geometry.computeGridTicks();
    int safeX = -1;
    for (int x = kWidth - 1; x > (int)lastNodeX + 5; --x) {
        bool nearTick = false;
        for (double t : ticks)
            if (std::abs(geometry.xForTime(t) - (float)x) < 3.0f) {
                nearTick = true;
                break;
            }
        if (!nearTick) {
            safeX = x;
            break;
        }
    }
    ASSERT_GE(safeX, 0) << "could not find a pixel column clear of both the curve and the grid";
    EXPECT_EQ(img.getPixelAt(safeX, 5), colors.bg1);

    // Sample the same polyline CurveEditorPaint.cpp strokes and require at least one sampled
    // pixel to read back close to the accent colour.
    bool foundAccentPixel = false;
    for (int seg = 0; seg < comp.getModel().getNumSegments() && !foundAccentPixel; ++seg) {
        const auto segStart = geometry.nodePosition(seg);
        const auto segEnd = geometry.nodePosition(seg + 1);
        for (int step = 0; step <= 24 && !foundAccentPixel; ++step) {
            const float progress = (float)step / 24.0f;
            const float x = segStart.x + (segEnd.x - segStart.x) * progress;
            const float y = geometry.yForLevel(comp.getModel().valueAt(seg, progress));
            const int px = juce::jlimit(0, kWidth - 1, (int)x);
            const int py = juce::jlimit(0, kHeight - 1, (int)y);
            if (colourClose(img.getPixelAt(px, py), colors.accent, 40))
                foundAccentPixel = true;
        }
    }
    EXPECT_TRUE(foundAccentPixel) << "expected at least one accent-coloured pixel along the drawn curve";

    comp.setLookAndFeel(nullptr);
}

TEST(CurveEditorPaintTest, PaintOnAnEmptyOrTinyComponentDoesNotThrow) {
    CurveEditorComponent comp;
    comp.setModel(buildEnvelopeModel());
    comp.setSize(0, 0);
    juce::Image img(juce::Image::ARGB, 1, 1, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
}

TEST(CurveEditorPaintTest, EnvelopeShapedCurveRendersToPngForVisualInspection) {
    CurveEditorComponent comp;
    comp.setSize(600, 220);

    EnvelopeShape shape;
    shape.attack = 0.005;
    shape.hold = 0.0;
    shape.decay = 0.300;
    shape.sustain = 0.4f;
    shape.release = 0.500;
    shape.attackBend = -0.3f;
    shape.decayBend = 0.65f;
    shape.releaseBend = 0.65f;
    comp.setModel(buildEnvelopeModel(shape));
    comp.setPlayhead(CurvePlayhead{kDecaySeg, 0.4f});

    synth::theme::AppLookAndFeel lf;
    comp.setLookAndFeel(&lf);

    juce::Image img(juce::Image::ARGB, comp.getWidth(), comp.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
    EXPECT_TRUE(hasOpaquePixel(img));

    comp.setLookAndFeel(nullptr);

    const char* pngPath = std::getenv("CURVE_EDITOR_SNAPSHOT");
    if (pngPath == nullptr || juce::String(pngPath).isEmpty())
        GTEST_SKIP() << "set CURVE_EDITOR_SNAPSHOT=<path> to write the rendered curve for visual inspection";

    juce::File outFile(pngPath);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    juce::FileOutputStream stream(outFile);
    ASSERT_TRUE(stream.openedOk()) << "failed to open " << pngPath << " for writing";
    juce::PNGImageFormat png;
    ASSERT_TRUE(png.writeImageToStream(img, stream)) << "failed to encode PNG to " << pngPath;
}

TEST(CurveEditorPaintTest, PlayheadNulloptHidesTheMarker) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());
    EXPECT_FALSE(comp.getPlayhead().has_value());

    comp.setPlayhead(CurvePlayhead{kDecaySeg, 0.5f});
    EXPECT_TRUE(comp.getPlayhead().has_value());

    comp.setPlayhead(std::nullopt);
    EXPECT_FALSE(comp.getPlayhead().has_value());
}

// Regression test for FRO1445: every curve editor draws its first and last dot whole and the shape edge to edge. One
// guard over every user of CurveEditorComponent: the envelope card, the LFO card and its morph card, the shape window
// (Filter and LFO) and the modulation curve box and window. A user missing from `registeredUsers()` fails the scan
// below, so a new editor cannot join without a dot check.
using synth::ui::CurveEditorComponent;
using synth::ui::CurveEditorGeometry;

namespace {

constexpr float kDotExtent = CurveEditorGeometry::kNodeMaxExtentPx;

/** The line as drawn runs from the first node to the last; each dot, at its widest, must lie inside the component. */
void expectEndDotsInside(CurveEditorComponent& editor, const juce::String& what) {
    ASSERT_GT(editor.getWidth(), 40) << what;
    const auto line = editor.getDrawnCurvePointsForTest();
    ASSERT_GE(line.size(), 2u) << what;
    EXPECT_GE(line.front().x - kDotExtent, 0.0f) << what << ": the first dot is cut by the left edge";
    EXPECT_LE(line.back().x + kDotExtent, (float)editor.getWidth())
        << what << ": the last dot is cut by the right edge";
}

/** A free-mode editor's shape spans the box: its ends sit at the insets and nowhere short of them. */
void expectSpansTheBox(CurveEditorComponent& editor, const juce::String& what) {
    const auto line = editor.getDrawnCurvePointsForTest();
    ASSERT_GE(line.size(), 2u) << what;
    EXPECT_NEAR(line.front().x, CurveEditorGeometry::kNodeInsetPx, 0.51f) << what;
    EXPECT_NEAR(line.back().x, (float)editor.getWidth() - CurveEditorGeometry::kNodeInsetPx, 0.51f)
        << what << ": the shape stops short of the right edge";
}

/** The ring of the first dot is painted at its left extreme: not clipped away by the edge. */
void expectFirstDotPaintedWhole(CurveEditorComponent& editor, const juce::String& what) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), false, 1.0f);
    const auto first = editor.getDrawnCurvePointsForTest().front();
    const auto ringLeft =
        image.getPixelAt((int)std::floor(first.x - CurveEditorGeometry::kNodeRadiusPx), (int)std::round(first.y));
    const auto beyond = image.getPixelAt(0, (int)std::round(first.y));
    EXPECT_NE(ringLeft, beyond) << what << ": no ring pixel where the first dot's left edge should be";
}

std::set<juce::String> registeredUsers() {
    return {"Graph/CardBody/CardBodyViews.cpp", "Graph/ModuleComponent/ModuleComponentLfoCard.cpp",
            "Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h", "ModuleViews/ModCurve/ModCurveEditorView.h",
            "ModuleViews/ShapeEditor/ShapeListEditor.h"};
}

} // namespace

TEST_F(ModuleComponentTest, EveryCurveEditorUserIsCoveredByTheEdgeDotGuard) {
    auto root = juce::File::getCurrentWorkingDirectory().getChildFile(__FILE__).getParentDirectory();
    while (root.getParentDirectory() != root && !root.getChildFile("Source/UI").isDirectory())
        root = root.getParentDirectory();
    const auto source = root.getChildFile("Source/UI");
    ASSERT_TRUE(source.isDirectory()) << source.getFullPathName();
    std::set<juce::String> found;
    for (const auto& file : source.findChildFiles(juce::File::findFiles, true, "*.h;*.cpp")) {
        const auto text = file.loadFileAsString();
        const bool owns = text.contains("CurveEditorComponent curve") ||
                          text.contains("CurveEditorComponent editor_") ||
                          text.contains("make_unique<synth::ui::CurveEditorComponent>") ||
                          text.contains("make_unique<CurveEditorComponent>");
        if (owns)
            found.insert(file.getRelativePathFrom(source).replaceCharacter('\\', '/'));
    }
    EXPECT_FALSE(found.empty());
    for (const auto& path : found)
        EXPECT_TRUE(registeredUsers().count(path) > 0)
            << path << " owns a curve editor but the edge-dot guard does not cover it: add a case below";
}

TEST_F(ModuleComponentTest, ModCurveBoxAndWindowDrawBothEndDotsWholeAndSpanTheBox) {
    BoxRig box;
    box.box->setSize(320, 260);
    expectEndDotsInside(box.editor(), "mod curve box");
    expectSpansTheBox(box.editor(), "mod curve box");
    expectFirstDotPaintedWhole(box.editor(), "mod curve box");

    synth::ui::ModCurveWindowContent window(box.target);
    window.setSize(520, 440);
    expectEndDotsInside(window.view().editor(), "mod curve window");
    expectSpansTheBox(window.view().editor(), "mod curve window");
}

TEST_F(ModuleComponentTest, ShapeWindowsDrawBothEndDotsWhole) {
    synth::ui::test::ShapeRig filter;
    synth::ui::ShapeListEditor filterEditor(filter.adapter, 0);
    filterEditor.setSize(720, 460);
    expectEndDotsInside(filterEditor.getCurveEditor(), "filter shape window");
    expectFirstDotPaintedWhole(filterEditor.getCurveEditor(), "filter shape window");

    AudioEngine engine;
    auto* lfo = new LFOModule();
    const auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    AppUndoManager undo;
    auto adapter = std::make_shared<synth::ui::LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
    synth::ui::ShapeListEditor lfoEditor(adapter, 0);
    lfoEditor.setSize(720, 460);
    expectEndDotsInside(lfoEditor.getCurveEditor(), "LFO shape window");
    expectSpansTheBox(lfoEditor.getCurveEditor(), "LFO shape window");
}

TEST_F(ModuleComponentTest, LfoAndEnvelopeCardsDrawBothEndDotsWhole) {
    AudioEngine engine;
    GraphEditor editor(engine);
    {
        LFOModule lfo;
        ModuleComponent card(&lfo, juce::AudioProcessorGraph::NodeID(1), editor);
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&lfo, "shape")) = LFOModule::kCustomShapeIndex;
        card.timerCallback();
        CurveEditorComponent* curve = nullptr;
        for (auto* child : card.getChildren())
            if (auto* c = dynamic_cast<CurveEditorComponent*>(child))
                curve = c;
        ASSERT_NE(curve, nullptr);
        expectEndDotsInside(*curve, "LFO card");
        expectSpansTheBox(*curve, "LFO card");
        expectFirstDotPaintedWhole(*curve, "LFO card");
    }
    {
        ADSRModule adsr;
        ModuleComponent card(&adsr, juce::AudioProcessorGraph::NodeID(2), editor);
        juce::ToggleButton* toggle = nullptr;
        for (auto* child : card.getChildren())
            if (auto* t = dynamic_cast<juce::ToggleButton*>(child))
                if (t->getButtonText() == "Show Envelope Graph")
                    toggle = t;
        ASSERT_NE(toggle, nullptr);
        toggle->setToggleState(true, juce::sendNotificationSync);
        CurveEditorComponent* curve = nullptr;
        for (auto* child : card.getChildren())
            if (auto* c = dynamic_cast<CurveEditorComponent*>(child))
                curve = c;
        ASSERT_NE(curve, nullptr);
        expectEndDotsInside(*curve, "envelope card");
        expectFirstDotPaintedWhole(*curve, "envelope card");
    }
}

// Regression test for FRO1447: an undo or redo glides the whole curve, the line between the dots as well as the dots.
// One table of every undoable property of a curve (a dot's time, a dot's level, a segment's bend, the number of dots):
// each restore is drawn at the old value on the first frame, strictly between old and new a little later, and settled
// at the new one; Reduce Motion lands it and fades in, Animations Off lands it at once.
using synth::LfoCustomWave;
using synth::ui::CurveEditorComponent;
using synth::ui::CurveEditorGeometry;
using synth::ui::CurveGeometryConfig;

namespace {

struct FullMotion {
    FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        synth::ui::setReducedMotionForTest(false);
    }
    ~FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

LfoCustomWave wave(std::vector<LfoCustomWave::Point> points) {
    LfoCustomWave w;
    w.points = std::move(points);
    return w;
}

/** Three dots: low, high, low, with a bend on each segment. */
LfoCustomWave base() { return wave({{0.0f, 0.1f, 0.2f}, {0.5f, 0.9f, -0.3f}, {1.0f, 0.2f, 0.0f}}); }

float lineY(const std::vector<juce::Point<float>>& line, float x) {
    for (size_t i = 1; i < line.size(); ++i)
        if (line[i].x >= x) {
            const float span = line[i].x - line[i - 1].x;
            return span > 0.0f ? line[i - 1].y + (line[i].y - line[i - 1].y) * (x - line[i - 1].x) / span : line[i].y;
        }
    return line.back().y;
}

struct Property {
    const char* name;
    LfoCustomWave before;
    LfoCustomWave after;
    std::function<double(CurveEditorComponent&)> drawn;
};

double drawnNodeX(CurveEditorComponent& ed, int node) {
    CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    const CurveEditorGeometry g(ed.getModel(), ed.getLocalBounds().toFloat(), config);
    return g.nodePosition(node).x + ed.getPointTweenForTest().offsetFor(node).x;
}

double drawnNodeY(CurveEditorComponent& ed, int node) {
    CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    const CurveEditorGeometry g(ed.getModel(), ed.getLocalBounds().toFloat(), config);
    return g.nodePosition(node).y + ed.getPointTweenForTest().offsetFor(node).y;
}

std::vector<Property> properties() {
    auto moveX = base();
    moveX.points[1].x = 0.7f;
    auto moveY = base();
    moveY.points[1].y = 0.3f;
    auto bend = base();
    bend.points[0].bend = 0.8f;
    auto added = base();
    added.points.insert(added.points.begin() + 1, {0.25f, 0.95f, 0.0f});
    return {
        {"a dot's time", base(), moveX, [](CurveEditorComponent& e) { return drawnNodeX(e, 1); }},
        {"a dot's level", base(), moveY, [](CurveEditorComponent& e) { return drawnNodeY(e, 1); }},
        {"a segment's bend", base(), bend, [](CurveEditorComponent& e) { return (double)e.getDrawnBendForTest(0); }},
        {"the number of dots", base(), added,
         [](CurveEditorComponent& e) { return (double)lineY(e.getDrawnCurvePointsForTest(), 0.25f * e.getWidth()); }},
    };
}

struct Rig {
    CurveEditorComponent ed;
    Rig(const LfoCustomWave& start) {
        ed.setSize(300, 200);
        ed.setVisibleRangeOverride(1.0);
        ed.getPointTweenForTest().forceAnimateForTest(true);
        ed.getBendTweenForTest().forceAnimateForTest(true);
        ed.getMorphTweenForTest().forceAnimateForTest(true);
        ed.setModel(synth::ui::curveModelFromWave(start));
    }
    void restore(const LfoCustomWave& w) { ed.setModel(synth::ui::curveModelFromWave(w), true); }
    void step(double ms) {
        ed.getPointTweenForTest().stepForTest(ms);
        ed.getBendTweenForTest().stepForTest(ms);
        ed.getMorphTweenForTest().stepForTest(ms);
    }
    bool gliding() {
        return ed.getPointTweenForTest().isAnimating() || ed.getBendTweenForTest().isAnimating() ||
               ed.getMorphTweenForTest().isAnimating();
    }
};

} // namespace

TEST(CurveEditorRestoreGlide, EveryUndoableCurvePropertyGlidesOnUndoAndRedo) {
    for (const auto& property : properties()) {
        SCOPED_TRACE(property.name);
        FullMotion motion;
        Rig rig(property.before);
        const double old = property.drawn(rig.ed);
        rig.restore(property.after);
        rig.ed.getPointTweenForTest().completeForTest();
        rig.ed.getBendTweenForTest().completeForTest();
        rig.ed.getMorphTweenForTest().completeForTest();
        const double now = property.drawn(rig.ed);
        ASSERT_GT(std::abs(now - old), 1e-3) << "the edit changed the property";

        // Redo from the old shape, as an undo would run: from `before` back to `after` mid-glide.
        rig.restore(property.before);
        EXPECT_TRUE(rig.gliding());
        EXPECT_NEAR(property.drawn(rig.ed), now, std::abs(now - old) * 0.02 + 1e-3) << "first frame at the old place";
        rig.step(70.0);
        const double mid = property.drawn(rig.ed);
        EXPECT_GT(std::abs(mid - now), 1e-4) << "it has left the old place";
        EXPECT_LT(std::abs(mid - old), std::abs(now - old)) << "and is on its way to the new one";
        EXPECT_TRUE((mid > std::min(old, now) - 1e-6) && (mid < std::max(old, now) + 1e-6)) << "between old and new";
        rig.step(1000.0);
        EXPECT_FALSE(rig.gliding());
        EXPECT_NEAR(property.drawn(rig.ed), old, 1e-3);
    }
}

TEST(CurveEditorRestoreGlide, ABendGlidesBetweenTheOldAndTheNewBendAndTheLineFollowsIt) {
    FullMotion motion;
    auto bent = base();
    bent.points[0].bend = 0.8f;
    Rig rig(base());
    rig.restore(bent);
    EXPECT_NEAR(rig.ed.getDrawnBendForTest(0), 0.2f, 1e-4f);
    const float lineBefore = lineY(rig.ed.getDrawnCurvePointsForTest(), 0.25f * 300.0f);
    rig.step(70.0);
    const float mid = rig.ed.getDrawnBendForTest(0);
    EXPECT_GT(mid, 0.2f);
    EXPECT_LT(mid, 0.8f);
    EXPECT_NE(lineY(rig.ed.getDrawnCurvePointsForTest(), 0.25f * 300.0f), lineBefore) << "the line itself moved";
    rig.step(1000.0);
    EXPECT_NEAR(rig.ed.getDrawnBendForTest(0), 0.8f, 1e-4f);
}

TEST(CurveEditorRestoreGlide, UnderReduceMotionABendLandsAtOnceAndAnimationsOffLandsPlainly) {
    FullMotion motion;
    auto bent = base();
    bent.points[0].bend = 0.8f;
    {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
        synth::ui::setReducedMotionForTest(true);
        Rig rig(base());
        rig.restore(bent);
        EXPECT_NEAR(rig.ed.getDrawnBendForTest(0), 0.8f, 1e-4f) << "no glide under Reduce Motion";
    }
    {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
        Rig rig(base());
        rig.restore(bent);
        EXPECT_NEAR(rig.ed.getDrawnBendForTest(0), 0.8f, 1e-4f);
        EXPECT_FALSE(rig.gliding());
    }
}
