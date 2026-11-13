// AttenuverterCurveTests.cpp -- AttenuverterModule's response curve on the audio path: linear stays
// sample-identical, a curve maps the signal before the amount, mirrored for negatives, and the handoff
// never allocates.
#include "Filter/AllocationCounter.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ModCurve.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

using synth::ModCurve;

namespace {
constexpr int kCurveBlock = 128;

void setCurveTestAmount(AttenuverterModule& m, float amount) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(m.getParameters()[1]);
    p->setValueNotifyingHost(p->convertTo0to1(amount));
}

std::unique_ptr<AttenuverterModule> makeCurveTestModule(float amount, bool prepare = true) {
    auto m = std::make_unique<AttenuverterModule>();
    if (prepare)
        m->prepareToPlay(44100.0, kCurveBlock);
    setCurveTestAmount(*m, amount);
    return m;
}

// Runs enough blocks for the amount smoother to settle, returns the last one.
juce::AudioBuffer<float> runCurveBlocks(AttenuverterModule& m, const std::vector<float>& input, int blocks = 12) {
    juce::AudioBuffer<float> buffer(1, kCurveBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < kCurveBlock; ++i)
            buffer.setSample(0, i, input[(size_t)i % input.size()]);
        m.processBlock(buffer, midi);
    }
    return buffer;
}

float curveTableValue(const ModCurve& c, float input) {
    ModCurve::Table t;
    c.renderTable(t);
    return ModCurve::lookup(t, input);
}
} // namespace

TEST(AttenuverterCurveTest, LinearCurveIsSampleIdenticalToNoCurve) {
    auto plain = makeCurveTestModule(0.7f);
    auto linear = makeCurveTestModule(0.7f);
    linear->setCurve(ModCurve::preset(ModCurve::Preset::Exp));
    linear->setCurve(ModCurve::linear());

    std::vector<float> input;
    for (int i = 0; i < kCurveBlock; ++i)
        input.push_back(std::sin(0.1f * (float)i) * 1.3f);
    const auto a = runCurveBlocks(*plain, input);
    const auto b = runCurveBlocks(*linear, input);
    for (int i = 0; i < kCurveBlock; ++i)
        EXPECT_EQ(a.getSample(0, i), b.getSample(0, i)) << i;
}

TEST(AttenuverterCurveTest, ExpCurveMapsTheInputThenAmountScales) {
    const auto exp = ModCurve::preset(ModCurve::Preset::Exp);
    auto full = makeCurveTestModule(1.0f);
    full->setCurve(exp);
    EXPECT_NEAR(runCurveBlocks(*full, {0.5f}).getSample(0, kCurveBlock - 1), curveTableValue(exp, 0.5f), 1.0e-5f);

    auto half = makeCurveTestModule(0.5f);
    half->setCurve(exp);
    EXPECT_NEAR(runCurveBlocks(*half, {0.5f}).getSample(0, kCurveBlock - 1), 0.5f * curveTableValue(exp, 0.5f),
                1.0e-5f);
}

// What a listener hears: an LFO-rate sine through the curve, rendered block by block.
TEST(AttenuverterCurveTest, RenderedSineFollowsTheCurveAndStaysSmooth) {
    const auto render = [](const ModCurve& curve) {
        auto m = makeCurveTestModule(1.0f);
        m->setCurve(curve);
        runCurveBlocks(*m, {0.0f});
        std::vector<float> out;
        constexpr double kCyclesPerSample = 2.0 / 44100.0;
        juce::AudioBuffer<float> buffer(1, kCurveBlock);
        juce::MidiBuffer midi;
        for (int b = 0; b < 200; ++b) {
            for (int i = 0; i < kCurveBlock; ++i)
                buffer.setSample(
                    0, i,
                    (float)std::sin(juce::MathConstants<double>::twoPi * kCyclesPerSample * (b * kCurveBlock + i)));
            m->processBlock(buffer, midi);
            for (int i = 0; i < kCurveBlock; ++i)
                out.push_back(buffer.getSample(0, i));
        }
        return out;
    };
    const auto rms = [](const std::vector<float>& v) {
        double sum = 0.0;
        for (const auto x : v)
            sum += (double)x * x;
        return std::sqrt(sum / (double)v.size());
    };
    const auto largestStep = [](const std::vector<float>& v) {
        float step = 0.0f;
        for (size_t i = 1; i < v.size(); ++i)
            step = std::max(step, std::abs(v[i] - v[i - 1]));
        return step;
    };

    const auto linear = render(ModCurve::linear());
    const auto exp = render(ModCurve::preset(ModCurve::Preset::Exp));
    EXPECT_LT(rms(exp), rms(linear) * 0.9) << "an Exp curve spends most of the sweep lower";
    EXPECT_NEAR(*std::max_element(exp.begin(), exp.end()), *std::max_element(linear.begin(), linear.end()), 0.01f)
        << "the top of the sweep still reaches the same level";
    EXPECT_LT(largestStep(exp), 0.01f) << "no clicks from the table lookup";

    // A raised minimum jumps across zero by design: the output flips between -min and +min there, and nowhere else.
    auto raised = ModCurve::linear();
    raised.rangeMin = 0.2f;
    const auto withMin = render(raised);
    EXPECT_GT(largestStep(withMin), 0.3f);
    EXPECT_LT(largestStep(withMin), 0.45f);
}

