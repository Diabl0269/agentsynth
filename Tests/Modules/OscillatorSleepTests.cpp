// A mono Oscillator nobody can hear sleeps and wakes sample-identical
// (docs/architecture/audio-engine.md#oscillator-sleep). Each case renders the same patch and the same MIDI twice on
// fresh graphs, once with synth::RenderShortcuts off (the full render) and once on; every output sample must match
// (float ==, so +0 and -0 agree) and the run with shortcuts on must show whether the sleep path ran. In these rigs only
// the Oscillator and the VCA can sleep, and the VCA only on a silent input, which only a sleeping Oscillator gives it:
// so a slept count above 0 means the Oscillator slept.

#include "../FakeAudioIODevice.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/OscillatorSleepProof.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModCurve.h"
#include "Modules/OscillatorModule.h"
#include "Modules/RenderShortcuts.h"
#include "Modules/VCAModule.h"
#include <functional>
#include <gtest/gtest.h>
#include <vector>

namespace {

using Graph = juce::AudioProcessorGraph;
using IOProcessor = Graph::AudioGraphIOProcessor;
constexpr double kRate = 48000.0;
constexpr int kBlock = 128;
constexpr int kMidi = Graph::midiChannelIndex;

struct ShortcutsOff {
    ShortcutsOff() { synth::RenderShortcuts::setEnabled(false); }
    ~ShortcutsOff() { synth::RenderShortcuts::setEnabled(true); }
};

void setParam(juce::AudioProcessor& p, const juce::String& id, float actual) {
    auto* param = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&p, id));
    ASSERT_NE(param, nullptr) << id;
    param->setValueNotifyingHost(param->convertTo0to1(actual));
}

enum class Gain { Envelope, LinearAttenuverter, OffsetCurve, Lfo, Audio };

struct Patch {
    int waveform = 2; // Sine, Square, Saw, Triangle
    int unison = 1;
    float rand = 0.0f; // percent
    float glideMs = 0.0f;
    Gain gain = Gain::Envelope;
};

// MIDI in -> Oscillator and ADSR; Oscillator's two-leg jack -> VCA audio; the VCA's gain CV from `gain`; VCA -> out.
struct Rig {
    Graph graph;
    Graph::Node::Ptr in, out, osc, adsr, vca;

    explicit Rig(const Patch& patch) {
        graph.setPlayConfigDetails(0, 2, kRate, kBlock);
        in = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::midiInputNode));
        out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
        osc = graph.addNode(std::make_unique<OscillatorModule>());
        adsr = graph.addNode(std::make_unique<ADSRModule>());
        vca = graph.addNode(std::make_unique<VCAModule>());
        auto& o = *osc->getProcessor();
        setParam(o, "waveform", (float)patch.waveform);
        setParam(o, "unison", (float)patch.unison);
        setParam(o, "detune", 20.0f);
        setParam(o, "randomPhase", patch.rand);
        setParam(o, "glide", patch.glideMs);
        auto& a = *adsr->getProcessor();
        setParam(a, "attack", 0.002f);
        setParam(a, "decay", 0.05f);
        setParam(a, "sustain", 0.6f);
        setParam(a, "release", 0.01f);
        connect(in, kMidi, osc, kMidi);
        connect(in, kMidi, adsr, kMidi);
        connect(osc, 0, vca, 0);
        connect(osc, OscillatorModule::kRightBase, vca, VCAModule::kRightBase);
        connect(vca, 0, out, 0);
        connect(vca, VCAModule::kRightBase, out, 1);
        wireGain(patch.gain);
        graph.prepareToPlay(kRate, kBlock);
        synth::publishOscillatorSleepCones(graph);
    }

    void connect(const Graph::Node::Ptr& from, int fromCh, const Graph::Node::Ptr& to, int toCh) {
        ASSERT_TRUE(graph.addConnection({{from->nodeID, fromCh}, {to->nodeID, toCh}}));
    }

    void wireGain(Gain gain) {
        if (gain == Gain::Envelope) {
            connect(adsr, 0, vca, 1);
        } else if (gain == Gain::LinearAttenuverter || gain == Gain::OffsetCurve) {
            auto att = graph.addNode(std::make_unique<AttenuverterModule>());
            setParam(*att->getProcessor(), "amount", 0.8f);
            if (gain == Gain::OffsetCurve) {
                synth::ModCurve curve; // zero in maps to a quarter: an offset on the VCA's gain
                curve.rangeMin = 0.25f;
                dynamic_cast<AttenuverterModule*>(att->getProcessor())->setCurve(curve);
            }
            connect(adsr, 0, att, 0);
            connect(att, 0, vca, 1);
        } else if (gain == Gain::Lfo) {
            auto lfo = graph.addNode(std::make_unique<LFOModule>());
            connect(lfo, 0, vca, 1);
        } else {
            auto other = graph.addNode(std::make_unique<OscillatorModule>());
            setParam(*other->getProcessor(), "rateMode", 1.0f); // free-running at Fixed rate, no note needed
            connect(other, 0, vca, 1);
        }
    }
};

