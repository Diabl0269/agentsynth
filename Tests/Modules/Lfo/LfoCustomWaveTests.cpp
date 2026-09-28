// LfoCustomWaveTests.cpp -- synth::LfoCustomWave, the pure headless model behind
// LFOModule's Custom waveform: the default/preset shapes, JSON round-trip and sanitise rules
// (LFOModule::setCustomWave/setExtraState's only gate against untrusted-shaped input), the
// right-continuous step evaluator, the render table's wrap guard, and the whole-wave tools.
#include "Modules/Lfo/LfoCustomWave.h"
#include <gtest/gtest.h>

using synth::LfoCustomWave;

namespace {
juce::var pointVar(double x, double y, juce::var bend = {}) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("x", x);
    obj->setProperty("y", y);
    if (!bend.isVoid())
        obj->setProperty("bend", bend);
    return juce::var(obj);
}

juce::var waveVar(int version, juce::Array<juce::var> points) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", version);
    obj->setProperty("points", juce::var(points));
    return juce::var(obj);
}
} // namespace

TEST(LfoCustomWaveTest, DefaultWaveIsTriangleAndRoundTrips) {
    const auto def = LfoCustomWave::defaultWave();
    const auto triangle = LfoCustomWave::preset(LfoCustomWave::Preset::Triangle);
    EXPECT_EQ(def, triangle);
    EXPECT_TRUE(def.isDefault());

    const auto roundTripped = LfoCustomWave::fromVar(def.toVar());
    EXPECT_EQ(roundTripped, def);
}

TEST(LfoCustomWaveTest, ToVarWritesVersionAndPoints) {
    const auto wave = LfoCustomWave::preset(LfoCustomWave::Preset::RampUp);
    const auto v = wave.toVar();
    auto* obj = v.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ((int)obj->getProperty("version"), LfoCustomWave::kVersion);
    ASSERT_TRUE(obj->getProperty("points").isArray());
    EXPECT_EQ(obj->getProperty("points").getArray()->size(), 2);
}

TEST(LfoCustomWaveTest, SanitiseClampsSortsAndPinsEndpoints) {
    juce::Array<juce::var> pts;
    pts.add(pointVar(0.5, 1.5));      // y clamped to 1
    pts.add(pointVar(-0.2, -1.0));    // x, y clamped to 0; sorts before the first
    pts.add(pointVar(2.0, 0.5, 5.0)); // x clamped to 1; bend clamped to 1
    const auto wave = LfoCustomWave::fromVar(waveVar(1, pts));

    ASSERT_EQ((int)wave.points.size(), 3);
    EXPECT_FLOAT_EQ(wave.points[0].x, 0.0f);
    EXPECT_FLOAT_EQ(wave.points[0].y, 0.0f);
    EXPECT_FLOAT_EQ(wave.points[1].y, 1.0f);
    EXPECT_FLOAT_EQ(wave.points.back().x, 1.0f);
    EXPECT_FLOAT_EQ(wave.points.back().bend, 0.0f) << "the last point's outgoing bend is always forced to 0";
}

TEST(LfoCustomWaveTest, SanitiseDropsNonFiniteAndNonObjectPoints) {
    juce::Array<juce::var> pts;
    pts.add(pointVar(0.0, 0.0));
    pts.add(juce::var("not an object"));
    pts.add(pointVar(std::numeric_limits<double>::quiet_NaN(), 0.5));
    pts.add(pointVar(1.0, 1.0));
    const auto wave = LfoCustomWave::fromVar(waveVar(1, pts));

    ASSERT_EQ((int)wave.points.size(), 2);
    EXPECT_FLOAT_EQ(wave.points[0].y, 0.0f);
    EXPECT_FLOAT_EQ(wave.points[1].y, 1.0f);
}

TEST(LfoCustomWaveTest, SanitiseCapsPointCount) {
    juce::Array<juce::var> pts;
    for (int i = 0; i < 10000; ++i)
        pts.add(pointVar((double)i / 9999.0, 0.5));
    const auto wave = LfoCustomWave::fromVar(waveVar(1, pts));

    EXPECT_EQ((int)wave.points.size(), LfoCustomWave::kMaxPoints);
    EXPECT_FLOAT_EQ(wave.points[0].x, 0.0f) << "truncation keeps the left-most (lowest-x) points";
}

