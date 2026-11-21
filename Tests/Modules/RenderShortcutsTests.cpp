// Every render shortcut is bit-identical to the path it skips: each scenario renders twice on fresh modules, once with
// synth::RenderShortcuts disabled (the reference) and once enabled, and every output sample and published meter must
// match bit for bit (docs/architecture/audio-engine.md#render-shortcuts).

#include "Modules/ADSRModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/RenderShortcuts.h"
#include <cstring>
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

// Everything a render publishes, as raw bits so -0, NaN and last-ulp differences all count.
using Trace = std::vector<juce::uint32>;

void push(Trace& t, float f) {
    juce::uint32 bits;
    std::memcpy(&bits, &f, sizeof bits);
    t.push_back(bits);
}

void pushBuffer(Trace& t, const juce::AudioBuffer<float>& b) {
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            push(t, b.getReadPointer(ch)[i]);
}

enum class Scenario { Notes, GateBurst, SustainAutomation };

Trace renderAdsr(bool poly, Scenario scenario) {
    ADSRModule adsr;
    adsr.prepareToPlay(kRate, kBlock);
    setBool(adsr, "poly", poly);
    setFloat(adsr, "release", 0.015f);
    Trace trace;
    int block = 0;
    const auto run = [&](const juce::MidiMessage* midi, float gate, bool thresholdCv) {
        juce::AudioBuffer<float> buf(15, kBlock);
        buf.clear();
        if (gate != 0.0f) {
            for (int i = 0; i < kBlock; ++i)
                buf.setSample(0, i, gate);
            if (poly)
                for (int i = 0; i < kBlock; ++i)
                    buf.setSample(1, i, gate * 0.5f);
        }
        if (thresholdCv)
            for (int i = 0; i < kBlock; ++i)
                buf.setSample(8, i, 0.01f * (float)i);
        juce::MidiBuffer m;
        if (midi != nullptr)
            m.addEvent(*midi, 3);
        adsr.processBlock(buf, m);
        pushBuffer(trace, buf);
        push(trace, adsr.getMeterLevel());
        push(trace, adsr.getEffectiveThreshold());
        push(trace, adsr.isOverThreshold() ? 1.0f : 0.0f);
        push(trace, (float)adsr.getTriggerCount());
        push(trace, (float)(int)adsr.getPlayheadStage());
        push(trace, adsr.getPlayheadProgress());
        push(trace, adsr.getPlayheadLevel());
        ++block;
    };
    const auto idle = [&](int n, bool thresholdCv = false) {
        for (int i = 0; i < n; ++i)
            run(nullptr, 0.0f, thresholdCv);
    };
    const auto on = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    const auto off = juce::MidiMessage::noteOff(1, 60);

    idle(20);
    if (scenario == Scenario::Notes) {
        run(&on, 0.0f, false);
        idle(10);
        run(&off, 0.0f, false);
        idle(40);
        idle(20, true);
    } else if (scenario == Scenario::GateBurst) {
        for (int i = 0; i < 6; ++i)
            run(nullptr, 0.8f, i % 2 == 0);
        idle(60);
        idle(10, true);
    } else {
        setFloat(adsr, "sustain", 0.2f);
        idle(10);
        setFloat(adsr, "velocity", 20.0f);
        idle(10);
        run(&on, 0.0f, false);
        idle(5);
        run(&off, 0.0f, false);
        idle(40);
    }
    idle(20);
    return trace;
}

void expectIdenticalAdsr(bool poly, Scenario scenario) {
    Trace reference;
    {
        ShortcutsOff off;
        reference = renderAdsr(poly, scenario);
    }
    const Trace fast = renderAdsr(poly, scenario);
    ASSERT_EQ(reference.size(), fast.size());
    for (size_t i = 0; i < reference.size(); ++i)
        ASSERT_EQ(reference[i], fast[i]) << "word " << i;
}

// A CV jack's whole block, sample 0 as given and the rest from `rest(i)`.
void renderOscillator(float first, float (*rest)(int), Trace& trace) {
    OscillatorModule osc;
    osc.prepareToPlay(kRate, kBlock);
    auto* unison = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(&osc, "unison"));
    ASSERT_NE(unison, nullptr);
    *unison = 3;
    setFloat(osc, "detune", 20.0f);
    for (int block = 0; block < 12; ++block) {
        juce::AudioBuffer<float> buf(24, kBlock);
        buf.clear();
        for (int ch = 14; ch <= 20; ++ch) {
            buf.setSample(ch, 0, first);
            for (int i = 1; i < kBlock; ++i)
                buf.setSample(ch, i, rest(i));
        }
        juce::MidiBuffer m;
        if (block == 0)
            m.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
        osc.processBlock(buf, m);
        pushBuffer(trace, buf);
    }
}

void expectIdenticalOscillator(float first, float (*rest)(int)) {
    Trace reference, fast;
    {
        ShortcutsOff off;
        renderOscillator(first, rest, reference);
    }
    renderOscillator(first, rest, fast);
    EXPECT_EQ(reference, fast);
}

float zeros(int) { return 0.0f; }
float ramp(int i) { return 0.004f * (float)i; }

} // namespace

TEST(RenderShortcutsTest, SwitchDefaultsOnAndRestores) {
    EXPECT_TRUE(synth::RenderShortcuts::enabled());
    {
        ShortcutsOff off;
        EXPECT_FALSE(synth::RenderShortcuts::enabled());
    }
    EXPECT_TRUE(synth::RenderShortcuts::enabled());
}

TEST(RenderShortcutsTest, AdsrMonoNotesIsBitIdentical) { expectIdenticalAdsr(false, Scenario::Notes); }
TEST(RenderShortcutsTest, AdsrMonoGateBurstIsBitIdentical) { expectIdenticalAdsr(false, Scenario::GateBurst); }
TEST(RenderShortcutsTest, AdsrMonoSustainAutomationIsBitIdentical) {
    expectIdenticalAdsr(false, Scenario::SustainAutomation);
}
TEST(RenderShortcutsTest, AdsrPolyNotesIsBitIdentical) { expectIdenticalAdsr(true, Scenario::Notes); }
TEST(RenderShortcutsTest, AdsrPolyGateBurstIsBitIdentical) { expectIdenticalAdsr(true, Scenario::GateBurst); }
TEST(RenderShortcutsTest, AdsrPolySustainAutomationIsBitIdentical) {
    expectIdenticalAdsr(true, Scenario::SustainAutomation);
}

TEST(RenderShortcutsTest, OscillatorCvNegativeZeroAtSampleZeroIsBitIdentical) {
    expectIdenticalOscillator(-0.0f, zeros);
    expectIdenticalOscillator(-0.0f, ramp);
}
TEST(RenderShortcutsTest, OscillatorCvRampIsBitIdentical) {
    expectIdenticalOscillator(0.0f, ramp);
    expectIdenticalOscillator(0.1f, ramp);
}
TEST(RenderShortcutsTest, OscillatorCvZerosIsBitIdentical) { expectIdenticalOscillator(0.0f, zeros); }