using Events = std::function<void(int block, juce::MidiBuffer&, Rig&)>;

struct Run {
    std::vector<float> samples; // both output channels, block after block
    unsigned slept = 0;
    int heardIdle = 0; // non-zero samples in blocks the events leave idle (no note held, VCA not bypassed)
};

Run render(const Patch& patch, int blocks, const Events& events, const std::function<bool(int)>& idle = {}) {
    const juce::ScopedNoDenormals noDenormals;
    Rig rig(patch);
    Run run;
    synth::RenderShortcuts::sleptBlocks() = 0;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        events(b, midi, rig);
        rig.graph.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i) {
                const float v = buffer.getSample(ch, i);
                run.samples.push_back(v);
                if (idle && idle(b) && v != 0.0f)
                    ++run.heardIdle;
            }
    }
    run.slept = synth::RenderShortcuts::sleptBlocks();
    return run;
}

void expectSame(const Run& full, const Run& fast) {
    ASSERT_EQ(full.samples.size(), fast.samples.size());
    for (size_t i = 0; i < full.samples.size(); ++i)
        ASSERT_TRUE(full.samples[i] == fast.samples[i]) << "sample " << i << " (block " << i / (2 * kBlock)
                                                        << "): " << full.samples[i] << " vs " << fast.samples[i];
}

/** Renders full and fast; returns the fast run. */
Run renderBoth(const Patch& patch, int blocks, const Events& events, const std::function<bool(int)>& idle = {}) {
    Run full;
    {
        const ShortcutsOff off;
        full = render(patch, blocks, events, idle);
    }
    EXPECT_EQ(full.slept, 0u);
    Run fast = render(patch, blocks, events, idle);
    expectSame(full, fast);
    return fast;
}

void setBypass(Rig& rig, bool on) { dynamic_cast<ModuleBase*>(rig.vca->getProcessor())->setBypassed(on); }

// A played note, a long rest, a second note on a sample inside a block (the wake), its release and rest, then the
// VCA bypassed with no MIDI at all: the Oscillator is heard dry, so its phases must have run on exactly while it
// slept.
void phrase(int b, juce::MidiBuffer& midi, Rig& rig) {
    if (b == 2)
        midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 10);
    if (b == 20)
        midi.addEvent(juce::MidiMessage::noteOff(1, 57), 5);
    if (b == 80)
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 37);
    if (b == 100)
        midi.addEvent(juce::MidiMessage::noteOff(1, 64), 90);
    if (b == 160)
        setBypass(rig, true);
    if (b == 200)
        setBypass(rig, false);
}

constexpr int kPhraseBlocks = 260;

} // namespace

TEST(OscillatorSleep, EveryWaveformWakesSampleIdentical) {
    for (int waveform = 0; waveform < 4; ++waveform) {
        SCOPED_TRACE(waveform);
        Patch patch;
        patch.waveform = waveform;
        const auto fast = renderBoth(patch, kPhraseBlocks, phrase);
        EXPECT_GT(fast.slept, 40u) << "the Oscillator never slept";
    }
}

TEST(OscillatorSleep, UnisonAndGlideWakeSampleIdentical) {
    Patch unison;
    unison.unison = 4;
    unison.rand = 100.0f; // apart: no in-phase start, so the low-band trim rests
    EXPECT_GT(renderBoth(unison, kPhraseBlocks, phrase).slept, 40u);

    Patch glide;
    glide.glideMs = 80.0f;
    EXPECT_GT(renderBoth(glide, kPhraseBlocks, phrase).slept, 40u);

    // Rand 0 restarts every voice of a wide stack in phase, and the trim that takes that back out keeps state: asleep
    // only in the two blocks before the first note's restart, still identical.
    Patch inPhase;
    inPhase.unison = 3;
    EXPECT_LE(renderBoth(inPhase, kPhraseBlocks, phrase).slept, 2u);
}

