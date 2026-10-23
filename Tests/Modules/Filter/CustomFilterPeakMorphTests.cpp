// CustomFilterPeakMorphTests.cpp -- the Custom filter's morph moves each shape's peaks toward each other instead of
// cross-fading: peak detection, one formant at the log-mean halfway, a monotone glide, continuity in the cursor
// (including shapes with different peak counts and a folded match), exact shapes at full weight, no added gain, a
// click-free saw sweep through the real engine, and the UI blend equal to what the engine designs from.
#include "CustomFilterTestHelpers.h"
#include "Modules/Filter/CustomFilterBlend.h"
#include "Modules/Filter/CustomFilterPeakMorph.h"
#include <gtest/gtest.h>
#include <limits>

using namespace synth;
using namespace synth::test;
using namespace synth::customfilter;
using Preset = CustomFilterShapes::Preset;

namespace {

constexpr int kMaxPeaks = CustomFilterEngine::ShapeTable::kMaxPeaks;

CustomFilterShape preset(Preset p, float x, float y) {
    return {CustomFilterShapes::presetName(p), CustomFilterShapes::presetCurve(p), {x, y}};
}

CustomFilterShapes listOf(std::vector<CustomFilterShape> shapes) {
    CustomFilterShapes s;
    s.shapes = std::move(shapes);
    s.sanitise();
    return s;
}

/** ah on the left, oo on the right, both at mid height: Morph X sweeps ah -> oo. */
CustomFilterShapes ahOo() { return listOf({preset(Preset::Ah, 0.0f, 0.5f), preset(Preset::Oo, 1.0f, 0.5f)}); }

/** The default vowels plus the three animal calls, whose peak counts differ from the vowels'. */
CustomFilterShapes vowelsAndAnimals() {
    auto s = CustomFilterShapes::defaultShapes();
    s.shapes.push_back(preset(Preset::Wolf, 0.2f, 0.8f));
    s.shapes.push_back(preset(Preset::Crow, 0.85f, 0.75f));
    s.shapes.push_back(preset(Preset::Frog, 0.5f, 0.45f));
    return s;
}

std::vector<float> peaksOf(const CustomFilterEngine::ShapeTable& t, int s) {
    const float* p = t.peaks.data() + s * kMaxPeaks;
    return {p, p + t.peakCount[(size_t)s]};
}

std::vector<float> peaksOf(const std::vector<float>& db) {
    std::vector<float> out((size_t)kMaxPeaks);
    out.resize((size_t)findPeaks(db.data(), (int)db.size(), out.data()));
    return out;
}

std::vector<float> blendAt(const CustomFilterEngine::ShapeTable& t, float x, float y) {
    std::vector<float> w((size_t)t.count), out((size_t)kGridSize);
    CustomFilterEngine::blendWeights(t, x, y, w.data());
    blendShapes(t, w.data(), out.data());
    return out;
}

float gridOf(float hz) { return CustomFilterShapes::hzToX(hz) * (float)(kGridSize - 1); }
float hzOf(float grid) { return CustomFilterShapes::xToHz(grid / (float)(kGridSize - 1)); }

/** Largest change of the blended grid between neighbouring cursor positions, `steps` steps from a to b. */
float maxStepChange(const CustomFilterEngine::ShapeTable& t, float ax, float ay, float bx, float by, int steps) {
    auto prev = blendAt(t, ax, ay);
    float worst = 0.0f;
    for (int i = 1; i <= steps; ++i) {
        const float a = (float)i / (float)steps;
        const auto cur = blendAt(t, ax + (bx - ax) * a, ay + (by - ay) * a);
        for (int g = 0; g < kGridSize; ++g) {
            if (!std::isfinite(cur[(size_t)g]))
                return std::numeric_limits<float>::infinity();
            worst = std::max(worst, std::abs(cur[(size_t)g] - prev[(size_t)g]));
        }
        prev = cur;
    }
    return worst;
}

/** A band-limited saw at `f0` (sum of harmonics below 20 kHz), phase-continuous across calls via `phase`. */
void fillSaw(float* out, int n, double f0, double& phase) {
    const int harmonics = (int)(20000.0 / f0);
    for (int i = 0; i < n; ++i) {
        double v = 0.0;
        for (int h = 1; h <= harmonics; ++h)
            v += std::sin(h * phase) / h;
        out[i] = (float)(0.3 * v);
        phase += 2.0 * juce::MathConstants<double>::pi * f0 / kTestSampleRate;
    }
}

struct ClickMetrics {
    float maxJump = 0.0f;    // max |y[n] - y[n-1]|
    float maxHfBlock = 0.0f; // max per-block RMS of the second difference (a click is broadband)
};

/** Renders `seconds` of a 110 Hz saw through an engine on ahOo(), Morph X going from x0 to x1 linearly per block,
    and measures the output after a 0.1 s settle. */
ClickMetrics renderSweep(float x0, float x1, double seconds = 1.0) {
    constexpr int kBlock = 512;
    CustomFilterEngine engine;
    engine.setShapes(ahOo());
    engine.prepare(kTestSampleRate);
    const int blocks = (int)(seconds * kTestSampleRate) / kBlock;
    const int settle = (int)(0.1 * kTestSampleRate) / kBlock;
    std::vector<float> y((size_t)(blocks * kBlock));
    double phase = 0.0;
    for (int b = 0; b < blocks; ++b) {
        float* ch[1] = {y.data() + b * kBlock};
        fillSaw(ch[0], kBlock, 110.0, phase);
        CustomFilterEngine::Controls c;
        c.morphY = 0.5f;
        c.morphX = b < settle ? x0 : x0 + (x1 - x0) * (float)(b - settle) / (float)(blocks - 1 - settle);
        engine.process(ch, 1, kBlock, c);
    }
    ClickMetrics m;
    for (int b = settle; b < blocks; ++b) {
        double hf = 0.0;
        for (int i = 0; i < kBlock; ++i) {
            const size_t n = (size_t)(b * kBlock + i);
            m.maxJump = std::max(m.maxJump, std::abs(y[n] - y[n - 1]));
            const float d2 = y[n] - 2.0f * y[n - 1] + y[n - 2];
            hf += (double)d2 * d2;
        }
        m.maxHfBlock = std::max(m.maxHfBlock, (float)std::sqrt(hf / kBlock));
    }
    return m;
}

} // namespace