TEST(AttenuverterCurveTest, ChangingTheCurveWhilePlayingStaysFinite) {
    auto m = makeCurveTestModule(1.0f);
    juce::AudioBuffer<float> buffer(1, kCurveBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < 64; ++b) {
        m->setCurve(ModCurve::preset(b % 2 == 0 ? ModCurve::Preset::Exp : ModCurve::Preset::Log));
        for (int i = 0; i < kCurveBlock; ++i)
            buffer.setSample(0, i, 0.5f);
        m->processBlock(buffer, midi);
        for (int i = 0; i < kCurveBlock; ++i) {
            ASSERT_TRUE(std::isfinite(buffer.getSample(0, i)));
            ASSERT_LE(std::abs(buffer.getSample(0, i)), 1.0f);
        }
    }
}

TEST(AttenuverterCurveTest, NegativeInputIsMirrored) {
    const auto exp = ModCurve::preset(ModCurve::Preset::Exp);
    auto m = makeCurveTestModule(1.0f);
    m->setCurve(exp);
    EXPECT_NEAR(runCurveBlocks(*m, {-0.5f}).getSample(0, kCurveBlock - 1), -curveTableValue(exp, 0.5f), 1.0e-5f);
}

TEST(AttenuverterCurveTest, BypassStillClears) {
    auto m = makeCurveTestModule(1.0f);
    m->setCurve(ModCurve::preset(ModCurve::Preset::Log));
    m->setBypassed(true);
    const auto out = runCurveBlocks(*m, {0.8f});
    EXPECT_EQ(out.getMagnitude(0, 0, kCurveBlock), 0.0f);
    EXPECT_EQ(m->getLastOutputPeak(), 0.0f);
}

TEST(AttenuverterCurveTest, PrepareToPlayKeepsTheCurve) {
    const auto log = ModCurve::preset(ModCurve::Preset::Log);
    auto m = makeCurveTestModule(1.0f);
    m->setCurve(log);
    m->prepareToPlay(48000.0, 256);
    EXPECT_EQ(m->getCurve(), log);
    EXPECT_NEAR(runCurveBlocks(*m, {0.5f}).getSample(0, kCurveBlock - 1), curveTableValue(log, 0.5f), 1.0e-5f);
}

TEST(AttenuverterCurveTest, SetCurveBeforeOrAfterPrepareTakesEffectWithinOneBlock) {
    const auto exp = ModCurve::preset(ModCurve::Preset::Exp);
    for (bool before : {true, false}) {
        auto m = makeCurveTestModule(1.0f, /*prepare=*/false);
        if (before)
            m->setCurve(exp);
        m->prepareToPlay(44100.0, kCurveBlock);
        if (!before)
            m->setCurve(exp);
        // One block, amount already at its target.
        const auto out = runCurveBlocks(*m, {0.5f}, 1);
        EXPECT_NEAR(out.getSample(0, kCurveBlock - 1), curveTableValue(exp, 0.5f), 1.0e-5f)
            << (before ? "before" : "after");
    }
}

TEST(AttenuverterCurveTest, AudioPathNeverAllocates) {
    auto m = makeCurveTestModule(0.8f);
    juce::AudioBuffer<float> buffer(1, kCurveBlock);
    juce::MidiBuffer midi;
    m->processBlock(buffer, midi); // warm up anything lazily created

    const ModCurve curves[] = {ModCurve::preset(ModCurve::Preset::Exp), ModCurve::preset(ModCurve::Preset::SCurve),
                               ModCurve::linear()};
    for (const auto& c : curves) {
        m->setCurve(c); // message thread: may allocate
        buffer.clear();
        for (int i = 0; i < kCurveBlock; ++i)
            buffer.setSample(0, i, 0.4f);
        synth::test::AllocationScope scope;
        m->processBlock(buffer, midi);
        EXPECT_EQ(scope.count(), 0);
    }
}
