// CurveEditorPaintTests.cpp
// Paint smoke tests for synth::ui::CurveEditorComponent: renders under the real app
// LookAndFeel and asserts the curve actually shows up (accent-coloured pixels along the drawn
// path, background elsewhere), plus an opt-in PNG dump of an envelope-shaped curve for visual
// inspection (same pattern as ADSR_CARD_PNG in
// Tests/UI/Graph/ModuleComponent/ModuleComponentLayoutTests.cpp).

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "CurveEditorTestHelpers.h"
#include "Modules/ADSRModule.h"
#include "Modules/LFOModule.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

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

// ---- the focus ring's band (CurveEditorComponent::plotBounds) ----

// Pixels of `image` in the ring's band (the outer `band` px of every side) that are not `surface`.
int ringBandPixels(const juce::Image& image, juce::Colour surface, int band) {
    int count = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x) {
            const bool inBand = x < band || y < band || x >= image.getWidth() - band || y >= image.getHeight() - band;
            if (inBand && image.getPixelAt(x, y) != surface)
                ++count;
        }
    return count;
}

juce::Image renderEditor(CurveEditorComponent& comp) {
    juce::Image image(juce::Image::ARGB, comp.getWidth(), comp.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    comp.paintEntireComponent(g, false);
    return image;
}

// A free curve with a point at each corner of the plot, so a dot or the line at the far ends is drawn.
CurveModel ringCornerModel() {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes(4);
    nodes[0] = CurveNode{0.0, 0.0f, true, true};
    nodes[1] = CurveNode{0.3, 1.0f, true, true};
    nodes[2] = CurveNode{0.7, 0.0f, true, true};
    nodes[3] = CurveNode{1.0, 1.0f, true, true};
    model.setNodes(nodes);
    return model;
}

struct RingFixture {
    synth::theme::AppLookAndFeel lf;
    CurveEditorComponent comp;
    RingFixture() {
        comp.setLookAndFeel(&lf);
        comp.setSize(kWidth, kHeight);
    }
    ~RingFixture() { comp.setLookAndFeel(nullptr); }
    int band() const { return (int)std::floor(focusRingInset(synth::theme::themeOf(comp))); }
    juce::Colour surface() const { return synth::theme::themeOf(comp).colors.bg1; }
};
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
    const CurveEditorGeometry geometry(comp.getModel(), comp.plotBounds());

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
    const CurveEditorGeometry g(ed.getModel(), ed.plotBounds(), config);
    return g.nodePosition(node).x + ed.getPointTweenForTest().offsetFor(node).x;
}

double drawnNodeY(CurveEditorComponent& ed, int node) {
    CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    const CurveEditorGeometry g(ed.getModel(), ed.plotBounds(), config);
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

namespace {

CurveModel freeLine(const std::vector<std::pair<double, float>>& nodes) {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> built;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const bool end = i == 0 || i + 1 == nodes.size();
        built.push_back(CurveNode{nodes[i].first, nodes[i].second, !end, true});
    }
    model.setNodes(built);
    return model;
}

struct EditRig {
    CurveEditorComponent ed;
    explicit EditRig(CurveModel model) {
        ed.setSize(300, 200);
        ed.setVisibleRangeOverride(1.0);
        ed.getPointTweenForTest().forceAnimateForTest(true);
        ed.getBendTweenForTest().forceAnimateForTest(true);
        ed.getMorphTweenForTest().forceAnimateForTest(true);
        ed.setModel(std::move(model));
    }
    float weight() { return ed.getMorphTweenForTest().offsetFor(0).y; }
};

} // namespace