TEST(CustomFilterPeakMorph, FindsTheVowelFormantsAndIgnoresRipples) {
    const auto table = CustomFilterEngine::renderTable(ahOo());
    const auto ah = peaksOf(*table, 0), oo = peaksOf(*table, 1);
    ASSERT_EQ(ah.size(), 3u);
    ASSERT_EQ(oo.size(), 3u);
    const float ahHz[] = {650.0f, 1080.0f, 2650.0f}, ooHz[] = {350.0f, 600.0f, 2900.0f};
    for (size_t k = 0; k < 3; ++k) {
        EXPECT_NEAR(ah[k], gridOf(ahHz[k]), 2.0f) << "ah peak " << k;
        EXPECT_NEAR(oo[k], gridOf(ooHz[k]), 2.0f) << "oo peak " << k;
    }

    // A broad bump with a 1 dB ripple on its skirt and a 2 dB shelf: one peak.
    const auto rippled = makeShape(
        "r",
        {{20.0f, -30.0f}, {1000.0f, 0.0f}, {1400.0f, -6.0f}, {1500.0f, -5.0f}, {1700.0f, -8.0f}, {20000.0f, -30.0f}},
        {0.5f, 0.5f});
    const auto t2 = CustomFilterEngine::renderTable(listOf({rippled}));
    const auto p = peaksOf(*t2, 0);
    ASSERT_EQ(p.size(), 1u);
    EXPECT_NEAR(p[0], gridOf(1000.0f), 2.0f);

    // A peak that rises out of a grid end is not a peak; neither is a flat curve.
    const auto t3 =
        CustomFilterEngine::renderTable(listOf({makeShape("tilt", {{20.0f, -20.0f}, {20000.0f, 0.0f}}, {0.5f, 0.5f}),
                                                {"flat", CustomFilterShapes::flatCurve(), {0.2f, 0.2f}}}));
    EXPECT_TRUE(peaksOf(*t3, 0).empty());
    EXPECT_TRUE(peaksOf(*t3, 1).empty());
}

