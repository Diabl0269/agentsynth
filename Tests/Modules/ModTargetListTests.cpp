// ModTargetListTests.cpp
// A module's knob cables are declared once as a list; its input count, port labels, modulation targets and cable reads
// all follow from it. Pluck's list stands in for any module.

#include "Modules/ModTargetList.h"
#include "Modules/Pluck/PluckModule.h"
#include <gtest/gtest.h>

namespace {

constexpr int kN = (int)PluckModule::kModTargets.size();

juce::AudioBuffer<float> bufferWithCable(int channel, float value, int samples = 64) {
    juce::AudioBuffer<float> buffer(kN, samples);
    buffer.clear();
    juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), value, samples);
    return buffer;
}

} // namespace

TEST(ModTargetList, ChannelsLabelsAndTargetsAllDeriveFromTheList) {
    PluckModule pluck;
    EXPECT_EQ(pluck.getTotalNumInputChannels(), kN);
    const auto targets = pluck.getModulationTargets();
    ASSERT_EQ(targets.size(), (size_t)kN);
    for (int k = 0; k < kN; ++k) {
        SCOPED_TRACE(PluckModule::kModTargets[(size_t)k].paramId);
        const auto* param = findParameterByID(&pluck, PluckModule::kModTargets[(size_t)k].paramId);
        ASSERT_NE(param, nullptr);
        EXPECT_EQ(targets[(size_t)k].channelIndex, k);
        EXPECT_EQ(targets[(size_t)k].paramId, param->paramID);
        EXPECT_EQ(targets[(size_t)k].name, param->getName(64));
        EXPECT_TRUE(targets[(size_t)k].knobOnly);
        EXPECT_EQ(pluck.getInputPortLabel(k), param->getName(64));
        EXPECT_EQ(pluck.modulationChannelForParam(param->paramID), k);
    }
    EXPECT_EQ(pluck.getInputPortLabel(kN), "") << "a channel the list does not own has no label";
}

TEST(ModTargetList, ChannelOfParamOnAndReadOnAgree) {
    PluckModule pluck;
    synth::ModTargetList list{PluckModule::kModTargets};
    list.bind(pluck);
    EXPECT_EQ(list.size(), kN);
    EXPECT_EQ(list.endChannel(), kN);
    for (int k = 0; k < kN; ++k) {
        const auto id = juce::String(PluckModule::kModTargets[(size_t)k].paramId);
        EXPECT_EQ(list.channelOf(id), k);
        ASSERT_NE(list.paramOn(k), nullptr);
        EXPECT_EQ(list.paramOn(k)->paramID, id);
        EXPECT_EQ(list.readOn(k), PluckModule::kModTargets[(size_t)k].read);
        EXPECT_TRUE(list.ownsChannel(k));
    }
    EXPECT_EQ(list.channelOf("noSuchParam"), -1);
    EXPECT_EQ(list.paramOn(kN), nullptr);
    EXPECT_EQ(list.paramOn(-1), nullptr);
    EXPECT_EQ(list.readOn(kN), synth::ModRead::Live) << "an unowned channel reads live";
    EXPECT_FALSE(list.ownsChannel(kN));
}

TEST(ModTargetList, ValueAtStaysInTheKnobsRangeWhateverTheCableCarries) {
    PluckModule pluck;
    synth::ModTargetList list{PluckModule::kModTargets};
    list.bind(pluck);
    const int channel = list.channelOf("decay");
    const auto* decay = findParameterByID(&pluck, "decay");
    const float base = decay->convertFrom0to1(decay->getValue());
    const auto range = decay->getNormalisableRange();

    EXPECT_FLOAT_EQ(list.valueAt(bufferWithCable(channel, 0.0f), *decay, 10), base) << "no cable: the knob";
    EXPECT_FLOAT_EQ(list.valueAt(bufferWithCable(channel, 5.0f), *decay, 10), range.end) << "clamped at the top";
    EXPECT_FLOAT_EQ(list.valueAt(bufferWithCable(channel, -5.0f), *decay, 10), range.start) << "clamped at the bottom";
    const float half = list.valueAt(bufferWithCable(channel, 0.25f), *decay, 10);
    EXPECT_GT(half, base);
    EXPECT_LT(half, range.end);
    EXPECT_FLOAT_EQ(range.convertTo0to1(half), juce::jlimit(0.0f, 1.0f, range.convertTo0to1(base) + 0.25f))
        << "the cable moves the knob in its normalised range";
}

TEST(ModTargetList, ACableReadsZeroWhenItsChannelIsMissingFromTheBuffer) {
    PluckModule pluck;
    synth::ModTargetList list{PluckModule::kModTargets};
    list.bind(pluck);
    const auto* release = findParameterByID(&pluck, "release");
    ASSERT_GT(list.channelOf("release"), 2);
    juce::AudioBuffer<float> narrow(2, 32); // an engine layout carrying only the two audio channels
    narrow.clear();
    EXPECT_EQ(list.cvAt(narrow, *release, 5), 0.0f);
    EXPECT_FLOAT_EQ(list.valueAt(narrow, *release, 5), release->convertFrom0to1(release->getValue()));
    juce::AudioBuffer<float> empty(0, 0);
    EXPECT_EQ(list.cvAt(empty, *release, 0), 0.0f);
}

TEST(ModTargetList, ASampleIndexPastTheBlockReadsTheLastSample) {
    PluckModule pluck;
    synth::ModTargetList list{PluckModule::kModTargets};
    list.bind(pluck);
    const auto* tune = findParameterByID(&pluck, "tune");
    const int channel = list.channelOf("tune");
    auto buffer = bufferWithCable(channel, 0.0f, 16);
    buffer.setSample(channel, 15, 0.5f);
    EXPECT_EQ(list.cvAt(buffer, *tune, 16), 0.5f);
    EXPECT_EQ(list.cvAt(buffer, *tune, -3), 0.0f);
}
