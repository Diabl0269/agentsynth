// The shared Mix and Pan of ModuleBase's output stage (Source/Modules/ModuleOutputStage.cpp):
// which modules carry them, what they do to the audio legs, and that at rest (Mix 1, Pan 0) the
// stage is skipped and the output is bit-identical to the module's own render.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kBlocks = 24;

// Modules that opt in to the shared Pan, and the four that also take the shared Mix. Every other FX
// already has its own Mix; the Limiter has none because its lookahead latency would misalign the dry.
const std::vector<const char*> kSharedMixAndPan = {"Filter", "Compressor", "Gate", "Parametric EQ"};
const std::vector<const char*> kPanOnly = {"Bitcrusher", "Chorus",        "Delay",  "Distortion",     "Flanger",
                                           "Phaser",     "Pitch Shifter", "Reverb", "Ring Modulator", "Limiter"};

int countParam(juce::AudioProcessor& m, const juce::String& id) {
    int n = 0;
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id)
            ++n;
    return n;
}

void setParam(juce::AudioProcessor& m, const char* id, float value) {
    auto* p = findParameterByID(&m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

/** A 220 Hz saw (rich in harmonics, so a low-pass visibly changes it) on the audio legs of `m`. */
void fillSaw(juce::AudioProcessor& m, juce::AudioBuffer<float>& buf, double& phase) {
    buf.clear();
    std::vector<int> legs{0};
    if (auto* mb = dynamic_cast<ModuleBase*>(&m); mb != nullptr && mb->rightAudioLegChannel() > 0)
        legs.push_back(mb->rightAudioLegChannel());
    for (int i = 0; i < buf.getNumSamples(); ++i) {
        const float v = 0.4f * (float)(2.0 * std::fmod(phase, 1.0) - 1.0);
        phase += 220.0 / kRate;
        for (int leg : legs)
            if (leg < buf.getNumChannels())
                buf.setSample(leg, i, v);
    }
}

struct Rendered {
    juce::AudioBuffer<float> in, out;
};

/** Renders kBlocks blocks and keeps the last one's input and output (the stage has settled by then). */
Rendered renderLast(juce::AudioProcessor& m) {
    m.setPlayConfigDetails(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels(), kRate, kBlock);
    m.prepareToPlay(kRate, kBlock);
    const int channels = std::max(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf(channels, kBlock);
    Rendered r;
    double phase = 0.0;
    for (int b = 0; b < kBlocks; ++b) {
        fillSaw(m, buf, phase);
        r.in.makeCopyOf(buf);
        juce::MidiBuffer midi;
        m.processBlock(buf, midi);
    }
    r.out.makeCopyOf(buf);
    return r;
}

float rms(const juce::AudioBuffer<float>& b, int ch) { return b.getRMSLevel(ch, 0, b.getNumSamples()); }

} // namespace

TEST(ModuleMixPan, TheListedModulesCarryTheSharedParameters) {
    for (const char* name : kSharedMixAndPan) {
        auto m = synth::AIStateMapper::createModule(name);
        ASSERT_NE(m, nullptr) << name;
        EXPECT_EQ(countParam(*m, "mix"), 1) << name;
        EXPECT_EQ(countParam(*m, "pan"), 1) << name;
    }
    for (const char* name : kPanOnly) {
        auto m = synth::AIStateMapper::createModule(name);
        ASSERT_NE(m, nullptr) << name;
        EXPECT_LE(countParam(*m, "mix"), 1) << name << " must keep exactly its own Mix";
        EXPECT_EQ(countParam(*m, "pan"), 1) << name;
    }
}

TEST(ModuleMixPan, DefaultsAreUnityMixAndCentrePan) {
    auto m = synth::AIStateMapper::createModule("Filter");
    ASSERT_NE(m, nullptr);
    EXPECT_FLOAT_EQ(findParameterByID(m.get(), "mix")->convertFrom0to1(findParameterByID(m.get(), "mix")->getValue()),
                    1.0f);
    EXPECT_NEAR(findParameterByID(m.get(), "pan")->convertFrom0to1(findParameterByID(m.get(), "pan")->getValue()), 0.0f,
                1.0e-6f);
}

TEST(ModuleMixPan, MixZeroOnAFilterIsTheDrySignal) {
    FilterModule m;
    setParam(m, "cutoff", 200.0f);
    setParam(m, "mix", 0.0f);
    const auto r = renderLast(m);
    for (int i = 0; i < kBlock; ++i)
        ASSERT_NEAR(r.out.getSample(0, i), r.in.getSample(0, i), 1.0e-6f) << i;
}

TEST(ModuleMixPan, HalfMixKeepsTheDrySawUnderAResonantLowPass) {
    FilterModule wet, half;
    for (auto* m : {&wet, &half}) {
        setParam(*m, "cutoff", 200.0f);
        setParam(*m, "resonance", 0.9f);
    }
    setParam(half, "mix", 0.5f);
    const auto w = renderLast(wet);
    const auto h = renderLast(half);
    EXPECT_GT(rms(h.in, 0), 0.1f);
    // Mix follows the module's own render, so the blend is exactly half the dry plus half the wet.
    for (int i = 0; i < kBlock; ++i)
        ASSERT_NEAR(h.out.getSample(0, i), 0.5f * h.in.getSample(0, i) + 0.5f * w.out.getSample(0, i), 1.0e-5f) << i;
    EXPECT_GT(rms(h.out, 0), 0.4f * rms(h.in, 0));
}

TEST(ModuleMixPan, PanHardLeftSilencesTheRightLegAndLeavesTheLeftAtUnity) {
    for (const bool dual : {true, false}) {
        DistortionModule centre, left;
        for (auto* m : {&centre, &left})
            setParam(*m, "dualIO", dual ? 1.0f : 0.0f);
        setParam(left, "pan", -1.0f);
        const auto c = renderLast(centre);
        const auto l = renderLast(left);
        EXPECT_EQ(l.out.getMagnitude(1, 0, kBlock), 0.0f) << "dual=" << dual;
        for (int i = 0; i < kBlock; ++i)
            ASSERT_EQ(l.out.getSample(0, i), c.out.getSample(0, i)) << i;
    }
}

TEST(ModuleMixPan, PanHardRightSilencesTheLeftLeg) {
    DistortionModule m;
    setParam(m, "pan", 1.0f);
    const auto r = renderLast(m);
    EXPECT_LT(r.out.getMagnitude(0, 0, kBlock), 1.0e-6f) << "hard right leaves nothing on the left leg";
    EXPECT_GT(r.out.getMagnitude(1, 0, kBlock), 0.0f);
}

TEST(ModuleMixPan, PanRestsOnASplitBlockModuleCollapsedToOneJack) {
    FilterModule collapsed, centre;
    setParam(collapsed, "dualIO", 0.0f);
    setParam(collapsed, "pan", 1.0f);
    setParam(centre, "dualIO", 0.0f);
    const auto a = renderLast(collapsed);
    const auto b = renderLast(centre);
    for (int i = 0; i < kBlock; ++i)
        ASSERT_EQ(a.out.getSample(0, i), b.out.getSample(0, i)) << "no right jack, so the left leg is not attenuated";
}

TEST(ModuleMixPan, AtRestTheOutputIsBitIdenticalToTheModulesOwnRender) {
    std::vector<const char*> all = kSharedMixAndPan;
    all.insert(all.end(), kPanOnly.begin(), kPanOnly.end());
    for (const char* name : all) {
        auto viaBase = synth::AIStateMapper::createModule(name);
        auto direct = synth::AIStateMapper::createModule(name);
        ASSERT_NE(viaBase, nullptr) << name;
        for (auto* m : {viaBase.get(), direct.get()}) {
            m->setPlayConfigDetails(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels(), kRate, kBlock);
            m->prepareToPlay(kRate, kBlock);
        }
        auto* base = dynamic_cast<ModuleBase*>(direct.get());
        const int channels = std::max(viaBase->getTotalNumInputChannels(), viaBase->getTotalNumOutputChannels());
        juce::AudioBuffer<float> x(channels, kBlock), y(channels, kBlock);
        double phase = 0.0;
        for (int b = 0; b < 6; ++b) {
            fillSaw(*viaBase, x, phase);
            y.makeCopyOf(x);
            juce::MidiBuffer m1, m2;
            viaBase->processBlock(x, m1);
            base->processModuleBlock(y, m2);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < kBlock; ++i)
                    ASSERT_EQ(x.getSample(ch, i), y.getSample(ch, i)) << name << " ch" << ch << " block " << b;
        }
    }
}

TEST(ModuleMixPan, ABypassedModuleStillOutputsThePureDryWhateverMixAndPanAre) {
    FilterModule m;
    setParam(m, "mix", 0.5f);
    setParam(m, "pan", -1.0f);
    m.setBypassed(true);
    const auto r = renderLast(m);
    for (int ch : {0, (int)FilterModule::kRightBase})
        for (int i = 0; i < kBlock; ++i)
            ASSERT_EQ(r.out.getSample(ch, i), r.in.getSample(ch, i)) << "ch" << ch;
}

TEST(ModuleMixPan, AMutedModuleStaysSilentAtPartialMix) {
    FilterModule m;
    setParam(m, "mix", 0.5f);
    m.setMuted(true);
    const auto r = renderLast(m);
    EXPECT_EQ(r.out.getMagnitude(0, 0, kBlock), 0.0f);
}

TEST(ModuleMixPan, MixAndPanSurviveSaveAndLoad) {
    FilterModule a;
    setParam(a, "mix", 0.3f);
    setParam(a, "pan", -0.4f);
    juce::MemoryBlock state;
    a.getStateInformation(state);
    FilterModule b;
    b.setStateInformation(state.getData(), (int)state.getSize());
    for (const char* id : {"mix", "pan"})
        EXPECT_FLOAT_EQ(findParameterByID(&b, id)->getValue(), findParameterByID(&a, id)->getValue()) << id;
}