TEST(CustomFilterPeakMorph, HalfwayBetweenAhAndOoThereIsOneF1AtTheLogMean) {
    const auto table = CustomFilterEngine::renderTable(ahOo());
    const auto ah = peaksOf(*table, 0), oo = peaksOf(*table, 1);
    const float w[] = {0.5f, 0.5f};
    std::vector<float> out((size_t)kGridSize), fade((size_t)kGridSize);
    blendShapes(*table, w, out.data());
    for (int i = 0; i < kGridSize; ++i)
        fade[(size_t)i] = 0.5f * (table->db[(size_t)i] + table->db[(size_t)(kGridSize + i)]);

    const auto got = peaksOf(out);
    ASSERT_GE(got.size(), 2u);
    EXPECT_NEAR(got[0], 0.5f * (ah[0] + oo[0]), 3.0f)
        << "F1 at the log-mean of " << hzOf(ah[0]) << " and " << hzOf(oo[0]) << " Hz, got " << hzOf(got[0]);
    EXPECT_NEAR(got[1], 0.5f * (ah[1] + oo[1]), 3.0f) << "F2 at the log-mean, got " << hzOf(got[1]);
    // Between oo's F1 and ah's F2 the morph has exactly the two moved formants; the cross-fade had both pairs.
    const auto inSpan = [&](const std::vector<float>& peaks) {
        return std::count_if(peaks.begin(), peaks.end(),
                             [&](float p) { return p >= oo[0] - 3.0f && p <= ah[1] + 3.0f; });
    };
    EXPECT_EQ(inSpan(got), 2);
    EXPECT_GE(inSpan(peaksOf(fade)), 3) << "the plain fade this replaces is double-peaked";
    // The moved F1 is as loud as the two F1s' mean; the fade left it about 10 dB down.
    const float f1Level = out[(size_t)std::lround(got[0])];
    const float meanLevel =
        0.5f * (table->db[(size_t)std::lround(ah[0])] + table->db[(size_t)(kGridSize + std::lround(oo[0]))]);
    EXPECT_NEAR(f1Level, meanLevel, 1.0f);
}

TEST(CustomFilterPeakMorph, ThePeaksGlideMonotonicallyAsTheCursorSweepsAhToOo) {
    const auto table = CustomFilterEngine::renderTable(ahOo());
    constexpr int kSteps = 100;
    float prevF1 = 1.0e9f, prevF2 = 1.0e9f;
    for (int i = 0; i <= kSteps; ++i) {
        const float x = (float)i / (float)kSteps;
        const auto p = peaksOf(blendAt(*table, x, 0.5f));
        ASSERT_GE(p.size(), 2u) << "x " << x;
        EXPECT_LE(p[0], prevF1 + 0.5f) << "F1 moves down only, x " << x;
        EXPECT_LE(p[1], prevF2 + 0.5f) << "F2 moves down only, x " << x;
        prevF1 = p[0];
        prevF2 = p[1];
        if (i % 25 == 0)
            printf("[ morph ] ah->oo %3d%%: F1 %.0f Hz, F2 %.0f Hz\n", i, hzOf(p[0]), hzOf(p[1]));
    }
    EXPECT_NEAR(prevF1, peaksOf(*table, 1)[0], 1.0f) << "ends on oo";
}

// Continuity in the cursor: halving the step must roughly halve the largest per-step change of the blended grid
// (a jump would keep it), on sweeps through the shapes' dots, across shapes with different peak counts, and on a
// pair whose order matching folds (a heavy one-peak shape above a two-peak shape's second peak).
TEST(CustomFilterPeakMorph, TheBlendIsContinuousInTheCursor) {
    const auto mixed = CustomFilterEngine::renderTable(vowelsAndAnimals());
    const auto folded = CustomFilterEngine::renderTable(listOf(
        {makeShape("lowPair", {{20.0f, -30.0f}, {200.0f, 0.0f}, {280.0f, -24.0f}, {400.0f, 0.0f}, {20000.0f, -40.0f}},
                   {0.0f, 0.5f}),
         makeShape("high", {{20.0f, -40.0f}, {5000.0f, 0.0f}, {20000.0f, -30.0f}}, {1.0f, 0.5f})}));
    struct Path {
        const CustomFilterEngine::ShapeTable* t;
        float ax, ay, bx, by;
        const char* name;
    };
    const Path paths[] = {
        {mixed.get(), 0.0f, 0.25f, 1.0f, 0.25f, "through ah and ee"},
        {mixed.get(), 0.0f, 0.95f, 1.0f, 0.0f, "diagonal past wolf, frog, ee"},
        {mixed.get(), 0.5f, 0.0f, 0.5f, 1.0f, "through frog and oo"},
        {folded.get(), 0.0f, 0.5f, 1.0f, 0.5f, "folded match"},
    };
    for (const auto& p : paths) {
        const float coarse = maxStepChange(*p.t, p.ax, p.ay, p.bx, p.by, 400);
        const float fine = maxStepChange(*p.t, p.ax, p.ay, p.bx, p.by, 800);
        printf("[ morph ] %s: max step change %.3f dB at 400 steps, %.3f dB at 800\n", p.name, coarse, fine);
        EXPECT_LE(fine, 0.65f * coarse) << p.name;
        EXPECT_LT(fine, 2.5f) << p.name << ": one step is 1/800 of the way across the map";
    }
}

