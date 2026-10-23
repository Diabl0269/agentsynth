// FilterModule's Custom type: parameters and Morph CV reach the sound within two blocks, the
// shapes survive a save/load round trip, and projects saved before the type existed load and
// sound exactly as before.
//
// Raw channels are written as literals on purpose (they are part of the saved-patch contract):
// Morph X CV = ch28, Morph Y CV = ch29, in both voice modes.
#include "CustomFilterTestHelpers.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "Modules/FilterModule.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::test;

namespace {

constexpr int kBlock = 512;
constexpr int kChannels = 32;
constexpr int kMorphXCh = 28;
constexpr int kMorphYCh = 29;

juce::RangedAudioParameter* param(FilterModule& m, const juce::String& id) {
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id)
            return r;
    return nullptr;
}

void setValue(FilterModule& m, const juce::String& id, float plain) {
    auto* p = param(m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(plain));
}

/** Renders `blocks` blocks of white-ish noise (same seed every call) through a mono filter, calling
    `beforeBlock(blockIndex, buffer)` before each one; returns ch0 of every block, concatenated. */
template <typename Fn>
std::vector<float> render(FilterModule& m, int blocks, Fn&& beforeBlock) {
    m.prepareToPlay(kTestSampleRate, kBlock);
    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    juce::Random rng(1234);
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        for (int i = 0; i < kBlock; ++i)
            buf.setSample(0, i, rng.nextFloat() - 0.5f);
        beforeBlock(b, buf);
        m.processBlock(buf, midi);
        out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
    }
    return out;
}

std::vector<float> render(FilterModule& m, int blocks) {
    return render(m, blocks, [](int, juce::AudioBuffer<float>&) {});
}

float maxDiff(const std::vector<float>& a, const std::vector<float>& b, int fromBlock, int toBlock) {
    float m = 0.0f;
    for (int n = fromBlock * kBlock; n < toBlock * kBlock; ++n)
        m = std::max(m, std::abs(a[(size_t)n] - b[(size_t)n]));
    return m;
}

CustomFilterShapes fiveShapes() {
    CustomFilterShapes s;
    s.shapes = {makeShape("one", {{20.0f, -6.0f}, {20000.0f, -20.0f}}, {0.1f, 0.2f}),
                makeShape("two", {{20.0f, -20.0f}, {1000.0f, 0.0f}, {20000.0f, -20.0f}}, {0.9f, 0.2f}),
                makeShape("three", {{20.0f, -30.0f}, {20000.0f, 0.0f}}, {0.5f, 0.5f, 0.75f}),
                makeShape("four", {{20.0f, 0.0f}, {300.0f, -24.0f}, {3000.0f, 0.0f}, {20000.0f, -10.0f}}, {0.1f, 0.9f}),
                makeShape("five", {{20.0f, -12.0f}, {20000.0f, -12.0f}}, {0.9f, 0.9f, 0.0f})};
    s.shapes[1].curve.points[1].bend = 0.4f;
    return s;
}

} // namespace

TEST(FilterCustom, CustomIsAppendedLastAndOldIndicesAreUnchanged) {
    FilterModule m;
    auto* type = dynamic_cast<juce::AudioParameterChoice*>(param(m, "filterType"));
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->choices,
              juce::StringArray({"LPF24", "LPF12", "HPF24", "HPF12", "BPF24", "BPF12", "Notch", "Custom"}));
    EXPECT_EQ(type->getIndex(), 0) << "default stays LPF24";
}

TEST(FilterCustom, CustomTypeFiltersTheSignal) {
    FilterModule custom;
    setValue(custom, "filterType", (float)FilterModule::kCustomTypeIndex);
    const auto out = render(custom, 8);
    juce::Random rng(1234); // render()'s input, regenerated
    float peak = 0.0f, diff = 0.0f;
    for (int n = 0; n < 8 * kBlock; ++n) {
        const float in = rng.nextFloat() - 0.5f;
        peak = std::max(peak, std::abs(out[(size_t)n]));
        diff = std::max(diff, std::abs(out[(size_t)n] - in));
    }
    EXPECT_GT(peak, 1.0e-2f) << "the vowel default passes sound";
    EXPECT_GT(diff, 1.0e-2f) << "and changes it";
    EXPECT_GT(custom.getCustomDesignCount(), 0);
}

TEST(FilterCustom, MorphXParameterStepShowsUpWithinTwoBlocks) {
    constexpr int kStep = 6, kBlocks = 12;
    FilterModule stepped, reference;
    for (auto* m : {&stepped, &reference}) {
        setValue(*m, "filterType", (float)FilterModule::kCustomTypeIndex);
        setValue(*m, "morphY", 0.25f);
    }
    setValue(stepped, "morphX", 0.2f);   // "ah"
    setValue(reference, "morphX", 0.8f); // "ee" from the start
    const auto ref = render(reference, kBlocks);
    const auto out = render(stepped, kBlocks, [&](int b, juce::AudioBuffer<float>&) {
        if (b == kStep)
            setValue(stepped, "morphX", 0.8f);
    });
    EXPECT_GT(maxDiff(out, ref, 0, kStep), 1.0e-2f) << "before the step the two differ";
    EXPECT_LT(maxDiff(out, ref, kStep + 1, kBlocks), 1.0e-4f)
        << "by the block after the step the output is the new setting's, sample for sample";
}

