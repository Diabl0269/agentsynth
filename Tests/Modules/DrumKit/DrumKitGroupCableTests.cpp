// DrumKitGroupCableTests.cpp
// Every group knob (kickDecay, snareTune, closedDecay ...) takes a cable. It is read on the sample a pad of its group
// is struck and applied on top of that pad's own value for that hit only: it never writes the group knob or a pad, so a
// saved project is the same with or without a cable, and a hit already ringing does not follow a cable that moves.
#include "Modules/DrumKit/DrumKitModule.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <gtest/gtest.h>

namespace {

constexpr double kCableRate = 44100.0;
constexpr int kCableBlock = 256;
constexpr int kKick = 36, kSnare = 38, kClosedHat = 42, kOpenHat = 46;

struct Plan {
    int note = kSnare;
    int hitAt = 100;                                         // absolute sample
    std::function<float(int)> cv = [](int) { return 0.0f; }; // the cable's value at an absolute sample
    juce::String paramId;                                    // the group knob the cable is on
    double seconds = 1.0;
};

std::unique_ptr<DrumKitModule> makeCableKit() {
    auto kit = std::make_unique<DrumKitModule>();
    kit->prepareToPlay(kCableRate, kCableBlock);
    return kit;
}

std::vector<float> render(DrumKitModule& kit, const Plan& plan) {
    const int channel = plan.paramId.isEmpty() ? 0 : kit.modulationChannelForParam(plan.paramId);
    const int total = (int)(plan.seconds * kCableRate);
    std::vector<float> out;
    for (int start = 0; start < total; start += kCableBlock) {
        const int n = std::min(kCableBlock, total - start);
        juce::AudioBuffer<float> block(kit.getTotalNumInputChannels(), n);
        block.clear();
        if (plan.paramId.isNotEmpty())
            for (int i = 0; i < n; ++i)
                block.setSample(channel, i, plan.cv(start + i));
        juce::MidiBuffer midi;
        if (plan.hitAt >= start && plan.hitAt < start + n)
            midi.addEvent(juce::MidiMessage::noteOn(10, plan.note, (juce::uint8)110), plan.hitAt - start);
        kit.processBlock(block, midi);
        out.insert(out.end(), block.getReadPointer(0), block.getReadPointer(0) + n);
    }
    return out;
}

std::vector<float> renderConstant(const juce::String& paramId, int note, float cv) {
    auto kit = makeCableKit();
    Plan plan;
    plan.note = note;
    plan.paramId = paramId;
    plan.cv = [cv](int) { return cv; };
    return render(*kit, plan);
}

// Seconds from the hit until the last sample above 1% of the hit's peak.
double ringSeconds(const std::vector<float>& s, int hitAt) {
    float peak = 0.0f;
    for (float v : s)
        peak = std::max(peak, std::fabs(v));
    size_t last = (size_t)hitAt;
    for (size_t i = (size_t)hitAt; i < s.size(); ++i)
        if (std::fabs(s[i]) > 0.01f * peak)
            last = i;
    return (double)(last - (size_t)hitAt) / kCableRate;
}

int zeroCrossings(const std::vector<float>& s, size_t from, size_t to) {
    int count = 0;
    for (size_t i = from + 1; i < std::min(to, s.size()); ++i)
        count += (s[i - 1] < 0.0f) != (s[i] < 0.0f) ? 1 : 0;
    return count;
}

double rms(const std::vector<float>& s) {
    double sum = 0.0;
    for (float v : s)
        sum += (double)v * v;
    return s.empty() ? 0.0 : std::sqrt(sum / (double)s.size());
}

bool sameSamples(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0) {
    if (a.size() != b.size())
        return false;
    for (size_t i = from; i < a.size(); ++i)
        if (a[i] != b[i])
            return false;
    return true;
}

} // namespace

