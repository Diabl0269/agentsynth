// A module that sleeps on silent input is bit-identical to the full render: each scenario renders on fresh modules once
// with synth::RenderShortcuts disabled (the reference) and once enabled; every output sample (float ==, so +0 and -0
// agree and NaN never does) and every observable getter and view buffer must match, and the enabled run must show that
// the sleep path ran (docs/architecture/audio-engine.md#render-shortcuts).

#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FilterModule.h"
#include "Modules/RenderShortcuts.h"
#include "Modules/VCAModule.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 128;

struct ShortcutsOff {
    ShortcutsOff() { synth::RenderShortcuts::setEnabled(false); }
    ~ShortcutsOff() { synth::RenderShortcuts::setEnabled(true); }
};

void setFloat(ModuleBase& m, const juce::String& id, float actual) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

void setBool(ModuleBase& m, const juce::String& id, bool on) {
    auto* p = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(on ? 1.0f : 0.0f);
}

void setChoice(ModuleBase& m, const juce::String& id, int index) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    *p = index;
}

struct Capture {
    std::vector<float> values; // everything observed, in order
    unsigned slept = 0;
    int nonZeroOutputs = 0;
};

void pushBuffer(Capture& r, const juce::AudioBuffer<float>& b, int channels) {
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const float v = b.getReadPointer(ch)[i];
            r.values.push_back(v);
            if (ch == 0 && v != 0.0f)
                ++r.nonZeroOutputs;
        }
}

void pushView(Capture& r, VisualBuffer* vb, int size) {
    if (vb == nullptr)
        return;
    std::vector<float> copy((size_t)size);
    vb->copyTo(copy);
    r.values.insert(r.values.end(), copy.begin(), copy.end());
}

// Deterministic burst: a saw plus a little noise, on ch0.
void fillAudio(juce::AudioBuffer<float>& buf, int block) {
    juce::Random rng(1234 + block);
    for (int i = 0; i < buf.getNumSamples(); ++i) {
        const float phase = std::fmod((float)(block * kBlock + i) * 220.0f / (float)kRate, 1.0f);
        buf.setSample(0, i, 0.6f * (2.0f * phase - 1.0f) + 0.05f * (rng.nextFloat() - 0.5f));
    }
}

void expectSameRuns(const Capture& ref, const Capture& fast) {
    ASSERT_EQ(ref.values.size(), fast.values.size());
    for (size_t i = 0; i < ref.values.size(); ++i) {
        ASSERT_FALSE(std::isnan(ref.values[i]) || std::isnan(fast.values[i])) << "NaN at " << i;
        ASSERT_TRUE(ref.values[i] == fast.values[i])
            << "value " << i << ": " << ref.values[i] << " vs " << fast.values[i];
    }
}

enum class Step { Audio, Silence, SilenceCvRamp, SilenceKnobMove, SilenceResonanceMove };

struct FilterCase {
    int type = 0;
    float drive = 1.0f;
    float resonance = 0.2f;
    float cutoff = 1500.0f;
    std::vector<std::pair<Step, int>> steps;
};

Capture renderFilter(const FilterCase& c) {
    const juce::ScopedNoDenormals noDenormals; // as the device callback runs: a tail decays to exactly zero
    FilterModule f;
    f.prepareToPlay(kRate, kBlock);
    setChoice(f, "filterType", c.type);
    setFloat(f, "drive", c.drive);
    setFloat(f, "resonance", c.resonance);
    setFloat(f, "cutoff", c.cutoff);
    Capture run;
    synth::RenderShortcuts::sleptBlocks() = 0;
    int block = 0;
    for (const auto& [step, count] : c.steps) {
        for (int n = 0; n < count; ++n, ++block) {
            juce::AudioBuffer<float> buf(FilterModule::kNumChannels, kBlock);
            buf.clear();
            if (step == Step::Audio)
                fillAudio(buf, block);
            if (step == Step::SilenceCvRamp)
                for (int i = 0; i < kBlock; ++i)
                    buf.setSample(1, i, 0.5f * (float)i / (float)kBlock);
            if (step == Step::SilenceKnobMove && n == 0)
                setFloat(f, "cutoff", c.cutoff * 0.4f);
            if (step == Step::SilenceResonanceMove && n == 0)
                setFloat(f, "resonance", 0.8f);
            juce::MidiBuffer midi;
            f.processBlock(buf, midi);
            pushBuffer(run, buf, 2);
            run.values.push_back(f.getCurrentCutoff());
            run.values.push_back(f.getModulatedResonance());
            run.values.push_back(f.getModulatedDrive());
            run.values.push_back(f.getLastPlayedPitchHz());
            if (block % 8 == 0)
                pushView(run, f.getVisualBuffer(), 1024);
        }
    }
    pushView(run, f.getVisualBuffer(), 1024);
    pushView(run, &f.getSpectrumBuffer(), FilterModule::kSpectrumSamples);
    run.slept = synth::RenderShortcuts::sleptBlocks();
    return run;
}

void expectFilterIdentical(const FilterCase& c) {
    Capture ref;
    {
        ShortcutsOff off;
        ref = renderFilter(c);
    }
    const Capture fast = renderFilter(c);
    EXPECT_EQ(ref.slept, 0u);
    EXPECT_GT(fast.slept, 100u) << "the sleep path never ran, so this proves nothing";
    expectSameRuns(ref, fast);
}