// Regression test for FRO1991: every dot and bend handle vanished while the line eased after an add or a delete.
TEST(CurveEditorEditMorph, ADeleteKeepsEverySurvivingDotAndBendHandleDrawnWhileTheLineEases) {
    FullMotion motion;
    EditRig rig(freeLine({{0.0, 0.5f}, {0.25, 0.9f}, {0.5, 0.1f}, {0.75, 0.8f}, {1.0, 0.5f}}));
    ASSERT_TRUE(rig.ed.removeNode(2));
    ASSERT_GT(rig.weight(), 0.9f) << "the morph has begun";
    for (const double ms : {0.0, 40.0, 90.0}) {
        rig.ed.getMorphTweenForTest().stepForTest(ms);
        for (int i = 0; i < rig.ed.getModel().getNumNodes(); ++i)
            EXPECT_FLOAT_EQ(rig.ed.getDotMorphFadeForTest(i), 1.0f) << "dot " << i << " at " << ms << " ms";
        EXPECT_FLOAT_EQ(rig.ed.getBendMorphFadeForTest(0), 1.0f) << "a bend that did not change";
        EXPECT_FLOAT_EQ(rig.ed.getBendMorphFadeForTest(2), 1.0f) << "a bend that did not change";
    }
    rig.ed.getMorphTweenForTest().stepForTest(0.0);
    EXPECT_LT(rig.ed.getBendMorphFadeForTest(1), 0.2f) << "the merged segment's handle fades in";
}

TEST(CurveEditorEditMorph, AnAddedDotFadesInWhileTheOthersStayAndItsUndoAndRedoKeepThemSteadyToo) {
    FullMotion motion;
    const auto three = freeLine({{0.0, 0.5f}, {0.5, 0.9f}, {1.0, 0.5f}});
    EditRig rig(three);
    const auto geometry = rig.ed.getGeometryForTest();
    const int added = rig.ed.addPointAt(geometry.nodePosition(0) + juce::Point<float>(75.0f, 60.0f), true);
    ASSERT_GE(added, 0);
    const auto four = rig.ed.getModel();
    rig.ed.getMorphTweenForTest().stepForTest(0.0);
    for (int i = 0; i < rig.ed.getModel().getNumNodes(); ++i) {
        if (i == added)
            EXPECT_LT(rig.ed.getDotMorphFadeForTest(i), 0.1f) << "the new dot starts hidden";
        else
            EXPECT_FLOAT_EQ(rig.ed.getDotMorphFadeForTest(i), 1.0f) << "dot " << i;
    }
    rig.ed.getMorphTweenForTest().completeForTest();
    EXPECT_FLOAT_EQ(rig.ed.getDotMorphFadeForTest(added), 1.0f);

    // Undo of the add (one dot fewer) and its redo (one dot more) keep every dot that exists on both sides at fade 1.0
    // on every frame; only the dot that comes or goes, and the handle of the segment that merges or splits, fade.
    rig.ed.setModel(three, true);
    ASSERT_GT(rig.weight(), 0.9f) << "the morph has begun";
    for (const double ms : {0.0, 40.0, 90.0}) {
        rig.ed.getMorphTweenForTest().stepForTest(ms);
        for (int i = 0; i < 3; ++i)
            EXPECT_FLOAT_EQ(rig.ed.getDotMorphFadeForTest(i), 1.0f) << "undo: dot " << i << " at " << ms << " ms";
        EXPECT_FLOAT_EQ(rig.ed.getBendMorphFadeForTest(1), 1.0f) << "undo: the bend of the segment that stays";
    }
    rig.ed.getMorphTweenForTest().completeForTest();

    rig.ed.setModel(four, true);
    ASSERT_GT(rig.weight(), 0.9f);
    for (const double ms : {0.0, 40.0, 90.0}) {
        rig.ed.getMorphTweenForTest().stepForTest(ms);
        for (int i = 0; i < 4; ++i) {
            if (i == added)
                continue;
            EXPECT_FLOAT_EQ(rig.ed.getDotMorphFadeForTest(i), 1.0f) << "redo: dot " << i << " at " << ms << " ms";
        }
    }
    rig.ed.getMorphTweenForTest().stepForTest(0.0);
    EXPECT_LT(rig.ed.getDotMorphFadeForTest(added), 0.1f) << "redo: the added dot grows back in";
}