TEST(DrumKitGroupCable, EveryGroupKnobIsATargetOnItsOwnChannelAfterLevel) {
    DrumKitModule kit;
    const auto targets = kit.getModulationTargets();
    ASSERT_EQ(targets.size(), DrumKitModule::kModTargets.size());
    EXPECT_EQ(targets[0].paramId, "level");
    EXPECT_EQ(targets[0].channelIndex, 0);
    EXPECT_EQ(targets[1].paramId, "kickLevel");
    EXPECT_EQ(targets[1].channelIndex, 1);
    EXPECT_EQ(targets.back().paramId, "cowbellDecay");
    EXPECT_EQ(kit.getTotalNumInputChannels(), DrumKitModule::kPadModSlotBase + DrumKitModule::kMaxPadModSlots);
    EXPECT_EQ(kit.getInputPortLabel(DrumKitModule::kPadModSlotBase), "") << "the pad slots are not labelled yet";
    for (const auto& t : targets) {
        SCOPED_TRACE(t.paramId.toStdString());
        EXPECT_TRUE(t.knobOnly);
        ASSERT_NE(kit.parameterForModTarget(t), nullptr);
        EXPECT_EQ(kit.parameterForModTarget(t)->paramID, t.paramId);
    }
}

TEST(DrumKitGroupCable, ACableOnSnareDecayLengthensASnareHitAndLeavesAKickAlone) {
    const auto plain = renderConstant({}, kSnare, 0.0f);
    const auto longer = renderConstant("snareDecay", kSnare, 0.5f);
    const double without = ringSeconds(plain, 100), with = ringSeconds(longer, 100);
    std::printf("[measured] snare rings %.3f s without the snareDecay cable, %.3f s with +0.5\n", without, with);
    ASSERT_GT(rms(plain), 1.0e-3);
    EXPECT_GT(with, without * 1.5);

    const auto kick = renderConstant({}, kKick, 0.0f);
    EXPECT_TRUE(sameSamples(kick, renderConstant("snareDecay", kKick, 0.5f))) << "a kick hit does not hear the snare";
    EXPECT_TRUE(sameSamples(kick, renderConstant("kickLevel", kKick, 0.0f)));
}

TEST(DrumKitGroupCable, ACableOnKickTuneMovesTheKicksPitch) {
    const auto plain = renderConstant({}, kKick, 0.0f);
    const auto up = renderConstant("kickTune", kKick, 0.5f);
    const size_t from = 100 + (size_t)(0.01 * kCableRate), to = 100 + (size_t)(0.12 * kCableRate);
    const int without = zeroCrossings(plain, from, to), with = zeroCrossings(up, from, to);
    std::printf("[measured] kick zero crossings in 10-120 ms: %d without the kickTune cable, %d with +0.5\n", without,
                with);
    EXPECT_GT(with, without * 1.3);
}

TEST(DrumKitGroupCable, ALevelCableOnAGroupScalesJustThatGroup) {
    const auto plain = renderConstant({}, kKick, 0.0f);
    const auto quieter = renderConstant("kickLevel", kKick, -0.45f);
    EXPECT_LT(rms(quieter), rms(plain) * 0.7);
    EXPECT_TRUE(sameSamples(renderConstant({}, kSnare, 0.0f), renderConstant("kickLevel", kSnare, -0.45f)));
}

TEST(DrumKitGroupCable, TheClosedDecayCableReachesTheClosedHatAndNotTheOpenHat) {
    EXPECT_FALSE(sameSamples(renderConstant({}, kClosedHat, 0.0f), renderConstant("closedDecay", kClosedHat, 0.5f)));
    EXPECT_TRUE(sameSamples(renderConstant({}, kOpenHat, 0.0f), renderConstant("closedDecay", kOpenHat, 0.5f)));
    EXPECT_FALSE(sameSamples(renderConstant({}, kOpenHat, 0.0f), renderConstant("openDecay", kOpenHat, 0.5f)));
    EXPECT_TRUE(sameSamples(renderConstant({}, kClosedHat, 0.0f), renderConstant("openDecay", kClosedHat, 0.5f)));
}

