// LfoShapeMorphTests.cpp -- the LFO's Custom wave as a list of shapes on a morph map: one shape plays and saves
// exactly as the plain wave did, two blend by Morph X / Y, old saved state loads as one shape, the audio thread never
// allocates when the cursor moves or a list is swapped in, and the state round-trips.
#include "../Filter/AllocationCounter.h"
#include "Modules/Filter/CustomFilterEngine.h"
#include "Modules/LFOModule.h"
#include "Modules/Lfo/LfoCustomShapes.h"
#include "Modules/Lfo/LfoShapeBlender.h"
#include <cmath>
#include <gtest/gtest.h>

using synth::LfoCustomShapes;
using synth::LfoCustomWave;
using synth::LfoShapeBlender;
using synth::test::AllocationScope;

namespace {

juce::AudioProcessorParameter* findParam(LFOModule& lfo, const juce::String& paramId) {
    for (auto* p : lfo.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (withId->paramID == paramId)
                return p;
    return nullptr;
}

void setNormalised(LFOModule& lfo, const juce::String& id, float plain) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParam(lfo, id));
    p->setValueNotifyingHost(p->convertTo0to1(plain));
}

/** A Custom-shape LFO running free at 5 Hz, unipolar, so the output is the blended table's own y. */
void setUpCustom(LFOModule& lfo) {
    *dynamic_cast<juce::AudioParameterChoice*>(findParam(lfo, "shape")) = LFOModule::kCustomShapeIndex;
    *dynamic_cast<juce::AudioParameterBool*>(findParam(lfo, "mode")) = false;
    *dynamic_cast<juce::AudioParameterBool*>(findParam(lfo, "bipolar")) = false;
    setNormalised(lfo, "rateHz", 5.0f);
}

std::vector<float> render(LFOModule& lfo, int blocks, int blockSize = 256) {
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> buffer(LFOModule::kNumInputs, blockSize);
        buffer.clear();
        juce::MidiBuffer midi;
        lfo.processBlock(buffer, midi);
        for (int i = 0; i < blockSize; ++i)
            out.push_back(buffer.getSample(0, i));
    }
    return out;
}

LfoCustomShapes twoShapes() {
    LfoCustomShapes list;
    list.shapes.push_back({"low", LfoCustomWave::preset(LfoCustomWave::Preset::RampUp), {0.1f, 0.5f}});
    list.shapes.push_back({"high", LfoCustomWave::preset(LfoCustomWave::Preset::RampDown), {0.9f, 0.5f}});
    return list;
}

} // namespace

TEST(LfoShapeMorph, OneShapeIsBitIdenticalToThePlainWaveAtAnyCursor) {
    const auto wave = LfoCustomWave::preset(LfoCustomWave::Preset::SoftSine);
    LFOModule plain;
    plain.prepareToPlay(44100.0, 256);
    setUpCustom(plain);
    plain.setCustomWave(wave);
    const auto expected = render(plain, 8);

    LFOModule shaped;
    shaped.prepareToPlay(44100.0, 256);
    setUpCustom(shaped);
    LfoCustomShapes one;
    one.shapes.push_back({"only", wave, {0.2f, 0.9f}});
    shaped.setCustomShapes(one);
    setNormalised(shaped, "morphX", 0.9f);
    setNormalised(shaped, "morphY", 0.1f);
    EXPECT_EQ(render(shaped, 8), expected);

    // And the table itself is the wave's own render, not a blended near-copy.
    LfoShapeBlender blender;
    blender.setShapes(LfoCustomShapes::single(wave));
    LfoCustomWave::Table table{};
    wave.renderTable(table);
    EXPECT_EQ(blender.blend(0.3f, 0.7f), table);
}

TEST(LfoShapeMorph, TwoShapesBlendByMorphX) {
    LFOModule lfo;
    lfo.prepareToPlay(44100.0, 256);
    setUpCustom(lfo);
    setNormalised(lfo, "rateHz", 0.01f); // phase stays near 0 for a block
    lfo.setCustomShapes(twoShapes());    // RampUp starts at y = 0, RampDown at y = 1

    setNormalised(lfo, "morphX", 0.1f);
    const float atLow = render(lfo, 1, 1)[0];
    setNormalised(lfo, "morphX", 0.9f);
    const float atHigh = render(lfo, 1, 1)[0];
    setNormalised(lfo, "morphX", 0.5f);
    const float atMid = render(lfo, 1, 1)[0];

    EXPECT_LT(atLow, 0.05f) << "on the RampUp shape: its own first value";
    EXPECT_GT(atHigh, 0.95f) << "on the RampDown shape";
    EXPECT_NEAR(atMid, 0.5f, 0.05f) << "halfway: both shapes";
}