TEST(FilterCustom, MorphCvStepShowsUpWithinTwoBlocks) {
    constexpr int kStep = 6, kBlocks = 12;
    for (int cvCh : {kMorphXCh, kMorphYCh}) {
        FilterModule stepped, reference;
        for (auto* m : {&stepped, &reference}) {
            setValue(*m, "filterType", (float)FilterModule::kCustomTypeIndex);
            setValue(*m, "morphX", 0.2f);
            setValue(*m, "morphY", 0.25f);
        }
        // +0.6 CV moves the cursor 0.6 of the range: to "ee" (x) or towards "oo" (y).
        setValue(reference, cvCh == kMorphXCh ? "morphX" : "morphY", cvCh == kMorphXCh ? 0.8f : 0.85f);
        const auto ref = render(reference, kBlocks);
        const auto out = render(stepped, kBlocks, [&](int b, juce::AudioBuffer<float>& buf) {
            if (b >= kStep)
                for (int i = 0; i < kBlock; ++i)
                    buf.setSample(cvCh, i, 0.6f);
        });
        EXPECT_GT(maxDiff(out, ref, 0, kStep), 1.0e-2f) << "ch" << cvCh;
        EXPECT_LT(maxDiff(out, ref, kStep + 1, kBlocks), 1.0e-4f) << "ch" << cvCh;
    }
}

TEST(FilterCustom, MorphJacksAreModulationTargetsOnAppendedChannels) {
    FilterModule m;
    for (bool poly : {false, true}) {
        setValue(m, "poly", poly ? 1.0f : 0.0f);
        bool x = false, y = false;
        for (const auto& t : m.getModulationTargets()) {
            x |= (t.paramId == "morphX" && t.channelIndex == kMorphXCh);
            y |= (t.paramId == "morphY" && t.channelIndex == kMorphYCh);
        }
        EXPECT_TRUE(x && y) << (poly ? "poly" : "mono");
        EXPECT_EQ(m.mapInputChannel(kMorphXCh).role, PortRole::ModCV);
        EXPECT_EQ(m.mapInputChannel(kMorphYCh).visibleJackIndex, m.mapInputChannel(kMorphXCh).visibleJackIndex + 1);
        EXPECT_EQ(m.getInputPortLabel(m.mapInputChannel(kMorphXCh).visibleJackIndex), "Morph X");
        EXPECT_EQ(m.getInputPortLabel(m.mapInputChannel(kMorphYCh).visibleJackIndex), "Morph Y");
    }
    EXPECT_EQ(m.getTotalNumInputChannels(), kChannels);
}

TEST(FilterCustom, ExtraStateRoundTripsFiveShapesWithVectorPositions) {
    FilterModule a;
    EXPECT_TRUE(a.getExtraState().isVoid()) << "default shapes add nothing to a saved project";
    a.setCustomShapes(fiveShapes());
    const juce::var state = a.getExtraState();
    ASSERT_TRUE(state.isObject());

    // Through JSON text, as a project file stores it.
    const juce::var reparsed = juce::JSON::parse(juce::JSON::toString(state));
    FilterModule b;
    b.setExtraState(reparsed);
    const auto& got = b.getCustomShapes();
    ASSERT_EQ((int)got.shapes.size(), 5);
    EXPECT_TRUE(got == a.getCustomShapes());
    EXPECT_EQ(got.shapes[2].position, (std::vector<float>{0.5f, 0.5f, 0.75f})) << "a third coordinate survives";
    EXPECT_EQ(got.shapes[1].name, "two");

    // And it sounds the same.
    for (auto* m : {&a, &b})
        setValue(*m, "filterType", (float)FilterModule::kCustomTypeIndex);
    EXPECT_LT(maxDiff(render(a, 6), render(b, 6), 0, 6), 1.0e-6f);
}

TEST(FilterCustom, GarbageStateFallsBackSafely) {
    FilterModule m;
    m.setExtraState(juce::JSON::parse(R"({"version": 99, "shapes": []})"));
    EXPECT_TRUE(m.getCustomShapes().isDefault());
    m.setExtraState(juce::JSON::parse(R"({"version": 1, "shapes": []})"));
    EXPECT_TRUE(m.getCustomShapes().isDefault()) << "never zero shapes";
    m.setExtraState(juce::JSON::parse(R"({"version": 1, "shapes": [{"name": "x", "position": [7, -3, "a"]}]})"));
    ASSERT_EQ((int)m.getCustomShapes().shapes.size(), 1);
    EXPECT_EQ(m.getCustomShapes().shapes[0].position, (std::vector<float>{1.0f, 0.0f, 0.5f}));
    EXPECT_TRUE(m.getCustomShapes().shapes[0].curve == CustomFilterShapes::flatCurve());
}

