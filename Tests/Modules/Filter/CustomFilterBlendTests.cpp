// CustomFilterBlendTests.cpp -- the Custom blend as numbers (what the response view draws and "add a shape" copies),
// the preset curves, the default list's identity and the shape-list generation counter.
#include "Modules/Filter/CustomFilterBlend.h"
#include "Modules/FilterModule.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::customfilter;

TEST(CustomFilterBlend, AtAShapesPositionTheBlendIsThatShape) {
    const auto shapes = CustomFilterShapes::defaultShapes();
    const auto blend = blendedGridDb(shapes, 0.2f, 0.25f); // exactly on "ah"
    for (int i = 0; i < kGridSize; i += 64) {
        const float expected = CustomFilterShapes::yToDb(shapes.shapes[0].curve.evaluate((float)i / (kGridSize - 1)));
        EXPECT_NEAR(blend[(size_t)i], expected, 0.5f) << "grid point " << i;
    }
}

TEST(CustomFilterBlend, ResponseAppliesShiftAndDepthLikeTheKernelDesigner) {
    const auto shapes = CustomFilterShapes::defaultShapes();
    const auto plain = blendedGridDb(shapes, 0.3f, 0.4f);
    const auto doubled = responseGridDb(shapes, 0.3f, 0.4f, 0.0f, 2.0f);
    const auto flat = responseGridDb(shapes, 0.3f, 0.4f, 0.0f, 0.0f);
    const auto shifted = responseGridDb(shapes, 0.3f, 0.4f, 12.0f, 1.0f);
    for (int i : {100, 400, 800}) {
        EXPECT_NEAR(doubled[(size_t)i], plain[(size_t)i] * 2.0f, 1e-3);
        EXPECT_FLOAT_EQ(flat[(size_t)i], 0.0f);
    }
    // An octave up moves the response up in frequency: bin f reads the curve at f / 2.
    const int octave = (int)std::round((float)(kGridSize - 1) * std::log(2.0f) / std::log(1000.0f));
    EXPECT_NEAR(shifted[(size_t)(600 + octave)], plain[600], 0.3f);
}

TEST(CustomFilterBlend, CurveFromGridSamplesBackToTheRequestedPointCount) {
    std::vector<float> grid((size_t)kGridSize);
    for (int i = 0; i < kGridSize; ++i)
        grid[(size_t)i] = -24.0f + 24.0f * (float)i / (kGridSize - 1);
    const auto curve = curveFromGridDb(grid, 16);
    ASSERT_EQ(curve.points.size(), 16u);
    EXPECT_FLOAT_EQ(curve.points.front().x, 0.0f);
    EXPECT_FLOAT_EQ(curve.points.back().x, 1.0f);
    EXPECT_NEAR(CustomFilterShapes::yToDb(curve.points.front().y), -24.0f, 0.01f);
    EXPECT_NEAR(CustomFilterShapes::yToDb(curve.points.back().y), 0.0f, 0.01f);
    EXPECT_NEAR(CustomFilterShapes::yToDb(curve.evaluate(0.5f)), -12.0f, 0.5f);
}

TEST(CustomFilterBlend, CurveFromGridClampsIntoTheCurveRange) {
    std::vector<float> loud((size_t)kGridSize, 40.0f), quiet((size_t)kGridSize, -90.0f);
    for (const auto& p : curveFromGridDb(loud, 8).points)
        EXPECT_FLOAT_EQ(p.y, 1.0f);
    for (const auto& p : curveFromGridDb(quiet, 8).points)
        EXPECT_FLOAT_EQ(p.y, 0.0f);
}

TEST(CustomFilterShapesPresets, EightNamedCurvesAllDistinctAndUnderTheirGainCeiling) {
    const char* names[] = {"ah", "ee", "oo", "eh", "ih", "wolf", "crow", "frog"};
    for (int i = 0; i < CustomFilterShapes::kNumPresets; ++i) {
        const auto preset = static_cast<CustomFilterShapes::Preset>(i);
        EXPECT_STREQ(CustomFilterShapes::presetName(preset), names[i]);
        const auto curve = CustomFilterShapes::presetCurve(preset);
        float peak = -100.0f;
        for (const auto& p : curve.points)
            peak = std::max(peak, CustomFilterShapes::yToDb(p.y));
        // The vowels are loudness-matched and may reach the gain audit's +6 dB; the animal calls stay at +2 dB.
        EXPECT_LE(peak, i <= (int)CustomFilterShapes::Preset::Ih ? 6.001f : 2.001f) << names[i];
        for (int j = i + 1; j < CustomFilterShapes::kNumPresets; ++j)
            EXPECT_FALSE(curve == CustomFilterShapes::presetCurve(static_cast<CustomFilterShapes::Preset>(j)));
    }
}

TEST(CustomFilterShapesPresets, DefaultShapesAreStillTheVowelsAtTheirCorners) {
    const auto shapes = CustomFilterShapes::defaultShapes();
    ASSERT_EQ(shapes.shapes.size(), 3u);
    EXPECT_EQ(shapes.shapes[0].name, "ah");
    EXPECT_EQ(shapes.shapes[0].curve, CustomFilterShapes::presetCurve(CustomFilterShapes::Preset::Ah));
    EXPECT_EQ(shapes.shapes[0].position, (std::vector<float>{0.2f, 0.25f}));
    EXPECT_EQ(shapes.shapes[1].position, (std::vector<float>{0.8f, 0.25f}));
    EXPECT_EQ(shapes.shapes[2].position, (std::vector<float>{0.5f, 0.8f}));
    // The saved form is part of the project format: pin the first curve's size and first point.
    EXPECT_EQ(shapes.shapes[0].curve.points.size(), 16u);
    EXPECT_FLOAT_EQ(shapes.shapes[0].curve.points[0].y, CustomFilterShapes::dbToY(-39.7f));
    EXPECT_FLOAT_EQ(shapes.shapes[2].curve.points.back().y, CustomFilterShapes::dbToY(-47.0f));
    EXPECT_TRUE(shapes.isDefault());
}

TEST(FilterCustomShapesGeneration, ChangesWheneverTheListIsReplacedByAnyPath) {
    FilterModule filter;
    const int g0 = filter.getCustomShapesGeneration();
    filter.setCustomShapes(CustomFilterShapes::defaultShapes());
    const int g1 = filter.getCustomShapesGeneration();
    EXPECT_NE(g0, g1);
    filter.setExtraState(CustomFilterShapes::defaultShapes().toVar());
    EXPECT_NE(filter.getCustomShapesGeneration(), g1);
}

TEST(FilterCustomControls, TheKnobGettersReadTheParameters) {
    FilterModule filter;
    *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&filter, "morphX")) = 0.25f;
    *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&filter, "shift")) = 7.0f;
    EXPECT_FLOAT_EQ(filter.getCurrentMorphX(), 0.25f);
    EXPECT_FLOAT_EQ(filter.getCurrentShift(), 7.0f);
    EXPECT_FLOAT_EQ(filter.getCurrentMorphY(), 0.5f);
    EXPECT_FLOAT_EQ(filter.getCurrentDepth(), 1.0f);
}
