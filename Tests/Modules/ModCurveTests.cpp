// ModCurveTests.cpp -- synth::ModCurve: the presets' shape, the linear test, the JSON round trip and its
// never-fails fallback, range mapping, and the mirrored lookup the Attenuverter reads on the audio thread.
#include "Modules/ModCurve.h"
#include <gtest/gtest.h>
#include <limits>

using synth::ModCurve;

namespace {
float curveAtInput(const ModCurve& c, float input) {
    ModCurve::Table t;
    c.renderTable(t);
    return ModCurve::lookup(t, input);
}

juce::var curvePointVar(double x, juce::var y) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("x", x);
    obj->setProperty("y", y);
    return juce::var(obj);
}

juce::var curveJsonVar(juce::Array<juce::var> points, juce::var rangeMin = {}, juce::var rangeMax = {}) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", 1);
    obj->setProperty("points", juce::var(points));
    if (!rangeMin.isVoid())
        obj->setProperty("rangeMin", rangeMin);
    if (!rangeMax.isVoid())
        obj->setProperty("rangeMax", rangeMax);
    return juce::var(obj);
}
} // namespace

TEST(ModCurveTest, LinearIsTheDefaultAndTheOnlyLinear) {
    EXPECT_TRUE(ModCurve{}.isLinear());
    EXPECT_TRUE(ModCurve::linear().isLinear());
    EXPECT_TRUE(ModCurve::preset(ModCurve::Preset::Linear).isLinear());
    EXPECT_FALSE(ModCurve::preset(ModCurve::Preset::Exp).isLinear());
    EXPECT_FALSE(ModCurve::preset(ModCurve::Preset::Log).isLinear());
    EXPECT_FALSE(ModCurve::preset(ModCurve::Preset::SCurve).isLinear());

    auto ranged = ModCurve::linear();
    ranged.rangeMax = 0.9f;
    EXPECT_FALSE(ranged.isLinear());
    auto bent = ModCurve::linear();
    bent.shape.points[0].bend = 0.1f;
    EXPECT_FALSE(bent.isLinear());
}

TEST(ModCurveTest, PresetsHaveTheirShape) {
    EXPECT_NEAR(curveAtInput(ModCurve::linear(), 0.5f), 0.5f, 1.0e-3f);
    EXPECT_LT(curveAtInput(ModCurve::preset(ModCurve::Preset::Exp), 0.5f), 0.3f);
    EXPECT_GT(curveAtInput(ModCurve::preset(ModCurve::Preset::Log), 0.5f), 0.7f);

    const auto s = ModCurve::preset(ModCurve::Preset::SCurve);
    EXPECT_EQ(s.shape.points.size(), 3u);
    EXPECT_LT(curveAtInput(s, 0.25f), 0.25f);
    EXPECT_GT(curveAtInput(s, 0.75f), 0.75f);
    for (auto preset : {ModCurve::Preset::Exp, ModCurve::Preset::Log, ModCurve::Preset::SCurve}) {
        EXPECT_NEAR(curveAtInput(ModCurve::preset(preset), 0.0f), 0.0f, 1.0e-6f);
        EXPECT_NEAR(curveAtInput(ModCurve::preset(preset), 1.0f), 1.0f, 1.0e-6f);
    }
}

TEST(ModCurveTest, VarRoundTripKeepsShapeAndRange) {
    auto c = ModCurve::preset(ModCurve::Preset::SCurve);
    c.rangeMin = 0.1f;
    c.rangeMax = 0.8f;
    EXPECT_EQ(ModCurve::fromVar(c.toVar()), c);
    EXPECT_EQ(ModCurve::fromVar(juce::JSON::parse(juce::JSON::toString(c.toVar()))), c);
    EXPECT_TRUE(ModCurve::fromVar(ModCurve::linear().toVar()).isLinear());
}