TEST(FilterCustom, OldProjectsLoadAndSoundExactlyAsBefore) {
    // A host-state blob written before Custom existed: filterType saved as a normalised value over
    // seven choices and no choice name. 1.0 was Notch; over eight choices it would read as Custom.
    juce::ValueTree state("ModuleState");
    state.setProperty("filterType", 1.0f, nullptr);
    state.setProperty("cutoff", 0.3f, nullptr);
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), blob);

    FilterModule loaded;
    loaded.setStateInformation(blob.getData(), (int)blob.getSize());
    EXPECT_EQ(loaded.getCurrentFilterType(), 6) << "still Notch";

    FilterModule fresh;
    setValue(fresh, "filterType", 6.0f);
    param(fresh, "cutoff")->setValueNotifyingHost(0.3f);
    EXPECT_EQ(maxDiff(render(loaded, 6), render(fresh, 6), 0, 6), 0.0f) << "bit-identical to a filter set up directly";
    EXPECT_TRUE(loaded.getExtraState().isVoid());

    // Every old index still renders through the ladder/notch path: identical with or without an
    // empty Custom state applied, and untouched by the Custom-only parameters.
    for (int type = 0; type < 7; ++type) {
        FilterModule plain, touched;
        setValue(plain, "filterType", (float)type);
        setValue(touched, "filterType", (float)type);
        touched.setExtraState({});
        setValue(touched, "morphX", 0.9f);
        setValue(touched, "shift", 24.0f);
        setValue(touched, "depth", 0.0f);
        EXPECT_EQ(maxDiff(render(plain, 4), render(touched, 4), 0, 4), 0.0f) << "type " << type;
    }
}

TEST(FilterCustom, PolyVoicesAndRightLegShareTheKernel) {
    FilterModule m;
    setValue(m, "filterType", (float)FilterModule::kCustomTypeIndex);
    setValue(m, "poly", 1.0f);
    m.prepareToPlay(kTestSampleRate, kBlock);
    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < 4; ++b) {
        buf.clear();
        for (int i = 0; i < kBlock; ++i) {
            const float x = std::sin(0.07f * (float)(b * kBlock + i));
            buf.setSample(0, i, x);                            // voice 0, L
            buf.setSample(5, i, x);                            // voice 5, L
            buf.setSample(FilterModule::kRightBase + 5, i, x); // voice 5, R
        }
        m.processBlock(buf, midi);
    }
    for (int i = 0; i < kBlock; ++i) {
        EXPECT_FLOAT_EQ(buf.getSample(5, i), buf.getSample(0, i));
        EXPECT_FLOAT_EQ(buf.getSample(FilterModule::kRightBase + 5, i), buf.getSample(0, i));
        EXPECT_EQ(buf.getSample(3, i), 0.0f) << "a silent voice stays silent";
    }
}

TEST(FilterCustomModuleText, ShiftAndDepthShowUnitsAndRoundTrip) {
    FilterModule m;
    auto* shift = param(m, "shift");
    auto* depth = param(m, "depth");
    ASSERT_NE(shift, nullptr);
    ASSERT_NE(depth, nullptr);
    EXPECT_EQ(shift->getText(shift->convertTo0to1(0.0f), 0), "0.0 st");
    EXPECT_EQ(shift->getText(shift->convertTo0to1(-0.004f), 0), "0.0 st") << "never negative zero";
    EXPECT_EQ(shift->getText(shift->convertTo0to1(-12.0f), 0), "-12.0 st");
    EXPECT_NEAR(shift->convertFrom0to1(shift->getValueForText("-12.0 st")), -12.0f, 1e-3f);
    EXPECT_EQ(depth->getText(depth->convertTo0to1(1.0f), 0), "100%");
    EXPECT_NEAR(depth->convertFrom0to1(depth->getValueForText("100%")), 1.0f, 1e-3f);
    EXPECT_NEAR(depth->convertFrom0to1(depth->getValueForText("50")), 0.5f, 1e-3f);
}

TEST(FilterCustomShapePresets, NoTwoPointsNearlyOverlap) {
    // x is log frequency over 10 octaves: 0.02 is about a fifth of an octave. A vowel's formants are 100-250 Hz wide
    // (about 0.01 of x at 1 kHz), so their points sit closer than the animal calls' broad humps.
    for (int i = 0; i < CustomFilterShapes::kNumPresets; ++i) {
        const auto preset = static_cast<CustomFilterShapes::Preset>(i);
        const auto curve = CustomFilterShapes::presetCurve(preset);
        const float minGap = i <= (int)CustomFilterShapes::Preset::Ih ? 0.004f : 0.02f;
        for (size_t p = 1; p < curve.points.size(); ++p)
            EXPECT_GE(curve.points[p].x - curve.points[p - 1].x, minGap)
                << CustomFilterShapes::presetName(preset) << " point " << p;
    }
}