TEST(DrumKitGroupCable, ACableNeverWritesAKnobAPadOrTheSavedState) {
    auto untouched = makeCableKit();
    auto cabled = makeCableKit();
    for (const auto& spec : DrumKitModule::kModTargets) {
        const juce::String id = spec.paramId;
        Plan plan;
        plan.note = id.startsWith("kick") ? kKick : id.startsWith("snare") ? kSnare : kClosedHat;
        plan.paramId = id;
        plan.cv = [](int) { return 0.6f; };
        plan.seconds = 0.3;
        render(*cabled, plan);
    }
    ASSERT_EQ(untouched->getParameters().size(), cabled->getParameters().size());
    for (int i = 0; i < untouched->getParameters().size(); ++i)
        EXPECT_EQ(untouched->getParameters()[i]->getValue(), cabled->getParameters()[i]->getValue())
            << untouched->getParameters()[i]->getName(64);
    EXPECT_TRUE(cabled->getExtraState().isVoid()) << "an untouched kit still saves no pad state";
    EXPECT_EQ(juce::JSON::toString(untouched->getExtraState()), juce::JSON::toString(cabled->getExtraState()));
}

TEST(DrumKitGroupCable, AHitAlreadyRingingIgnoresACableThatMovesAfterIt) {
    constexpr int kSwitch = 3000;
    auto steadyKit = makeCableKit();
    Plan steady;
    steady.paramId = "snareDecay";
    const auto a = render(*steadyKit, steady);
    auto switchedKit = makeCableKit();
    Plan switched = steady;
    switched.cv = [](int i) { return i < kSwitch ? 0.0f : 1.0f; };
    const auto b = render(*switchedKit, switched);
    EXPECT_TRUE(sameSamples(a, b)) << "the hit took its decay when it was struck";
}

TEST(DrumKitGroupCable, AHitReadsItsCableOnItsOwnSampleNotTheBlocksFirst) {
    constexpr int kHitAt = 200;
    const auto ramp = [](int i) { return i < kCableBlock ? (float)i / (float)(kCableBlock - 1) : 0.0f; };
    const float atHit = ramp(kHitAt);
    auto rampKit = makeCableKit();
    Plan rising;
    rising.paramId = "snareDecay";
    rising.hitAt = kHitAt;
    rising.cv = ramp;
    auto heldKit = makeCableKit();
    Plan held = rising;
    held.cv = [atHit](int i) { return i < kCableBlock ? atHit : 0.0f; };
    EXPECT_TRUE(sameSamples(render(*rampKit, rising), render(*heldKit, held)));
}

TEST(DrumKitGroupCable, ACardAuditionStrikesWithTheCableAtTheBlocksFirstSample) {
    const auto strike = [](float cv) {
        auto kit = makeCableKit();
        int pad = -1;
        for (int i = 0; i < 32; ++i)
            if (kit->pad(i).note == kSnare)
                pad = i;
        EXPECT_GE(pad, 0);
        kit->auditionPad(pad);
        const int channel = kit->modulationChannelForParam("snareDecay");
        std::vector<float> out;
        for (int start = 0; start < (int)kCableRate; start += kCableBlock) {
            juce::AudioBuffer<float> block(kit->getTotalNumInputChannels(), kCableBlock);
            block.clear();
            juce::FloatVectorOperations::fill(block.getWritePointer(channel), cv, kCableBlock);
            juce::MidiBuffer midi;
            kit->processBlock(block, midi);
            out.insert(out.end(), block.getReadPointer(0), block.getReadPointer(0) + kCableBlock);
        }
        return out;
    };
    const auto plain = strike(0.0f), longer = strike(0.5f);
    EXPECT_GT(ringSeconds(longer, 0), ringSeconds(plain, 0) * 1.5);
}