TEST(LfoShapeMorph, MorphCvMovesTheBlend) {
    LFOModule lfo;
    lfo.prepareToPlay(44100.0, 256);
    setUpCustom(lfo);
    setNormalised(lfo, "rateHz", 0.01f);
    lfo.setCustomShapes(twoShapes());
    setNormalised(lfo, "morphX", 0.1f);

    juce::AudioBuffer<float> buffer(LFOModule::kNumInputs, 1);
    buffer.clear();
    for (int i = 0; i < 1; ++i)
        buffer.setSample(LFOModule::kMorphXChannel, i, 0.8f); // CV in normalised units: 0.1 -> 0.9
    juce::MidiBuffer midi;
    lfo.processBlock(buffer, midi);
    EXPECT_GT(buffer.getSample(0, 0), 0.95f);
}

TEST(LfoShapeMorph, MorphTargetsAreKnobOnlyAndAutomatable) {
    LFOModule lfo;
    int found = 0;
    for (const auto& t : lfo.getModulationTargets()) {
        if (t.paramId == "morphX" || t.paramId == "morphY") {
            EXPECT_TRUE(t.knobOnly) << t.name;
            ++found;
        }
    }
    EXPECT_EQ(found, 2);
    for (const char* id : {"morphX", "morphY"}) {
        auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParam(lfo, id));
        ASSERT_NE(p, nullptr);
        EXPECT_FLOAT_EQ(p->get(), 0.5f);
        EXPECT_TRUE(p->isAutomatable());
        EXPECT_FLOAT_EQ(p->getNormalisableRange().start, 0.0f);
        EXPECT_FLOAT_EQ(p->getNormalisableRange().end, 1.0f);
    }
}

TEST(LfoShapeMorph, OldBareWaveStateLoadsAsOneShapeAndSavesTheSameWay) {
    const auto wave = LfoCustomWave::preset(LfoCustomWave::Preset::Square);
    const juce::var legacy = wave.toVar(); // what every LFO saved before shapes existed
    LFOModule lfo;
    lfo.setExtraState(legacy);
    ASSERT_EQ(lfo.getCustomShapes().shapes.size(), 1u);
    EXPECT_EQ(lfo.getCustomWave(), wave);
    EXPECT_EQ(juce::JSON::toString(lfo.getExtraState()), juce::JSON::toString(legacy));
}

TEST(LfoShapeMorph, DefaultStateStillSavesNothing) {
    LFOModule lfo;
    EXPECT_TRUE(lfo.getExtraState().isVoid());
    lfo.setCustomShapes(twoShapes());
    EXPECT_FALSE(lfo.getExtraState().isVoid());
}

TEST(LfoShapeMorph, ShapeListStateRoundTrips) {
    LFOModule a;
    auto list = twoShapes();
    list.shapes.push_back({"mid", LfoCustomWave::preset(LfoCustomWave::Preset::Pulse), {0.5f, 0.9f}});
    a.setCustomShapes(list);
    const auto saved = a.getExtraState();
    ASSERT_TRUE(LfoCustomShapes::isShapeListVar(saved));

    LFOModule b;
    b.setExtraState(juce::JSON::parse(juce::JSON::toString(saved)));
    EXPECT_EQ(b.getCustomShapes(), a.getCustomShapes());
    ASSERT_EQ(b.getCustomShapes().shapes.size(), 3u);
    EXPECT_EQ(b.getCustomShapes().shapes[2].name, "mid");
    EXPECT_FLOAT_EQ(b.getCustomShapes().shapes[2].position[1], 0.9f);
}

TEST(LfoShapeMorph, ASingleNamedOrMovedShapeKeepsItsNameAndPosition) {
    LfoCustomShapes list;
    list.shapes.push_back({"solo", LfoCustomWave::preset(LfoCustomWave::Preset::RampUp), {0.2f, 0.8f}});
    const auto back = LfoCustomShapes::fromVar(juce::JSON::parse(juce::JSON::toString(list.toVar())));
    EXPECT_EQ(back, list);
}

TEST(LfoShapeMorph, GarbageStateResolvesToTheDefaultShape) {
    EXPECT_TRUE(LfoCustomShapes::fromVar(juce::var("nope")).isDefault());
    auto* future = new juce::DynamicObject();
    future->setProperty("version", 99);
    future->setProperty("shapes", juce::Array<juce::var>{});
    EXPECT_TRUE(LfoCustomShapes::fromVar(juce::var(future)).isDefault());
    auto* empty = new juce::DynamicObject();
    empty->setProperty("version", 1);
    empty->setProperty("shapes", juce::Array<juce::var>{});
    EXPECT_TRUE(LfoCustomShapes::fromVar(juce::var(empty)).isDefault());
}