// Any other change of the point count (here three dots replaced by four others) keeps the hide-all morph.
TEST(CurveEditorEditMorph, AnUndoThatRewritesSeveralDotsStillHidesEveryDot) {
    FullMotion motion;
    EditRig rig(freeLine({{0.0, 0.5f}, {0.5, 0.9f}, {1.0, 0.5f}}));
    rig.ed.setModel(freeLine({{0.0, 0.2f}, {0.3, 0.9f}, {0.6, 0.1f}, {1.0, 0.7f}}), true);
    rig.ed.getMorphTweenForTest().stepForTest(0.0);
    for (int i = 0; i < rig.ed.getModel().getNumNodes(); ++i)
        EXPECT_LT(rig.ed.getDotMorphFadeForTest(i), 0.1f) << "dot " << i;
    // One dot more, but another dot also moved: not a plain add either.
    EditRig second(freeLine({{0.0, 0.5f}, {0.5, 0.9f}, {1.0, 0.5f}}));
    second.ed.setModel(freeLine({{0.0, 0.5f}, {0.25, 0.3f}, {0.5, 0.4f}, {1.0, 0.5f}}), true);
    second.ed.getMorphTweenForTest().stepForTest(0.0);
    EXPECT_LT(second.ed.getDotMorphFadeForTest(0), 0.1f);
}

// Regression test for FRO1989: deleting the middle of three dots at one x blended the two lines by x, which cannot tell
// the dots of a vertical stack apart, so the line jumped.
TEST(CurveEditorEditMorph, DeletingTheMiddleOfThreeStackedDotsRetractsTheLineWithoutAJump) {
    FullMotion motion;
    EditRig rig(freeLine({{0.0, 0.5f}, {0.5, 0.8f}, {0.5, 0.1f}, {0.5, 0.5f}, {1.0, 0.5f}}));
    const float oldDeepest = [&] {
        float deepest = 0.0f;
        for (const auto& p : rig.ed.getDrawnCurvePointsForTest())
            deepest = std::max(deepest, p.y);
        return deepest;
    }();
    const float span = rig.ed.getGeometryForTest().yForLevel(0.1f) - rig.ed.getGeometryForTest().yForLevel(0.8f);
    ASSERT_TRUE(rig.ed.removeNode(2));
    const float newDeepest = rig.ed.getGeometryForTest().yForLevel(0.5f);

    float previousDeepest = oldDeepest + 1.0f;
    for (const double ms : {20.0, 60.0, 120.0, 400.0}) {
        rig.ed.getMorphTweenForTest().stepForTest(ms);
        const auto line = rig.ed.getDrawnCurvePointsForTest();
        float deepest = 0.0f;
        for (size_t i = 0; i < line.size(); ++i) {
            ASSERT_FALSE(std::isnan(line[i].y));
            deepest = std::max(deepest, line[i].y);
            if (i > 0)
                EXPECT_LE(std::abs(line[i].y - line[i - 1].y), span + 1.0f) << "no jump at " << ms << " ms";
        }
        EXPECT_LE(deepest, previousDeepest + 0.01f) << "the former middle dot's reach only retracts";
        EXPECT_GE(deepest, newDeepest - 0.5f);
        EXPECT_LE(deepest, oldDeepest + 0.5f);
        previousDeepest = deepest;
    }
    EXPECT_NEAR(previousDeepest, newDeepest, 1.0f) << "it lands on the new line";
}

// A playhead step invalidates only the strips the marker leaves and takes, and the picture after it is the one a full
// repaint gives.
TEST(CurveEditorPaintTest, APlayheadStepRepaintsOnlyTheMarkerStripsAndLooksLikeAFullRepaint) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setVisible(true);
    EnvelopeShape shape;
    shape.decayBend = 0.4f;
    comp.setModel(buildEnvelopeModel(shape));
    synth::theme::AppLookAndFeel lf;
    comp.setLookAndFeel(&lf);
    auto* cache = new synth::ui::ZoomFrozenCachedImage(comp);
    comp.setCachedComponentImage(cache);

    auto renderThroughCache = [&] {
        juce::Image target(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType());
        juce::Graphics g(target);
        cache->paint(g);
        return target;
    };
    comp.setPlayhead(CurvePlayhead{1, 0.2f});
    renderThroughCache();
    ASSERT_EQ(cache->getStaleAreaForTest(), 0);

    const CurvePlayhead from{1, 0.2f};
    const CurvePlayhead to{1, 0.6f};
    const auto oldStrip = comp.playheadMarkerBounds(from);
    const auto newStrip = comp.playheadMarkerBounds(to);
    comp.setPlayhead(to);
    const int stale = cache->getStaleAreaForTest();
    EXPECT_GT(stale, 0);
    EXPECT_LE(stale, oldStrip.getWidth() * oldStrip.getHeight() + newStrip.getWidth() * newStrip.getHeight());
    EXPECT_LT(stale, kWidth * kHeight / 4);

    const auto stepped = renderThroughCache();
    cache->invalidateAll();
    const auto full = renderThroughCache();
    int differing = 0;
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
            if (!colourClose(stepped.getPixelAt(x, y), full.getPixelAt(x, y), 1))
                ++differing;
    EXPECT_EQ(differing, 0) << "within one level per channel: the clip edge rounds an antialiased pixel once more";
    comp.setCachedComponentImage(nullptr);
}