TEST(LfoCustomWaveTest, SanitiseFallsBackToDefault) {
    const auto def = LfoCustomWave::defaultWave();

    EXPECT_EQ(LfoCustomWave::fromVar(juce::var()), def) << "void var";
    EXPECT_EQ(LfoCustomWave::fromVar(juce::var("not an object")), def) << "non-object";

    auto* missingPoints = new juce::DynamicObject();
    missingPoints->setProperty("version", 1);
    EXPECT_EQ(LfoCustomWave::fromVar(juce::var(missingPoints)), def) << "missing points";

    auto* pointsNotArray = new juce::DynamicObject();
    pointsNotArray->setProperty("version", 1);
    pointsNotArray->setProperty("points", "nope");
    EXPECT_EQ(LfoCustomWave::fromVar(juce::var(pointsNotArray)), def) << "points not an array";

    juce::Array<juce::var> onePoint;
    onePoint.add(pointVar(0.0, 0.0));
    EXPECT_EQ(LfoCustomWave::fromVar(waveVar(1, onePoint)), def) << "fewer than 2 valid points";

    juce::Array<juce::var> twoPoints;
    twoPoints.add(pointVar(0.0, 0.0));
    twoPoints.add(pointVar(1.0, 1.0));
    EXPECT_EQ(LfoCustomWave::fromVar(waveVar(2, twoPoints)), def) << "version newer than kVersion";
}

TEST(LfoCustomWaveTest, EvaluateIsRightContinuousAtSteps) {
    const auto square = LfoCustomWave::preset(LfoCustomWave::Preset::Square);
    EXPECT_FLOAT_EQ(square.evaluate(0.5f), 0.0f) << "at the step, the segment STARTING there wins";
    EXPECT_NEAR(square.evaluate(0.4999f), 1.0f, 1.0e-3f);
}

TEST(LfoCustomWaveTest, EvaluateUsesEnvelopeGeneratorShape) {
    const auto soft = LfoCustomWave::preset(LfoCustomWave::Preset::SoftSine);
    const float linearMidpoint = (soft.points[0].y + soft.points[1].y) * 0.5f;
    EXPECT_NE(soft.evaluate(0.125f), linearMidpoint) << "a non-zero bend must actually bend the segment";
}

TEST(LfoCustomWaveTest, RenderTableWrapGuardEqualsFirstSample) {
    const auto wave = LfoCustomWave::preset(LfoCustomWave::Preset::Triangle);
    LfoCustomWave::Table table{};
    wave.renderTable(table);
    EXPECT_FLOAT_EQ(table[LfoCustomWave::kTableSize], table[0]);
}

TEST(LfoCustomWaveTest, RenderTableRampUpIsMonotonic) {
    const auto wave = LfoCustomWave::preset(LfoCustomWave::Preset::RampUp);
    LfoCustomWave::Table table{};
    wave.renderTable(table);
    for (int i = 1; i < LfoCustomWave::kTableSize; ++i)
        EXPECT_GE(table[(size_t)i], table[(size_t)i - 1]);
}

TEST(LfoCustomWaveTest, InvertPreservesBendsReverseNegatesThem) {
    auto soft = LfoCustomWave::preset(LfoCustomWave::Preset::SoftSine);
    const auto before = soft.points;

    auto inverted = soft;
    inverted.apply(LfoCustomWave::Tool::Invert);
    ASSERT_EQ(inverted.points.size(), before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_FLOAT_EQ(inverted.points[i].y, 1.0f - before[i].y);
        EXPECT_FLOAT_EQ(inverted.points[i].bend, before[i].bend);
    }

    auto reversed = soft;
    reversed.apply(LfoCustomWave::Tool::Reverse);
    const int n = (int)before.size();
    ASSERT_EQ((int)reversed.points.size(), n);
    for (int k = 0; k < n - 1; ++k)
        EXPECT_FLOAT_EQ(reversed.points[(size_t)k].bend, -before[(size_t)(n - 2 - k)].bend);
    EXPECT_FLOAT_EQ(reversed.points.back().bend, 0.0f);
}

TEST(LfoCustomWaveTest, PresetsAreAllValidAndDistinct) {
    const LfoCustomWave::Preset all[] = {LfoCustomWave::Preset::Triangle, LfoCustomWave::Preset::RampUp,
                                         LfoCustomWave::Preset::RampDown, LfoCustomWave::Preset::Square,
                                         LfoCustomWave::Preset::Pulse,    LfoCustomWave::Preset::Steps4,
                                         LfoCustomWave::Preset::SoftSine};
    std::vector<LfoCustomWave> waves;
    for (auto p : all) {
        const auto w = LfoCustomWave::preset(p);
        EXPECT_GE((int)w.points.size(), LfoCustomWave::kMinPoints);
        EXPECT_FLOAT_EQ(w.points.front().x, 0.0f);
        EXPECT_FLOAT_EQ(w.points.back().x, 1.0f);
        EXPECT_FLOAT_EQ(w.points.back().bend, 0.0f);
        waves.push_back(w);
    }
    for (size_t i = 0; i < waves.size(); ++i)
        for (size_t j = i + 1; j < waves.size(); ++j)
            EXPECT_NE(waves[i], waves[j]) << i << " vs " << j;
}