TEST(LfoShapeMorph, SetCustomWaveEditsTheFirstShapeAndKeepsTheRest) {
    LFOModule lfo;
    lfo.setCustomShapes(twoShapes());
    lfo.setCustomWave(LfoCustomWave::preset(LfoCustomWave::Preset::Square));
    ASSERT_EQ(lfo.getCustomShapes().shapes.size(), 2u);
    EXPECT_EQ(lfo.getCustomWave(), LfoCustomWave::preset(LfoCustomWave::Preset::Square));
    EXPECT_EQ(lfo.getCustomShapes().shapes[1].name, "high");
}

TEST(LfoShapeMorph, GenerationBumpsOnEveryShapeListChange) {
    LFOModule lfo;
    const int g0 = lfo.getCustomWaveGeneration();
    lfo.setCustomShapes(twoShapes());
    EXPECT_GT(lfo.getCustomWaveGeneration(), g0);
}

TEST(LfoShapeMorph, TheAudioThreadDoesNotAllocateWhenTheCursorMovesOrAListIsSwappedIn) {
    LFOModule lfo;
    lfo.prepareToPlay(44100.0, 256);
    setUpCustom(lfo);
    auto list = twoShapes();
    list.shapes.push_back({"third", LfoCustomWave::preset(LfoCustomWave::Preset::Pulse), {0.5f, 0.9f}});
    lfo.setCustomShapes(list);
    render(lfo, 1); // adopt, and warm up outside the scope

    auto* morphX = dynamic_cast<juce::AudioParameterFloat*>(findParam(lfo, "morphX"));
    auto* morphY = dynamic_cast<juce::AudioParameterFloat*>(findParam(lfo, "morphY"));
    juce::AudioBuffer<float> buffer(LFOModule::kNumInputs, 256);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < 32; ++blk) {
        morphX->setValueNotifyingHost(0.5f + 0.5f * std::sin(0.4f * (float)blk));
        morphY->setValueNotifyingHost(0.5f + 0.5f * std::cos(0.3f * (float)blk));
        buffer.clear();
        AllocationScope scope;
        lfo.processBlock(buffer, midi);
        EXPECT_EQ(scope.count(), 0) << "block " << blk;
    }

    lfo.setCustomShapes(twoShapes()); // message thread: allocates the new tables
    buffer.clear();
    AllocationScope scope;
    lfo.processBlock(buffer, midi);
    EXPECT_EQ(scope.count(), 0) << "adopting a new list";
}

TEST(LfoShapeMorph, TheBlendOnlyRunsWhenTheCursorOrTheListChanges) {
    LfoShapeBlender blender;
    blender.setShapes(twoShapes());
    const auto first = blender.blend(0.2f, 0.5f);
    const auto same = blender.blend(0.2f, 0.5f);
    EXPECT_EQ(first, same);
    EXPECT_NE(blender.blend(0.8f, 0.5f), first) << "a moved cursor re-blends";
    blender.setShapes(LfoCustomShapes::single(LfoCustomWave::preset(LfoCustomWave::Preset::Square)));
    LfoCustomWave::Table square{};
    LfoCustomWave::preset(LfoCustomWave::Preset::Square).renderTable(square);
    EXPECT_EQ(blender.blend(0.8f, 0.5f), square) << "a new list re-blends";
}

TEST(LfoShapeMorph, ABlendKeepsTheWrapGuardEqualToTheFirstEntry) {
    LfoShapeBlender blender;
    blender.setShapes(twoShapes());
    const auto& table = blender.blend(0.4f, 0.5f);
    EXPECT_FLOAT_EQ(table[LfoCustomWave::kTableSize], table[0]);
}

TEST(LfoShapeMorph, TheBlendWeightsAreTheFilterWeights) {
    const float xs[3] = {0.1f, 0.9f, 0.5f};
    const float ys[3] = {0.5f, 0.5f, 0.9f};
    float w[3];
    synth::morph::blendWeights(xs, ys, 3, 0.3f, 0.4f, w);
    synth::CustomFilterEngine::ShapeTable table;
    table.count = 3;
    table.x = {xs, xs + 3};
    table.y = {ys, ys + 3};
    float f[3];
    synth::CustomFilterEngine::blendWeights(table, 0.3f, 0.4f, f);
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(w[i], f[i]);
    EXPECT_NEAR(w[0] + w[1] + w[2], 1.0f, 1.0e-6f);
}
