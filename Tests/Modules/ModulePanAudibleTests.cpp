// The pan guard: every factory module type that shows a Pan knob must move the sound, never silence
// it. One enumeration over the module factory x jack mode x input shape x Pan -1/0/+1. A mono source
// patched to the left jack only, or a collapsed split-block module, used to leave the right leg empty,
// so a hard-right pan took the left leg to zero and nothing was left to hear.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kBlocks = 40;

enum class Input { LeftOnly, Stereo };

struct Case {
    juce::String type;
    bool hasDual = false;
    bool dual = false;
    Input input = Input::LeftOnly;
    float pan = 0.0f;
    bool normalled = true; // the engine's Right-borrows-Left flag, for a split module with only Left patched
};

struct Heard {
    float left = 0.0f, right = 0.0f;
};

// Generators own their pan and need notes to sound; the rest take an audio input.
const std::vector<juce::String> kGenerators = {"Oscillator", "Wavetable"};
// A mixer strip's pan is the project pan law, covered by the mixer tests.
const std::vector<juce::String> kOwnLaw = {"Channel Strip"};

bool contains(const std::vector<juce::String>& v, const juce::String& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

void setParam(juce::AudioProcessor& m, const char* id, float value) {
    auto* p = findParameterByID(&m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

/** The audio legs as a downstream cable sees them: raw ch0, and the right leg where one exists. */
Heard render(const Case& c) {
    auto proc = synth::AIStateMapper::createModule(c.type);
    auto* m = dynamic_cast<ModuleBase*>(proc.get());
    Heard h;
    if (m == nullptr)
        return h;
    if (c.hasDual)
        setParam(*m, "dualIO", c.dual ? 1.0f : 0.0f);
    setParam(*m, "pan", c.pan);
    m->setPlayConfigDetails(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels(), kRate, kBlock);
    m->prepareToPlay(kRate, kBlock);

    const int right = m->rightAudioLegChannel();
    const bool splitBlock = m->hasSplitBlockStereo();
    const bool generator = contains(kGenerators, c.type);
    // A collapsed split-block module hides its right block: no jack reaches it, so only raw ch0 is heard.
    const bool rightHeard = right > 0 && (!c.hasDual || c.dual || !splitBlock);
    // The right INPUT leg exists only where that raw channel is an audio input (a Ring Modulator's ch1
    // is its Modulator). A collapsed contiguous pair fans one cable onto both legs; a split Left-only
    // patch leaves Right empty (the engine then normals it when the module opts in).
    const bool rightIsAudioIn = right > 0 && m->mapInputChannel(right).role == PortRole::Audio;
    const bool fan = c.input == Input::LeftOnly && c.hasDual && !c.dual && !splitBlock && rightIsAudioIn;
    const bool stereoIn = c.input == Input::Stereo && rightIsAudioIn && (!c.hasDual || c.dual || !splitBlock);
    m->setNormalLeftToRight(c.input == Input::LeftOnly && c.hasDual && c.dual && c.normalled &&
                            m->hasStereoAudioInputPair());

    const int channels = std::max(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf(channels, kBlock);
    double phase = 0.0;
    for (int b = 0; b < kBlocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (generator) {
            if (b == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
        } else {
            for (int i = 0; i < kBlock; ++i) {
                const float v = 0.4f * (float)(2.0 * std::fmod(phase, 1.0) - 1.0);
                phase += 220.0 / kRate;
                buf.setSample(0, i, v);
                if ((fan || stereoIn) && right < channels)
                    buf.setSample(right, i, v);
                if (c.type == "Ring Modulator")
                    buf.setSample(1, i, v); // ch1 is the Modulator input
            }
        }
        m->processBlock(buf, midi);
    }
    h.left = buf.getRMSLevel(0, 0, kBlock);
    h.right = rightHeard && right < channels ? buf.getRMSLevel(right, 0, kBlock) : -1.0f;
    return h;
}

std::string name(const Case& c) {
    return (c.type + (c.hasDual ? (c.dual ? " split" : " collapsed") : " fixed") +
            (c.input == Input::Stereo ? " stereo-in"
             : c.normalled            ? " mono-in"
                                      : " mono-in-unnormalled") +
            " pan " + juce::String(c.pan, 1))
        .toStdString();
}

} // namespace

TEST(ModulePanAudible, EveryPanKnobMovesTheSoundAndNeverSilencesIt) {
    int checked = 0;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto probe = synth::AIStateMapper::createModule(type);
        if (probe == nullptr || findParameterByID(probe.get(), "pan") == nullptr || contains(kOwnLaw, type))
            continue;
        auto* mb = dynamic_cast<ModuleBase*>(probe.get());
        ASSERT_NE(mb, nullptr) << type;
        const bool hasDual = mb->hasDualIOParameter();
        const bool generator = contains(kGenerators, type);
        for (const bool dual : hasDual ? std::vector<bool>{true, false} : std::vector<bool>{false}) {
            for (const Input input : {Input::LeftOnly, Input::Stereo}) {
                for (const bool normalled : {true, false}) {
                    if ((input == Input::Stereo && !normalled) || (generator && (input == Input::Stereo || !normalled)))
                        continue;
                    Case c{type, hasDual, dual, input, 0.0f, normalled};
                    const auto at = [&](float pan) {
                        c.pan = pan;
                        return render(c);
                    };
                    const Heard mid = at(0.0f), lo = at(-1.0f), hi = at(1.0f), nearMid = at(0.05f);
                    const std::string n = name(c);
                    ASSERT_GT(mid.left, 0.01f) << n << ": the case must be audible at centre";
                    const bool rightHeard = hi.right >= 0.0f;
                    if (!rightHeard) {
                        // One heard leg: a balance would fade it out, so the pan rests.
                        EXPECT_NEAR(lo.left, mid.left, 1.0e-4f) << n;
                        EXPECT_NEAR(hi.left, mid.left, 1.0e-4f) << n;
                        ++checked;
                        continue;
                    }
                    const float ref = std::max(mid.left, mid.right);
                    // Never silent at either extreme, and the whole sound lands on the panned side.
                    EXPECT_GT(hi.right, 0.3f * ref) << n << " full right is silent";
                    EXPECT_GT(lo.left, 0.3f * ref) << n << " full left is silent";
                    EXPECT_LT(hi.left, 1.0e-3f * ref) << n << " full right leaks left";
                    EXPECT_LT(lo.right, 1.0e-3f * ref) << n << " full left leaks right";
                    // Leaving centre never makes a leg jump.
                    EXPECT_LT(std::abs(nearMid.left - mid.left), 0.2f * ref) << n;
                    EXPECT_LT(std::abs(nearMid.right - mid.right), 0.2f * ref) << n;
                    ++checked;
                }
            }
        }
    }
    EXPECT_GT(checked, 40) << "the enumeration must reach every module with a Pan knob";
}

namespace {
/** A left-only saw block fed to `m` (right input leg empty). */
void fillLeftOnly(juce::AudioBuffer<float>& buf, double& phase) {
    buf.clear();
    for (int i = 0; i < buf.getNumSamples(); ++i) {
        buf.setSample(0, i, 0.4f * (float)(2.0 * std::fmod(phase, 1.0) - 1.0));
        phase += 220.0 / kRate;
    }
}
} // namespace

TEST(ModulePanAudible, CentreWithALeftOnlyInputIsBitIdenticalToTheModulesOwnRender) {
    for (const char* type : {"Delay", "Reverb", "Filter", "Chorus"}) {
        auto viaBase = synth::AIStateMapper::createModule(type);
        auto direct = synth::AIStateMapper::createModule(type);
        for (auto* m : {viaBase.get(), direct.get()}) {
            setParam(*m, "dualIO", 1.0f);
            m->setPlayConfigDetails(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels(), kRate, kBlock);
            m->prepareToPlay(kRate, kBlock);
        }
        const int channels = std::max(viaBase->getTotalNumInputChannels(), viaBase->getTotalNumOutputChannels());
        juce::AudioBuffer<float> x(channels, kBlock), y(channels, kBlock);
        double phase = 0.0;
        for (int b = 0; b < 8; ++b) {
            fillLeftOnly(x, phase);
            y.makeCopyOf(x);
            juce::MidiBuffer m1, m2;
            viaBase->processBlock(x, m1);
            dynamic_cast<ModuleBase*>(direct.get())->processModuleBlock(y, m2);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < kBlock; ++i)
                    ASSERT_EQ(x.getSample(ch, i), y.getSample(ch, i)) << type << " ch" << ch << " block " << b;
        }
    }
}

TEST(ModulePanAudible, ABypassedModuleIgnoresPanAndLendsNothingToTheRightLeg) {
    for (const char* type : {"Delay", "Reverb", "Filter"}) {
        auto proc = synth::AIStateMapper::createModule(type);
        auto* m = dynamic_cast<ModuleBase*>(proc.get());
        setParam(*m, "dualIO", 1.0f);
        setParam(*m, "pan", 1.0f);
        m->setBypassed(true);
        m->setPlayConfigDetails(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels(), kRate, kBlock);
        m->prepareToPlay(kRate, kBlock);
        const int channels = std::max(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf(channels, kBlock), in(channels, kBlock);
        double phase = 0.0;
        for (int b = 0; b < kBlocks; ++b) {
            fillLeftOnly(buf, phase);
            in.makeCopyOf(buf);
            juce::MidiBuffer midi;
            m->processBlock(buf, midi);
        }
        for (int i = 0; i < kBlock; ++i)
            ASSERT_EQ(buf.getSample(0, i), in.getSample(0, i)) << type;
        EXPECT_EQ(buf.getMagnitude(m->rightAudioLegChannel(), 0, kBlock), 0.0f) << type;
    }
}