TEST(OscillatorSleep, ANoteAcrossTheLoopEndWakesInTheSameBlock) {
    // A loop wrap ends a held note and starts it again inside one block; the next wrap lands on a block's last sample.
    const Events wraps = [](int b, juce::MidiBuffer& midi, Rig&) {
        if (b == 2)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        if (b == 40) {
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 64);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 64);
        }
        if (b == 50)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        if (b == 90)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), kBlock - 1);
    };
    const auto fast = renderBoth(Patch{}, 120, wraps);
    EXPECT_GT(fast.slept, 20u);
}

TEST(OscillatorSleep, AGainThatIsNotAnEnvelopeAtRestNeverSleeps) {
    const Events none = [](int, juce::MidiBuffer&, Rig&) {};
    const auto always = [](int) { return true; };
    for (const auto gain : {Gain::Lfo, Gain::Audio, Gain::OffsetCurve}) {
        SCOPED_TRACE((int)gain);
        Patch patch;
        patch.gain = gain;
        const auto fast = renderBoth(patch, 60, none, always);
        EXPECT_EQ(fast.slept, 0u);
        EXPECT_GT(fast.heardIdle, 0) << "the Oscillator is heard through this gain, so it must keep running";
    }
    Patch linear;
    linear.gain = Gain::LinearAttenuverter;
    EXPECT_GT(renderBoth(linear, kPhraseBlocks, phrase).slept, 40u) << "a linear Attenuverter keeps zero at zero";
}

TEST(OscillatorSleep, ACableEditRefreshesTheProof) {
    // A cable straight from the Oscillator to the output makes it heard: the proof is re-run on the edit and the
    // Oscillator wakes; unplugging that cable lets it sleep again.
    const Events edits = [](int b, juce::MidiBuffer&, Rig& rig) {
        if (b == 20) {
            rig.connect(rig.osc, 0, rig.out, 0);
            synth::publishOscillatorSleepCones(rig.graph);
        }
        if (b == 40) {
            rig.graph.removeConnection({{rig.osc->nodeID, 0}, {rig.out->nodeID, 0}});
            synth::publishOscillatorSleepCones(rig.graph);
        }
    };
    const auto heard = [](int b) { return b >= 20 && b < 40; };
    const auto fast = renderBoth(Patch{}, 60, edits, heard);
    EXPECT_GT(fast.heardIdle, 0);
    EXPECT_GT(fast.slept, 20u);
}

TEST(OscillatorSleep, TheEngineRefreshesTheProofOnACanvasCableEdit) {
    // Through the engine's own graph broadcast, as a canvas cable drag reaches it: no refresh call by hand.
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kRate, kBlock);
    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    auto adsr = graph.addNode(std::make_unique<ADSRModule>());
    auto vca = graph.addNode(std::make_unique<VCAModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {vca->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{adsr->nodeID, 0}, {vca->nodeID, 1}}));
    ASSERT_TRUE(graph.addConnection({{vca->nodeID, 0}, {out->nodeID, 0}}));

    synth::test::FakeAudioIODevice device(0, 2);
    engine.audioDeviceAboutToStart(&device);
    const auto renderBlocks = [&](int count) {
        std::vector<float> left((size_t)device.getCurrentBufferSizeSamples()), right(left.size());
        float* outs[] = {left.data(), right.data()};
        float peak = 0.0f;
        synth::RenderShortcuts::sleptBlocks() = 0;
        for (int b = 0; b < count; ++b) {
            engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, (int)left.size(), {});
            for (float v : left)
                peak = std::max(peak, std::abs(v));
        }
        return std::make_pair(peak, synth::RenderShortcuts::sleptBlocks());
    };
    const auto pump = [] {
        for (int i = 0; i < 20; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    };
    pump();
    const auto asleep = renderBlocks(20);
    EXPECT_EQ(asleep.first, 0.0f);
    EXPECT_GT(asleep.second, 0u) << "an Oscillator gated by an envelope that never fires should sleep";

    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {out->nodeID, 0}}));
    pump();
    const auto heard = renderBlocks(20);
    EXPECT_GT(heard.first, 0.1f) << "the new cable must wake the Oscillator";
    engine.audioDeviceStopped();
}