// The curve editor's focus ring frames the whole plot with a gap: whatever the editor draws (grid, labels, curve, fill,
// dots at both ends of the range, the playhead) ends inside the plot, so the band the ring owns (stroke plus the shared
// gap, focusRingInset) stays the plain surface colour. Every host (envelope card, shape editor, modulation curve) draws
// through this one paint; FocusRingSweepTests rule 5 measures the real hosts. The mouse path runs through real
// juce::MouseEvents at the plot's edges: a hit area still sits on what is drawn. Bug kind: focus-ring-box.
TEST(CurveEditorFocusRingInsetTest, ThePlotIsTheBoundsLessTheRingInset) {
    RingFixture f;
    const auto inset = std::floor(focusRingInset(synth::theme::themeOf(f.comp)));
    EXPECT_EQ(f.comp.plotBounds(), f.comp.getLocalBounds().toFloat().reduced(inset));
    const auto geometry = f.comp.getGeometryForTest();
    f.comp.setModel(ringCornerModel());
    for (int i = 0; i < f.comp.getModel().getNumNodes(); ++i) {
        const auto p = f.comp.getGeometryForTest().nodePosition(i);
        EXPECT_GE(p.x - CurveEditorGeometry::kNodeMaxExtentPx, f.comp.plotBounds().getX()) << i;
        EXPECT_LE(p.x + CurveEditorGeometry::kNodeMaxExtentPx, f.comp.plotBounds().getRight()) << i;
        EXPECT_GE(p.y - CurveEditorGeometry::kNodeMaxExtentPx, f.comp.plotBounds().getY()) << i;
        EXPECT_LE(p.y + CurveEditorGeometry::kNodeMaxExtentPx, f.comp.plotBounds().getBottom()) << i;
    }
    (void)geometry;
}

// The envelope card's plain time grid, with the line and fill drawn.
TEST(CurveEditorFocusRingInsetTest, EnvelopeCurveAndTimeGridEndInsideTheRing) {
    RingFixture f;
    f.comp.setModel(buildEnvelopeModel());
    EXPECT_EQ(ringBandPixels(renderEditor(f.comp), f.surface(), f.band()), 0);
}

// A selected dot and the bend handles, at the corners of a free curve.
TEST(CurveEditorFocusRingInsetTest, DotsAtTheCornersAndTheSelectionEndInsideTheRing) {
    RingFixture f;
    f.comp.setModel(ringCornerModel());
    f.comp.selectNodeRelative(3); // the last dot, at the plot's far corner
    EXPECT_EQ(ringBandPixels(renderEditor(f.comp), f.surface(), f.band()), 0);
}

// The LFO / modulation curve's fixed grid, plus a playhead and the baseline fill of a bipolar wave.
TEST(CurveEditorFocusRingInsetTest, FixedGridPlayheadAndBaselineEndInsideTheRing) {
    RingFixture f;
    f.comp.setModel(ringCornerModel());
    CurveEditorComponent::CurveGrid grid;
    grid.xDivisions = 8;
    grid.yDivisions = 4;
    f.comp.setGrid(grid);
    f.comp.setFillBaselineLevel(0.5f);
    f.comp.setPlayhead(CurvePlayhead{1, 0.5f});
    EXPECT_EQ(ringBandPixels(renderEditor(f.comp), f.surface(), f.band()), 0);
}

