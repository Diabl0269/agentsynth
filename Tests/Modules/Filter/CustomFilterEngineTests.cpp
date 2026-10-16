// CustomFilterEngine: the rendered response follows the drawn shape, blends between shapes at the
// morph cursor, moves with Shift and Depth, swaps kernels without clicks, and never allocates on
// the audio thread.
#include "AllocationCounter.h"
#include "CustomFilterTestHelpers.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::test;

namespace {

CustomFilterShapes single(const CustomFilterShape& s) {
    CustomFilterShapes shapes;
    shapes.shapes = {s};
    return shapes;
}

/** A broad, smooth bump: -12 dB at the ends, 0 dB around 1 kHz. */
CustomFilterShape smoothBump() {
    return makeShape("bump", {{20.0f, -12.0f}, {200.0f, -8.0f}, {1000.0f, 0.0f}, {5000.0f, -6.0f}, {20000.0f, -14.0f}},
                     {0.5f, 0.5f});
}

/** The frequency (on a fine log grid) where `ir`'s magnitude peaks. */
float peakFrequency(const std::vector<float>& ir, float lo, float hi) {
    float best = lo, bestDb = -1.0e9f;
    for (float f : logFrequencies(lo, hi, 400)) {
        const float db = magnitudeDb(ir, f);
        if (db > bestDb) {
            bestDb = db;
            best = f;
        }
    }
    return best;
}

/** Max |y[n] - y[n-1]| over [from, to). */
float maxJump(const std::vector<float>& y, int from, int to) {
    float m = 0.0f;
    for (int n = std::max(1, from); n < to; ++n)
        m = std::max(m, std::abs(y[(size_t)n] - y[(size_t)n - 1]));
    return m;
}

} // namespace

TEST(CustomFilterEngine, ImpulseResponseFollowsTheDrawnShapeWithin1dB) {
    const auto shape = smoothBump();
    CustomFilterEngine engine;
    engine.setShapes(single(shape));
    const auto ir = renderImpulse(engine, {});
    for (float f : logFrequencies(100.0f, 10000.0f, 40))
        EXPECT_NEAR(magnitudeDb(ir, f), drawnDb(shape, f), 1.0f) << f << " Hz";
}

TEST(CustomFilterEngine, HalfwayBlendSitsBetweenTheTwoShapes) {
    const auto low = makeShape("low", {{20.0f, 0.0f}, {20000.0f, -24.0f}}, {0.0f, 0.5f});
    const auto high = makeShape("high", {{20.0f, -24.0f}, {20000.0f, 0.0f}}, {1.0f, 0.5f});
    CustomFilterShapes shapes;
    shapes.shapes = {low, high};
    CustomFilterEngine engine;
    engine.setShapes(shapes);

    CustomFilterEngine::Controls c;
    c.morphX = 0.5f; // equidistant: 50/50
    const auto ir = renderImpulse(engine, c);
    for (float f : {100.0f, 300.0f, 1000.0f, 3000.0f, 8000.0f}) {
        const float a = drawnDb(low, f), b = drawnDb(high, f);
        const float got = magnitudeDb(ir, f);
        EXPECT_GT(got, std::min(a, b) + 1.0f) << f << " Hz";
        EXPECT_LT(got, std::max(a, b) - 1.0f) << f << " Hz";
        EXPECT_NEAR(got, 0.5f * (a + b), 1.0f) << f << " Hz: blended in dB";
    }
}

TEST(CustomFilterEngine, CursorOnAShapeIsDominatedByIt) {
    const auto low = makeShape("low", {{20.0f, 0.0f}, {20000.0f, -24.0f}}, {0.0f, 0.5f});
    const auto high = makeShape("high", {{20.0f, -24.0f}, {20000.0f, 0.0f}}, {1.0f, 0.5f});
    CustomFilterShapes shapes;
    shapes.shapes = {low, high};
    CustomFilterEngine engine;
    engine.setShapes(shapes);
    CustomFilterEngine::Controls c;
    c.morphX = 0.0f;
    const auto ir = renderImpulse(engine, c);
    for (float f : {200.0f, 2000.0f})
        EXPECT_NEAR(magnitudeDb(ir, f), drawnDb(low, f), 1.0f) << f << " Hz";
}