TEST(ModCurveTest, GarbageGivesLinearNotATriangle) {
    EXPECT_TRUE(ModCurve::fromVar({}).isLinear());
    EXPECT_TRUE(ModCurve::fromVar(juce::var("text")).isLinear());
    EXPECT_TRUE(ModCurve::fromVar(juce::var(3.0)).isLinear());
    EXPECT_TRUE(ModCurve::fromVar(curveJsonVar({})).isLinear());
    EXPECT_TRUE(ModCurve::fromVar(curveJsonVar({curvePointVar(0.0, 0.0)})).isLinear());
    // One valid point plus one non-finite: still fewer than two.
    EXPECT_TRUE(ModCurve::fromVar(curveJsonVar({curvePointVar(0.0, 0.0),
                                                curvePointVar(1.0, std::numeric_limits<double>::quiet_NaN())}))
                    .isLinear());
    EXPECT_TRUE(ModCurve::fromVar(curveJsonVar({curvePointVar(0.0, 0.0), curvePointVar(1.0, "x")})).isLinear());

    const juce::Array<juce::var> two{curvePointVar(0.0, 0.0), curvePointVar(1.0, 1.0)};
    EXPECT_TRUE(ModCurve::fromVar(curveJsonVar(two, std::numeric_limits<double>::infinity())).isLinear());
    EXPECT_TRUE(ModCurve::fromVar(curveJsonVar(two, juce::var("a"))).isLinear());

    auto* newer = new juce::DynamicObject();
    newer->setProperty("version", 99);
    newer->setProperty("points", juce::var(two));
    EXPECT_TRUE(ModCurve::fromVar(juce::var(newer)).isLinear());
}

TEST(ModCurveTest, RangeIsClampedAndOrdered) {
    const juce::Array<juce::var> two{curvePointVar(0.0, 0.0), curvePointVar(1.0, 1.0)};
    const auto clamped = ModCurve::fromVar(curveJsonVar(two, -3.0, 7.0));
    EXPECT_EQ(clamped.rangeMin, 0.0f);
    EXPECT_EQ(clamped.rangeMax, 1.0f);
    const auto swapped = ModCurve::fromVar(curveJsonVar(two, 0.8, 0.2));
    EXPECT_FLOAT_EQ(swapped.rangeMin, 0.2f);
    EXPECT_FLOAT_EQ(swapped.rangeMax, 0.8f);
}

TEST(ModCurveTest, RangeMapsTheOutput) {
    auto c = ModCurve::linear();
    c.rangeMin = 0.2f;
    c.rangeMax = 0.6f;
    ModCurve::Table t;
    c.renderTable(t);
    EXPECT_NEAR(t[0], 0.2f, 1.0e-6f);
    EXPECT_NEAR(t[ModCurve::kTableSize / 2], 0.4f, 1.0e-5f);
    EXPECT_NEAR(t[ModCurve::kTableSize], 0.6f, 1.0e-6f);
}

TEST(ModCurveTest, TableEndIsTheLastPointNotTheFirst) {
    ModCurve::Table t;
    ModCurve::linear().renderTable(t);
    EXPECT_FLOAT_EQ(t[0], 0.0f);
    EXPECT_FLOAT_EQ(t[ModCurve::kTableSize], 1.0f);

    auto c = ModCurve::linear();
    c.shape.points.back().y = 0.25f;
    c.renderTable(t);
    EXPECT_FLOAT_EQ(t[ModCurve::kTableSize], 0.25f);
}

TEST(ModCurveTest, LookupMirrorsClampsAndSurvivesNaN) {
    const auto c = ModCurve::preset(ModCurve::Preset::Exp);
    const float pos = curveAtInput(c, 0.5f);
    EXPECT_GT(pos, 0.0f);
    EXPECT_FLOAT_EQ(curveAtInput(c, -0.5f), -pos);
    EXPECT_NEAR(curveAtInput(c, 1.0f), 1.0f, 1.0e-6f);
    EXPECT_FLOAT_EQ(curveAtInput(c, 5.0f), curveAtInput(c, 1.0f));
    EXPECT_FLOAT_EQ(curveAtInput(c, -5.0f), -curveAtInput(c, 1.0f));
    EXPECT_FLOAT_EQ(curveAtInput(c, std::numeric_limits<float>::infinity()), curveAtInput(c, 1.0f));
    EXPECT_EQ(curveAtInput(c, std::numeric_limits<float>::quiet_NaN()), 0.0f);
    EXPECT_EQ(curveAtInput(c, 0.0f), 0.0f);

    // Zero counts as positive: a curve that starts above zero keeps its sign at 0 and -0.
    auto lifted = ModCurve::linear();
    lifted.rangeMin = 0.3f;
    EXPECT_FLOAT_EQ(curveAtInput(lifted, 0.0f), 0.3f);
    EXPECT_FLOAT_EQ(curveAtInput(lifted, -0.0f), 0.3f);
}