// The shape editor's labelled guides on both axes (the Filter shape editor) and explicit note lines with labels.
TEST(CurveEditorFocusRingInsetTest, LabelledGuidesAndExplicitLinesEndInsideTheRing) {
    RingFixture f;
    f.comp.setModel(ringCornerModel());
    CurveEditorComponent::AxisGuides guides;
    guides.x = {{0.0, "0", true}, {0.5, "500", false}, {1.0, "1k", false}};
    guides.y = {{0.0, "-12", false}, {0.5, "0", true}, {1.0, "+12", false}};
    f.comp.setAxisGuides(guides);
    EXPECT_EQ(ringBandPixels(renderEditor(f.comp), f.surface(), f.band()), 0);

    f.comp.setAxisGuides(std::nullopt);
    CurveEditorComponent::CurveGrid grid;
    for (int i = 0; i <= 8; ++i) {
        CurveEditorComponent::GridLine line;
        line.x = i / 8.0;
        line.tier =
            i % 4 == 0 ? CurveEditorComponent::GridLine::Tier::landmark : CurveEditorComponent::GridLine::Tier::snap;
        line.label = juce::String(i);
        grid.xLines.push_back(line);
    }
    f.comp.setGrid(grid);
    EXPECT_EQ(ringBandPixels(renderEditor(f.comp), f.surface(), f.band()), 0);
}

// The real mouse path at the plot's edges: a press on the dot at the plot's left and right ends picks it, and a drag
// past the plot's edge puts the dot at the plot's edge (never into the ring's band).
TEST(CurveEditorFocusRingInsetTest, PressesAndDragsAtThePlotEdgesStillHitTheDots) {
    RingFixture f;
    f.comp.setModel(ringCornerModel());
    const auto plot = f.comp.plotBounds();
    const auto first = f.comp.getGeometryForTest().nodePosition(0);
    const auto last = f.comp.getGeometryForTest().nodePosition(3);
    EXPECT_NEAR(first.x, plot.getX() + CurveEditorGeometry::kNodeInsetPx, 0.01f);
    EXPECT_NEAR(first.y, plot.getBottom() - CurveEditorGeometry::kLevelInsetPx, 0.01f);
    EXPECT_NEAR(last.y, plot.getY() + CurveEditorGeometry::kLevelInsetPx, 0.01f);

    // Pressing where the dot is drawn, then dragging it beyond every edge of the plot.
    f.comp.mouseDown(curveLeftClick(f.comp, first));
    f.comp.mouseDrag(curveLeftDrag(f.comp, {-40.0f, (float)kHeight + 40.0f}, first));
    f.comp.mouseUp(curveLeftDrag(f.comp, {-40.0f, (float)kHeight + 40.0f}, first));
    const auto movedFirst = f.comp.getGeometryForTest().nodePosition(0);
    EXPECT_GE(movedFirst.x, plot.getX());
    EXPECT_LE(movedFirst.y, plot.getBottom());

    f.comp.mouseDown(curveLeftClick(f.comp, last));
    f.comp.mouseDrag(curveLeftDrag(f.comp, {(float)kWidth + 40.0f, -40.0f}, last));
    f.comp.mouseUp(curveLeftDrag(f.comp, {(float)kWidth + 40.0f, -40.0f}, last));
    const auto movedLast = f.comp.getGeometryForTest().nodePosition(f.comp.getModel().getNumNodes() - 1);
    EXPECT_LE(movedLast.x, plot.getRight());
    EXPECT_GE(movedLast.y, plot.getY());

    // Neither end ever left the pixels the plot owns.
    EXPECT_EQ(ringBandPixels(renderEditor(f.comp), f.surface(), f.band()), 0);
}

// The hit area is where the dot is drawn: a press on the dot is a hit, a press in the ring's band beside it is not.
TEST(CurveEditorFocusRingInsetTest, TheHitAreaMatchesTheDrawnDot) {
    RingFixture f;
    f.comp.setModel(ringCornerModel());
    const auto geometry = f.comp.getGeometryForTest();
    const auto drawn = geometry.nodePosition(0);
    EXPECT_EQ(geometry.hitTest(drawn).kind, CurveHitKind::Node);
    const auto inBand = juce::Point<float>(1.0f, (float)kHeight - 1.0f);
    EXPECT_EQ(geometry.hitTest(inBand).kind, CurveHitKind::None);
}