TEST(CustomFilterEngine, ShiftMovesThePeakByTheExpectedRatio) {
    const auto peak = makeShape("peak", {{20.0f, -30.0f}, {1000.0f, 0.0f}, {20000.0f, -30.0f}}, {0.5f, 0.5f});
    CustomFilterEngine engine;
    engine.setShapes(single(peak));
    const float base = peakFrequency(renderImpulse(engine, {}), 200.0f, 8000.0f);
    EXPECT_NEAR(base / 1000.0f, 1.0f, 0.03f);

    for (float semis : {12.0f, -12.0f, 7.0f}) {
        CustomFilterEngine::Controls c;
        c.shiftSemitones = semis;
        const float moved = peakFrequency(renderImpulse(engine, c), 200.0f, 8000.0f);
        EXPECT_NEAR(moved / base, std::pow(2.0f, semis / 12.0f), 0.03f) << semis << " st";
    }
}

TEST(CustomFilterEngine, DepthZeroIsFlatAndDepthScalesDb) {
    const auto shape = smoothBump();
    CustomFilterEngine engine;
    engine.setShapes(single(shape));

    CustomFilterEngine::Controls flat;
    flat.depth = 0.0f;
    const auto irFlat = renderImpulse(engine, flat);
    for (float f : logFrequencies(20.0f, 20000.0f, 40))
        EXPECT_NEAR(magnitudeDb(irFlat, f), 0.0f, 0.1f) << f << " Hz";

    CustomFilterEngine::Controls doubled;
    doubled.depth = 2.0f;
    const auto irDouble = renderImpulse(engine, doubled);
    for (float f : logFrequencies(100.0f, 10000.0f, 20))
        EXPECT_NEAR(magnitudeDb(irDouble, f), 2.0f * drawnDb(shape, f), 1.5f) << f << " Hz";
}

TEST(CustomFilterEngine, DefaultShapesSoundAndNeverAddGain) {
    CustomFilterEngine engine; // the three vowel shapes
    ASSERT_EQ((int)engine.getShapes().shapes.size(), 3);
    const auto ir = renderImpulse(engine, {});
    float peak = -1.0e9f;
    for (float f : logFrequencies(20.0f, 20000.0f, 200))
        peak = std::max(peak, magnitudeDb(ir, f));
    EXPECT_GT(peak, -12.0f) << "the default must be audible";
    EXPECT_LT(peak, 3.0f) << "the default must not add gain";
}

TEST(CustomFilterEngine, MovingCursorNeverAllocatesOnTheAudioThread) {
    CustomFilterEngine engine;
    engine.prepare(kTestSampleRate);
    std::vector<float> l(512), r(512);
    int designsBefore = 0;
    {
        // Warm up once outside the scope so the first design is not the only one measured.
        float* ch[2] = {l.data(), r.data()};
        engine.process(ch, 2, 512, {});
        designsBefore = engine.getDesignCount();
    }
    AllocationScope scope;
    for (int blk = 0; blk < 64; ++blk) {
        for (int i = 0; i < 512; ++i)
            l[(size_t)i] = r[(size_t)i] = std::sin(0.05f * (float)(blk * 512 + i));
        CustomFilterEngine::Controls c;
        c.morphX = 0.5f + 0.5f * std::sin(0.3f * (float)blk);
        c.morphY = 0.5f + 0.5f * std::cos(0.2f * (float)blk);
        c.shiftSemitones = (float)(blk % 7) - 3.0f;
        c.depth = 0.5f + 0.02f * (float)blk;
        float* ch[2] = {l.data(), r.data()};
        engine.process(ch, 2, 512, c);
    }
    EXPECT_EQ(scope.count(), 0);
    EXPECT_GE(engine.getDesignCount() - designsBefore, 60) << "the cursor moved every block";
}