TEST(CustomFilterPeakMorph, AShapeAtFullWeightIsCopiedExactly) {
    const auto table = CustomFilterEngine::renderTable(vowelsAndAnimals());
    std::vector<float> w((size_t)table->count), out((size_t)kGridSize);
    for (int s = 0; s < table->count; ++s) {
        std::fill(w.begin(), w.end(), 0.0f);
        w[(size_t)s] = 1.0f;
        blendShapes(*table, w.data(), out.data());
        for (int i = 0; i < kGridSize; ++i)
            ASSERT_EQ(out[(size_t)i], table->db[(size_t)(s * kGridSize + i)]) << "shape " << s << " point " << i;
    }
}

TEST(CustomFilterPeakMorph, TheMorphNeverRisesAboveTheLoudestShape) {
    const auto table = CustomFilterEngine::renderTable(vowelsAndAnimals());
    const float ceiling = *std::max_element(table->db.begin(), table->db.end());
    for (int i = 0; i <= 20; ++i)
        for (int j = 0; j <= 20; ++j) {
            const auto out = blendAt(*table, (float)i / 20.0f, (float)j / 20.0f);
            ASSERT_LE(*std::max_element(out.begin(), out.end()), ceiling + 1.0e-4f) << i << ", " << j;
        }
}

TEST(CustomFilterPeakMorph, TheUiBlendIsWhatTheEngineDesignsFrom) {
    const auto shapes = vowelsAndAnimals();
    CustomFilterEngine engine;
    engine.setShapes(shapes);
    for (auto [x, y] : {std::pair{0.3f, 0.4f}, std::pair{0.62f, 0.71f}, std::pair{0.2f, 0.25f}}) {
        CustomFilterEngine::Controls c;
        c.morphX = x;
        c.morphY = y;
        renderImpulse(engine, c, 1024);
        const auto ui = blendedGridDb(shapes, x, y);
        const auto& heard = engine.getBlendedDb();
        ASSERT_EQ(heard.size(), ui.size());
        for (size_t i = 0; i < ui.size(); ++i)
            ASSERT_EQ(heard[i], ui[i]) << "(" << x << ", " << y << ") point " << i;
    }
}

// Through the real engine: a 110 Hz saw while Morph X sweeps ah -> oo over a second has no click (no larger jump and
// no more broadband energy in any block than the same saw held still at points along the way), and the response
// at every point of the sweep stays under the gain audit's ceiling.
TEST(CustomFilterPeakMorph, ASawSweptFromAhToOoHasNoClickAndAddsNoGain) {
    const auto sweep = renderSweep(0.0f, 1.0f);
    ClickMetrics still;
    for (int i = 0; i <= 8; ++i) {
        const auto m = renderSweep((float)i / 8.0f, (float)i / 8.0f, 0.3);
        still.maxJump = std::max(still.maxJump, m.maxJump);
        still.maxHfBlock = std::max(still.maxHfBlock, m.maxHfBlock);
    }
    printf("[ morph ] saw sweep: max jump %.4f (still %.4f), max block HF RMS %.5f (still %.5f)\n", sweep.maxJump,
           still.maxJump, sweep.maxHfBlock, still.maxHfBlock);
    EXPECT_LE(sweep.maxJump, still.maxJump * 1.05f + 0.002f);
    EXPECT_LE(sweep.maxHfBlock, still.maxHfBlock * 1.10f);

    CustomFilterEngine engine;
    engine.setShapes(ahOo());
    float loudest = -1.0e9f;
    for (int i = 0; i <= 8; ++i) {
        CustomFilterEngine::Controls c;
        c.morphX = (float)i / 8.0f;
        c.morphY = 0.5f;
        const auto ir = renderImpulse(engine, c, 4096);
        for (float f : logFrequencies(150.0f, 6000.0f, 600))
            loudest = std::max(loudest, magnitudeDb(ir, f));
    }
    printf("[ morph ] loudest response along the sweep %.2f dB\n", loudest);
    EXPECT_LT(loudest, 6.0f) << "the gain audit ceiling";
}
