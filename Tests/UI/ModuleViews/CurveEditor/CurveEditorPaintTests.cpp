// CurveEditorPaintTests.cpp
// Paint smoke tests for synth::ui::CurveEditorComponent (FRO111): renders under the real app
// LookAndFeel and asserts the curve actually shows up (accent-coloured pixels along the drawn
// path, background elsewhere), plus an opt-in PNG dump of an envelope-shaped curve for visual
// inspection (same pattern as ADSR_CARD_PNG in
// Tests/UI/Graph/ModuleComponent/ModuleComponentLayoutTests.cpp).

#include "CurveEditorTestHelpers.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>
#include <cstdlib>
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

} // namespace

TEST(CurveEditorPaintTest, PaintDrawsCurveInAccentColourOverBackgroundElsewhere) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    EnvelopeShape shape;
    shape.decayBend = 0.4f;
    comp.setModel(buildEnvelopeModel(shape));

    synth::theme::AppLookAndFeel lf;
    comp.setLookAndFeel(&lf);

    juce::Image img(juce::Image::ARGB, kWidth, kHeight, true);
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
    juce::Image img(juce::Image::ARGB, 1, 1, true);
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

    juce::Image img(juce::Image::ARGB, comp.getWidth(), comp.getHeight(), true);
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