std::vector<std::pair<Step, int>> wakeSteps() {
    return {{Step::Audio, 6},
            {Step::Silence, 500},
            {Step::SilenceCvRamp, 20},
            {Step::SilenceKnobMove, 120},
            {Step::SilenceResonanceMove, 120},
            {Step::Audio, 6},
            {Step::Silence, 20}};
}

} // namespace

TEST(SilentSleepFilter, EveryLadderTypeMatchesTheFullRender) {
    for (int type = 0; type <= 5; ++type)
        for (const auto& [drive, res] : {std::pair{1.0f, 0.2f}, std::pair{1.0f, 0.95f}, std::pair{6.0f, 0.95f}}) {
            SCOPED_TRACE(testing::Message() << "type " << type << " drive " << drive << " res " << res);
            FilterCase c;
            c.type = type;
            c.drive = drive;
            c.resonance = res;
            c.steps = wakeSteps();
            expectFilterIdentical(c);
        }
}

TEST(SilentSleepFilter, NotchAndCustomNeverSleepAndStillMatch) {
    for (const int type : {6, 7}) {
        FilterCase c;
        c.type = type;
        c.steps = {{Step::Audio, 6}, {Step::Silence, 100}, {Step::Audio, 3}};
        Capture ref;
        {
            ShortcutsOff off;
            ref = renderFilter(c);
        }
        const Capture fast = renderFilter(c);
        EXPECT_EQ(fast.slept, 0u) << type;
        expectSameRuns(ref, fast);
    }
}

TEST(SilentSleepFilter, ARingingResonantTailIsNotCutOff) {
    FilterCase c;
    c.type = 0;
    c.resonance = 0.97f;
    c.cutoff = 2000.0f;
    c.steps = {{Step::Audio, 1}, {Step::Silence, 400}};
    Capture ref;
    {
        ShortcutsOff off;
        ref = renderFilter(c);
    }
    const Capture fast = renderFilter(c);
    // Silence follows one audio block, so non-zero output beyond that block's 128 samples is the tail ringing.
    EXPECT_GT(ref.nonZeroOutputs, kBlock) << "the reference tail should ring past the burst block";
    EXPECT_EQ(ref.nonZeroOutputs, fast.nonZeroOutputs);
    EXPECT_GT(fast.slept, 0u) << "once the tail has decayed to exactly zero the filter sleeps";
    expectSameRuns(ref, fast);
}

namespace {

Capture renderVca() {
    VCAModule v;
    v.prepareToPlay(kRate, kBlock);
    setFloat(v, "gain", 0.0f);
    Capture run;
    synth::RenderShortcuts::sleptBlocks() = 0;
    for (int block = 0; block < 120; ++block) {
        if (block == 30)
            setFloat(v, "gain", 0.7f); // a knob move while the input is silent
        if (block == 60)
            setFloat(v, "gain", 0.0f);
        juce::AudioBuffer<float> buf(VCAModule::kNumChannels, kBlock);
        buf.clear();
        const bool audio = block >= 40 && block < 46;
        if (audio)
            fillAudio(buf, block);
        for (int i = 0; i < kBlock; ++i) // a gain CV that moves through the silence
            buf.setSample(1, i, 0.5f + 0.4f * std::sin(0.01f * (float)(block * kBlock + i)));
        juce::MidiBuffer midi;
        v.processBlock(buf, midi);
        pushBuffer(run, buf, 2);
        if (block % 8 == 0)
            pushView(run, v.getVisualBuffer(), 1024);
    }
    run.slept = synth::RenderShortcuts::sleptBlocks();
    return run;
}

} // namespace

TEST(SilentSleepVca, SilentInputAndAWakeMatchTheFullRender) {
    Capture ref;
    {
        ShortcutsOff off;
        ref = renderVca();
    }
    const Capture fast = renderVca();
    EXPECT_EQ(ref.slept, 0u);
    EXPECT_GT(fast.slept, 50u);
    expectSameRuns(ref, fast);
}

namespace {

Capture renderBypassedEq() {
    ParametricEQModule eq;
    eq.prepareToPlay(kRate, kBlock);
    setBool(eq, "bypassed", true);
    Capture run;
    for (int block = 0; block < 150; ++block) {
        if (block % 10 == 0) {
            setBool(eq, "band2On", block % 20 == 0);
            setFloat(eq, "band2Freq", 300.0f + 20.0f * (float)block);
            setFloat(eq, "band2Gain", (float)(block % 12) - 6.0f);
        }
        if (block == 110)
            setBool(eq, "bypassed", false);
        juce::AudioBuffer<float> buf(16, kBlock);
        buf.clear();
        if (block % 3 == 0 || block >= 110)
            fillAudio(buf, block);
        juce::MidiBuffer midi;
        eq.processBlock(buf, midi);
        pushBuffer(run, buf, 2);
        if (block % 8 == 0)
            pushView(run, eq.getVisualBuffer(), 1024);
    }
    return run;
}

} // namespace

TEST(SilentSleepParametricEq, BypassedBlocksWithParameterChangesMatchOnUnbypass) {
    Capture ref;
    {
        ShortcutsOff off;
        ref = renderBypassedEq();
    }
    expectSameRuns(ref, renderBypassedEq());
}