TEST(CustomFilterEngine, ShapeEditsReachTheAudioThreadWithoutAllocatingThere) {
    CustomFilterEngine engine;
    engine.prepare(kTestSampleRate);
    std::vector<float> buf(512, 0.0f);
    float* ch[1] = {buf.data()};
    engine.process(ch, 1, 512, {});
    const int before = engine.getDesignCount();
    engine.setShapes(single(smoothBump())); // message thread: allocates the new table
    {
        AllocationScope scope;
        engine.process(ch, 1, 512, {});
        EXPECT_EQ(scope.count(), 0);
    }
    EXPECT_EQ(engine.getDesignCount(), before + 1) << "a shape edit redesigns on the next block";
    engine.setShapes(CustomFilterShapes::defaultShapes()); // frees the retired table
}

TEST(CustomFilterEngine, KernelSwapHasNoDiscontinuity) {
    // Two shapes with very different DC gain (0 dB vs -30 dB) and a step of the cursor between
    // them: a hard kernel switch would jump by ~0.48 on a 0.5 DC input.
    const auto loud = makeShape("loud", {{20.0f, 0.0f}, {20000.0f, 0.0f}}, {0.0f, 0.5f});
    const auto quiet = makeShape("quiet", {{20.0f, -30.0f}, {20000.0f, -30.0f}}, {1.0f, 0.5f});
    CustomFilterShapes shapes;
    shapes.shapes = {loud, quiet};

    for (bool sine : {false, true}) {
        CustomFilterEngine engine;
        engine.setShapes(shapes);
        engine.prepare(kTestSampleRate);
        constexpr int kBlock = 512, kBlocks = 16, kSwap = 8;
        std::vector<float> y((size_t)(kBlock * kBlocks));
        for (size_t n = 0; n < y.size(); ++n)
            y[n] = sine ? 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 200.0f * (float)n / 44100.0f) : 0.5f;
        for (int blk = 0; blk < kBlocks; ++blk) {
            CustomFilterEngine::Controls c;
            c.morphX = blk < kSwap ? 0.0f : 1.0f;
            float* ch[1] = {y.data() + blk * kBlock};
            engine.process(ch, 1, kBlock, c);
        }
        const float steady = maxJump(y, 3 * kBlock, kSwap * kBlock);
        const float across = maxJump(y, kSwap * kBlock - 1, (kSwap + 2) * kBlock);
        EXPECT_LT(across, steady + 0.002f) << (sine ? "sine" : "DC") << ": steady " << steady;
        EXPECT_LT(std::abs(y[(size_t)((kBlocks - 1) * kBlock)]), 0.5f * 0.05f) << "ends on the quiet shape";
    }
}

// Rough CPU cost, printed rather than asserted (run with --gtest_also_run_disabled_tests, ideally
// on a Release build): mono vs 8 voices x 2 legs, with a still and a moving cursor.
TEST(CustomFilterEngine, DISABLED_CpuCost) {
    constexpr int kBlock = 512, kBlocks = 400;
    for (int channels : {1, 16}) {
        for (bool moving : {false, true}) {
            CustomFilterEngine engine;
            engine.prepare(kTestSampleRate);
            std::vector<std::vector<float>> bufs((size_t)channels, std::vector<float>(kBlock));
            std::vector<float*> ptrs;
            for (auto& b : bufs)
                ptrs.push_back(b.data());
            juce::Random rng(5);
            const auto start = juce::Time::getHighResolutionTicks();
            for (int blk = 0; blk < kBlocks; ++blk) {
                for (auto& b : bufs)
                    for (auto& x : b)
                        x = rng.nextFloat() - 0.5f;
                CustomFilterEngine::Controls c;
                if (moving)
                    c.morphX = 0.5f + 0.4f * std::sin(0.1f * (float)blk);
                engine.process(ptrs.data(), channels, kBlock, c);
            }
            const double secs = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
            const double audioSecs = (double)kBlocks * kBlock / kTestSampleRate;
            std::printf("[cpu] %2d ch, %s cursor: %.2f %% of one core (%d designs)\n", channels,
                        moving ? "moving" : "still ", 100.0 * secs / audioSecs, engine.getDesignCount());
        }
    }
}
