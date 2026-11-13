// SamplerInstrumentTests.cpp -- region selection (key, velocity layers, crossfade, round robin, release
// triggers) and building an instrument from files.
#include "Modules/Sampler/SamplerInstrument.h"
#include "SamplerTestWavs.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace synth::sampler;
using namespace sampler_test;

namespace {

std::array<RegionHit, kMaxHitsPerNote> hits;

int select(const Instrument& inst, int note, int velocity, bool release = false, int rr = 0) {
    return selectRegions(inst, note, velocity, release, rr, hits);
}

Instrument::Ptr twoLayers() {
    return makeInstrument({makeRegion(nullptr, 0, 127, 1, 63), makeRegion(nullptr, 0, 127, 64, 127)});
}

} // namespace

TEST(SamplerInstrumentSelectTest, KeyRangeDecidesWhichRegionsMatch) {
    auto inst = makeInstrument({makeRegion(nullptr, 0, 59), makeRegion(nullptr, 60, 127)});
    ASSERT_EQ(select(*inst, 59, 100), 1);
    EXPECT_EQ(hits[0].regionIndex, 0);
    ASSERT_EQ(select(*inst, 60, 100), 1);
    EXPECT_EQ(hits[0].regionIndex, 1);
    EXPECT_FLOAT_EQ(hits[0].gain, 1.0f);
}

TEST(SamplerInstrumentSelectTest, VelocityFarFromABorderPicksOneLayerAtFullGain) {
    auto inst = twoLayers();
    ASSERT_EQ(select(*inst, 60, 20), 1);
    EXPECT_EQ(hits[0].regionIndex, 0);
    EXPECT_FLOAT_EQ(hits[0].gain, 1.0f);
    ASSERT_EQ(select(*inst, 60, 100), 1);
    EXPECT_EQ(hits[0].regionIndex, 1);
    EXPECT_FLOAT_EQ(hits[0].gain, 1.0f);
    ASSERT_EQ(select(*inst, 60, 1), 1);
    ASSERT_EQ(select(*inst, 60, 127), 1);
    EXPECT_FLOAT_EQ(hits[0].gain, 1.0f);
}

TEST(SamplerInstrumentSelectTest, VelocityNearTheBorderCrossfadesWithEqualPower) {
    auto inst = twoLayers();
    for (int vel = 56; vel <= 71; ++vel) {
        ASSERT_EQ(select(*inst, 60, vel), 2) << vel;
        const float power = hits[0].gain * hits[0].gain + hits[1].gain * hits[1].gain;
        EXPECT_NEAR(power, 1.0f, 1.0e-4f) << vel;
    }
    // The lower layer fades out and the upper one in as velocity rises.
    select(*inst, 60, 58);
    const float lowerAt58 = hits[0].regionIndex == 0 ? hits[0].gain : hits[1].gain;
    select(*inst, 60, 69);
    const float lowerAt69 = hits[0].regionIndex == 0 ? hits[0].gain : hits[1].gain;
    EXPECT_GT(lowerAt58, lowerAt69);
}

TEST(SamplerInstrumentSelectTest, NoNeighbourOnThisNoteMeansNoCrossfade) {
    auto inst = makeInstrument({makeRegion(nullptr, 0, 59, 1, 63), makeRegion(nullptr, 60, 127, 64, 127)});
    ASSERT_EQ(select(*inst, 50, 60), 1);
    EXPECT_FLOAT_EQ(hits[0].gain, 1.0f);
}

TEST(SamplerInstrumentSelectTest, RoundRobinCyclesThroughTheSlots) {
    auto inst = makeInstrument({makeRegion(nullptr), makeRegion(nullptr), makeRegion(nullptr)});
    for (int i = 0; i < 3; ++i) {
        inst->regions[(size_t)i].seqLength = 3;
        inst->regions[(size_t)i].seqPosition = i + 1;
    }
    for (int counter = 0; counter < 7; ++counter) {
        ASSERT_EQ(select(*inst, 60, 90, false, counter), 1);
        EXPECT_EQ(hits[0].regionIndex, counter % 3);
    }
}

TEST(SamplerInstrumentSelectTest, ReleaseRegionsOnlyAnswerReleases) {
    auto inst = makeInstrument({makeRegion(nullptr), makeRegion(nullptr)});
    inst->regions[1].releaseTrigger = true;
    ASSERT_EQ(select(*inst, 60, 90, false), 1);
    EXPECT_EQ(hits[0].regionIndex, 0);
    ASSERT_EQ(select(*inst, 60, 90, true), 1);
    EXPECT_EQ(hits[0].regionIndex, 1);
}

TEST(SamplerInstrumentSelectTest, NeverWritesMoreThanTheHitLimit) {
    std::vector<Region> regions;
    for (int i = 0; i < 10; ++i)
        regions.push_back(makeRegion(nullptr));
    auto inst = makeInstrument(std::move(regions));
    EXPECT_EQ(select(*inst, 60, 90), kMaxHitsPerNote);
}

TEST(SamplerInstrumentBuildTest, SharesOneDecodePerFileAndDropsUnreadableOnes) {
    TempDir dir;
    ASSERT_TRUE(writeWav(dir.file("a.wav"), 1000, 1, 44100.0, [](int, int) { return 0.25f; }));
    ASSERT_TRUE(writeWav(dir.file("b.wav"), 500, 2, 44100.0, [](int, int) { return 0.5f; }));
    std::vector<RegionSpec> specs(4);
    specs[0].samplePath = dir.file("a.wav").getFullPathName();
    specs[1].samplePath = dir.file("a.wav").getFullPathName();
    specs[2].samplePath = dir.file("missing.wav").getFullPathName();
    specs[3].samplePath = dir.file("b.wav").getFullPathName();
    specs[1].loKey = 10;

    juce::StringArray errors;
    auto inst = buildInstrument("kit", specs, errors);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->name, "kit");
    ASSERT_EQ(inst->regions.size(), 3u);
    EXPECT_EQ(inst->regions[0].sample.get(), inst->regions[1].sample.get());
    EXPECT_NE(inst->regions[0].sample.get(), inst->regions[2].sample.get());
    EXPECT_EQ(inst->regions[1].loKey, 10);
    EXPECT_EQ(errors.size(), 1);
    EXPECT_TRUE(errors[0].contains("missing.wav"));
    EXPECT_EQ(inst->memoryBytes(), (size_t)(1000 * 1 + 500 * 2) * sizeof(float));
}

TEST(SamplerInstrumentBuildTest, ReturnsNullWhenNothingDecodes) {
    std::vector<RegionSpec> specs(1);
    specs[0].samplePath = "/definitely/not/here.wav";
    juce::StringArray errors;
    EXPECT_EQ(buildInstrument("x", specs, errors), nullptr);
    EXPECT_FALSE(errors.isEmpty());
}

TEST(SamplerInstrumentBuildTest, ReturnsNullWhenTheMemoryBudgetIsExceeded) {
    TempDir dir;
    ASSERT_TRUE(writeWav(dir.file("a.wav"), 1000, 1, 44100.0, [](int, int) { return 0.25f; }));
    std::vector<RegionSpec> specs(1);
    specs[0].samplePath = dir.file("a.wav").getFullPathName();
    juce::StringArray errors;
    EXPECT_EQ(buildInstrument("x", specs, errors, 1000), nullptr);
    ASSERT_FALSE(errors.isEmpty());
    EXPECT_TRUE(errors.joinIntoString(" ").containsIgnoreCase("memory"));
    errors.clear();
    EXPECT_NE(buildInstrument("x", specs, errors, 4000), nullptr);
}
