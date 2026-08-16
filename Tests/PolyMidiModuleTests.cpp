#include "Modules/ADSRModule.h"
#include "Modules/PolyMidiModule.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr int kBlockSize = 512;
constexpr int kNumChannels = 16;
constexpr double kSampleRate = 44100.0;

// The module's minimum low-gate window on a re-articulation: jmax(16, 1 ms).
constexpr int kMinGateGap = 44; // 44100 * 0.001

float noteHz(int note) { return static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(note)); }

// The pitch/gate a voice is holding at the end of the last rendered block. Both are 5 ms-smoothed,
// so reading the final sample of a 512-sample block gives the settled value.
float voicePitch(const juce::AudioBuffer<float>& b, int voice) { return b.getSample(voice, b.getNumSamples() - 1); }
float voiceGate(const juce::AudioBuffer<float>& b, int voice) { return b.getSample(voice + 8, b.getNumSamples() - 1); }

// Gate CV of one voice at a sample offset inside the last rendered block.
float gateAt(const juce::AudioBuffer<float>& b, int voice, int sample) { return b.getSample(voice + 8, sample); }

// First offset at or after `from` where the voice's gate crosses 0.5, or -1.
int firstGateRise(const juce::AudioBuffer<float>& b, int voice, int from) {
    for (int s = from; s < b.getNumSamples(); ++s)
        if (gateAt(b, voice, s) > 0.5f)
            return s;
    return -1;
}

// One MIDI event inside a block.
struct Event {
    int note;
    int offset; // sample offset within the block
    bool on;
    float velocity = 0.8f;
};

void setStealMode(PolyMidiModule& m, PolyMidiModule::StealMode mode) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&m, "voiceSteal"));
    ASSERT_NE(p, nullptr);
    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(static_cast<int>(mode))));
}

void setVelToGate(PolyMidiModule& m, bool on) {
    auto* p = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&m, "velToGate"));
    ASSERT_NE(p, nullptr);
    p->setValueNotifyingHost(on ? 1.0f : 0.0f);
}

void pushBlock(PolyMidiModule& m, juce::AudioBuffer<float>& buffer, const std::vector<Event>& events) {
    juce::MidiBuffer midi;
    for (const auto& e : events)
        midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, e.velocity) : juce::MidiMessage::noteOff(1, e.note),
                      e.offset);
    m.processBlock(buffer, midi);
}

// Renders a fixed MIDI script through a freshly prepared module and returns every sample it
// produced, so two runs of the same script can be compared sample for sample.
std::vector<float> render(PolyMidiModule::StealMode mode, const std::vector<std::vector<Event>>& blocks) {
    PolyMidiModule m;
    m.prepareToPlay(44100.0, kBlockSize);
    setStealMode(m, mode);

    std::vector<float> out;
    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);
    for (const auto& block : blocks) {
        pushBlock(m, buffer, block);
        for (int ch = 0; ch < kNumChannels; ++ch)
            out.insert(out.end(), buffer.getReadPointer(ch), buffer.getReadPointer(ch) + kBlockSize);
    }
    return out;
}

// A >8-note chord (so voices get stolen) followed by more notes and some releases.
std::vector<std::vector<Event>> stealScript() {
    std::vector<Event> chord;
    for (int i = 0; i < 12; ++i)
        chord.push_back({60 + i, i * 8, true}); // 12 notes into 8 voices — 4 steals inside one block
    return {chord,
            {{72, 0, true}, {73, 64, true}, {74, 128, true}},
            {{60, 0, false}, {61, 32, false}},
            {{80, 0, true}, {81, 100, true}, {82, 200, true}, {83, 300, true}}};
}

} // namespace

class PolyMidiModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<PolyMidiModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<PolyMidiModule> module;
};

TEST_F(PolyMidiModuleTest, AcceptsMidi) { EXPECT_TRUE(module->acceptsMidi()); }

TEST_F(PolyMidiModuleTest, NoteOnAllocatesVoice) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

    juce::AudioBuffer<float> buffer(16, 512);
    module->processBlock(buffer, midi);

    // After Note On, at least one voice should be active
    uint8_t mask = module->getActiveVoiceMask();
    EXPECT_NE(mask, 0);
}

TEST_F(PolyMidiModuleTest, NoteOffDeactivatesVoice) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

    juce::AudioBuffer<float> buffer(16, 512);
    module->processBlock(buffer, midi);

    EXPECT_NE(module->getActiveVoiceMask(), 0);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    module->processBlock(buffer, midi);

    // Note off should immediately set voice inactive (though it might still have smoothed release in audio)
    EXPECT_EQ(module->getActiveVoiceMask(), 0);
}

TEST_F(PolyMidiModuleTest, VoiceStealingLRU) {
    juce::AudioBuffer<float> buffer(16, 512);

    // Switch on 8 voices, one per block. No sleep needed: voice age is a sample counter, not the
    // wall clock (issue #198).
    for (int i = 0; i < 8; ++i)
        pushBlock(*module, buffer, {{60 + i, 0, true}});

    EXPECT_EQ(module->getActiveVoiceMask(), 0xFF);

    // 9th note steals the least recently used voice — voice 0, which holds note 60.
    pushBlock(*module, buffer, {{72, 0, true}});

    EXPECT_EQ(module->getActiveVoiceMask(), 0xFF);
    EXPECT_NEAR(voicePitch(buffer, 0), noteHz(72), 1.0f);
    EXPECT_NEAR(voicePitch(buffer, 1), noteHz(61), 1.0f) << "only the oldest voice should be stolen";
}

// The parameter must default to the pre-#198 behaviour so patches saved without it load unchanged.
TEST_F(PolyMidiModuleTest, VoiceStealDefaultsToOldest) {
    EXPECT_EQ(module->getStealMode(), PolyMidiModule::StealMode::Oldest);

    auto* p = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "voiceSteal"));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->choices, juce::StringArray({"Oldest", "Round-Robin", "Random"}));
    EXPECT_EQ(p->getIndex(), 0);
}

// Issue #198's degenerate case: notes arriving inside a single block used to share one millisecond
// stamp, so every steal in a chord hit voice 0. Sample-offset stamps order them correctly.
TEST_F(PolyMidiModuleTest, SameBlockNotesStealInArrivalOrder) {
    juce::AudioBuffer<float> buffer(16, 512);

    for (int i = 0; i < 8; ++i)
        pushBlock(*module, buffer, {{60 + i, 0, true}});
    ASSERT_EQ(module->getActiveVoiceMask(), 0xFF);

    // Three steals inside one block, at distinct sample offsets.
    pushBlock(*module, buffer, {{72, 0, true}, {73, 64, true}, {74, 128, true}});

    EXPECT_EQ(module->getActiveVoiceMask(), 0xFF);
    EXPECT_NEAR(voicePitch(buffer, 0), noteHz(72), 1.0f);
    EXPECT_NEAR(voicePitch(buffer, 1), noteHz(73), 1.0f);
    EXPECT_NEAR(voicePitch(buffer, 2), noteHz(74), 1.0f);
    EXPECT_NEAR(voicePitch(buffer, 3), noteHz(63), 1.0f) << "voice 3 was not old enough to be stolen";
}

// A chord larger than the voice count, delivered in one block, must also steal in arrival order.
TEST_F(PolyMidiModuleTest, OverfullChordInOneBlockStealsInArrivalOrder) {
    juce::AudioBuffer<float> buffer(16, 512);

    std::vector<Event> chord;
    for (int i = 0; i < 11; ++i)
        chord.push_back({60 + i, i * 8, true}); // 11 notes, 8 voices → notes 60-62 get stolen

    pushBlock(*module, buffer, chord);

    EXPECT_EQ(module->getActiveVoiceMask(), 0xFF);
    EXPECT_NEAR(voicePitch(buffer, 0), noteHz(68), 1.0f);
    EXPECT_NEAR(voicePitch(buffer, 1), noteHz(69), 1.0f);
    EXPECT_NEAR(voicePitch(buffer, 2), noteHz(70), 1.0f);
    EXPECT_NEAR(voicePitch(buffer, 3), noteHz(63), 1.0f);
}

// Issue #198's headline property: identical input renders identically, every time. Note this alone
// is a weak detector — wall-clock stamping fails it only when a millisecond boundary happens to
// fall between the two runs. The tests above are the deterministic ones, because they pin *which*
// voice gets stolen.
TEST_F(PolyMidiModuleTest, RenderIsDeterministicAcrossRuns) {
    for (auto mode : {PolyMidiModule::StealMode::Oldest, PolyMidiModule::StealMode::RoundRobin,
                      PolyMidiModule::StealMode::Random}) {
        const auto first = render(mode, stealScript());
        juce::Thread::sleep(3); // a wall-clock stamp would land in a different millisecond here
        const auto second = render(mode, stealScript());

        ASSERT_EQ(first.size(), second.size());
        EXPECT_EQ(first, second) << "voice stealing must not depend on when the render happens";
    }
}

TEST_F(PolyMidiModuleTest, RoundRobinCyclesVoicesInOrder) {
    juce::AudioBuffer<float> buffer(16, 512);
    setStealMode(*module, PolyMidiModule::StealMode::RoundRobin);

    for (int i = 0; i < 8; ++i)
        pushBlock(*module, buffer, {{60 + i, 0, true}});

    // Re-trigger note 60 so voice 0 is the *newest*: LRU would now steal voice 1 first, round-robin
    // still starts at voice 0.
    pushBlock(*module, buffer, {{60, 0, true}});
    pushBlock(*module, buffer, {{80, 0, true}, {81, 64, true}, {82, 128, true}});

    EXPECT_NEAR(voicePitch(buffer, 0), noteHz(80), 1.0f);
    EXPECT_NEAR(voicePitch(buffer, 1), noteHz(81), 1.0f);
    EXPECT_NEAR(voicePitch(buffer, 2), noteHz(82), 1.0f);

    // The cursor keeps walking rather than restarting at the oldest voice.
    pushBlock(*module, buffer, {{83, 0, true}});
    EXPECT_NEAR(voicePitch(buffer, 3), noteHz(83), 1.0f);
}

// Random stealing is reproducible (asserted above for the render) *and* actually random: a
// least-recently-used or round-robin walk visits each of the 8 voices exactly once over 8 steals,
// so the eight stealing notes would all survive. A PRNG repeats and skips voices.
TEST_F(PolyMidiModuleTest, RandomStealIsReproducibleButNotSequential) {
    juce::AudioBuffer<float> buffer(16, 512);
    setStealMode(*module, PolyMidiModule::StealMode::Random);

    for (int i = 0; i < 8; ++i)
        pushBlock(*module, buffer, {{60 + i, 0, true}});
    for (int i = 0; i < 8; ++i)
        pushBlock(*module, buffer, {{80 + i, 0, true}});

    int survivingStealers = 0;
    for (int v = 0; v < 8; ++v)
        for (int n = 80; n < 88; ++n)
            if (std::abs(voicePitch(buffer, v) - noteHz(n)) < 1.0f)
                ++survivingStealers;

    EXPECT_LT(survivingStealers, 8) << "random stealing should revisit some voices, unlike LRU/round-robin";
    EXPECT_GT(survivingStealers, 0);
}

TEST_F(PolyMidiModuleTest, AllNotesOff) {
    juce::AudioBuffer<float> buffer(16, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0);
    module->processBlock(buffer, midi);

    EXPECT_NE(module->getActiveVoiceMask(), 0);

    midi.clear();
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    module->processBlock(buffer, midi);

    EXPECT_EQ(module->getActiveVoiceMask(), 0);
}

TEST_F(PolyMidiModuleTest, RenderChunk) {
    juce::AudioBuffer<float> buffer(16, 512);
    juce::MidiBuffer midi;

    // Note 60 = 261.63 Hz
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    module->processBlock(buffer, midi);

    // Voice 0 should have frequency (~261) in pitch channel (0) and 1.0 in gate (8)
    // Wait, smoothed value takes time. Let's check after some samples.
    EXPECT_GT(buffer.getSample(0, 511), 200.0f);
    EXPECT_GT(buffer.getSample(8, 511), 0.9f);
}

TEST_F(PolyMidiModuleTest, HasSixteenOutputChannels) { EXPECT_EQ(module->getTotalNumOutputChannels(), 16); }

// ===========================================================================
// Poly note contract — machine-generated MIDI re-articulates.
//
// Every path that (re)starts a note on a voice whose gate is still up — same-pitch retrigger, voice
// steal, and reuse of a just-released voice — must produce a gate edge a downstream envelope can
// see: an instant drop to 0 at the event sample, then a minimum 1 ms low window before the rise.
// ===========================================================================

// Defect 1: a repeated pitch used to refresh the voice silently, leaving the gate pinned high.
TEST_F(PolyMidiModuleTest, SamePitchRetriggerProducesGateEdge) {
    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);

    pushBlock(*module, buffer, {{60, 0, true}});
    pushBlock(*module, buffer, {}); // ~23 ms in total — the 5 ms gate ramp has long settled
    ASSERT_NEAR(voiceGate(buffer, 0), 1.0f, 1.0e-4f);
    const uint8_t maskBefore = module->getActiveVoiceMask();

    constexpr int kEvent = 128;
    pushBlock(*module, buffer, {{60, kEvent, true}});

    EXPECT_GT(gateAt(buffer, 0, kEvent - 1), 0.9f) << "gate stays high right up to the event sample";
    EXPECT_FLOAT_EQ(gateAt(buffer, 0, kEvent), 0.0f) << "a retrigger drops the gate instantly, not smoothly";
    for (int s = kEvent; s < kEvent + kMinGateGap; ++s)
        ASSERT_LE(gateAt(buffer, 0, s), 0.05f) << "gate must stay low across the whole 1 ms gap, at sample " << s;

    const int rise = firstGateRise(buffer, 0, kEvent);
    ASSERT_GE(rise, kEvent + kMinGateGap) << "the gate may not reopen before the gap has elapsed";
    EXPECT_LT(rise - kEvent, static_cast<int>(kSampleRate * 0.010)) << "and must be back up within ~10 ms";

    EXPECT_EQ(module->getActiveVoiceMask(), maskBefore) << "a retrigger reuses the same voice";
    EXPECT_NEAR(voicePitch(buffer, 0), noteHz(60), 1.0f);
}

// Defect 2: stealing wrote a new note into a still-active voice, so it glided instead of re-attacking.
TEST_F(PolyMidiModuleTest, StolenVoiceReattacks) {
    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);

    for (int i = 0; i < 8; ++i)
        pushBlock(*module, buffer, {{60 + i, 0, true}});
    ASSERT_EQ(module->getActiveVoiceMask(), 0xFF);
    ASSERT_NEAR(voiceGate(buffer, 0), 1.0f, 1.0e-4f);

    constexpr int kEvent = 128;
    pushBlock(*module, buffer, {{72, kEvent, true}}); // steals voice 0 (least recently used)

    EXPECT_FLOAT_EQ(gateAt(buffer, 0, kEvent), 0.0f);
    for (int s = kEvent; s < kEvent + kMinGateGap; ++s)
        ASSERT_LE(gateAt(buffer, 0, s), 0.05f) << "stolen voice must hold the gate low, at sample " << s;

    const int rise = firstGateRise(buffer, 0, kEvent);
    ASSERT_GE(rise, kEvent + kMinGateGap);
    EXPECT_LT(rise - kEvent, static_cast<int>(kSampleRate * 0.010));

    EXPECT_NEAR(voicePitch(buffer, 0), noteHz(72), 1.0f) << "the stolen voice takes the new pitch";
    EXPECT_NEAR(voiceGate(buffer, 1), 1.0f, 1.0e-4f) << "voices that were not stolen keep sounding";
}

// Chord-exceeds-voices policy, pinned: nine simultaneous notes on eight voices in Oldest mode drop
// the chord's *first* note — stamps are strictly increasing, so note 1 is the oldest by one sample.
TEST_F(PolyMidiModuleTest, NineNoteChordPolicy) {
    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);

    std::vector<Event> chord;
    for (int i = 0; i < 9; ++i)
        chord.push_back({60 + i, 0, true}); // all nine at the same sample offset
    pushBlock(*module, buffer, chord);

    EXPECT_EQ(module->getActiveVoiceMask(), 0xFF);
    EXPECT_NEAR(voicePitch(buffer, 0), noteHz(68), 1.0f) << "note 1 is stolen by note 9";
    for (int v = 1; v < 8; ++v)
        EXPECT_NEAR(voicePitch(buffer, v), noteHz(60 + v), 1.0f) << "notes 2..8 keep their voices, voice " << v;
}

// Defect 4: a clip boundary emits note-off and note-on for one pitch at the same sample. The freed
// voice is reused immediately, so without the enforced gap the edge would smooth away to nothing.
TEST_F(PolyMidiModuleTest, AdjacentOffOnSamePitchLeavesGap) {
    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);

    pushBlock(*module, buffer, {{60, 0, true}});
    pushBlock(*module, buffer, {});
    ASSERT_NEAR(voiceGate(buffer, 0), 1.0f, 1.0e-4f);

    constexpr int kEvent = 128;
    pushBlock(*module, buffer, {{60, kEvent, false}, {60, kEvent, true}}); // off first, same sample

    EXPECT_FLOAT_EQ(gateAt(buffer, 0, kEvent), 0.0f);
    for (int s = kEvent; s < kEvent + kMinGateGap; ++s)
        ASSERT_LE(gateAt(buffer, 0, s), 0.05f) << "off/on at one sample still yields a 1 ms gap, at sample " << s;

    const int rise = firstGateRise(buffer, 0, kEvent);
    ASSERT_GE(rise, kEvent + kMinGateGap);
    EXPECT_LT(rise - kEvent, static_cast<int>(kSampleRate * 0.010));
    EXPECT_EQ(module->getActiveVoiceMask(), 0x01) << "the same voice is re-used, not a second one";
}

// Default-off keeps the gate a plain 0/1 flag, so presets saved before the parameter existed render
// byte-identically.
TEST_F(PolyMidiModuleTest, VelocityToGateOffByDefault) {
    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);
    pushBlock(*module, buffer, {{60, 0, true, 0.25f}});
    EXPECT_FLOAT_EQ(voiceGate(buffer, 0), 1.0f);
}

TEST_F(PolyMidiModuleTest, VelocityHonouredWhenEnabled) {
    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);
    setVelToGate(*module, true);

    pushBlock(*module, buffer, {{60, 0, true, 0.5f}});
    EXPECT_NEAR(voiceGate(buffer, 0), 0.5f, 0.01f);

    pushBlock(*module, buffer, {{64, 0, true, 1.0f}});
    EXPECT_NEAR(voiceGate(buffer, 1), 1.0f, 0.01f);
    EXPECT_NEAR(voiceGate(buffer, 0), 0.5f, 0.01f) << "a held voice keeps its own level";
}

// The gap is a repair for a gate that is still up — a voice at rest must not pay for it.
TEST_F(PolyMidiModuleTest, FreshNoteOnIdleVoiceHasNoArtificialGap) {
    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);
    pushBlock(*module, buffer, {{60, 0, true}});

    EXPECT_GT(gateAt(buffer, 0, 0), 0.0f) << "an idle voice starts rising on the event sample itself";
    EXPECT_GT(gateAt(buffer, 0, 2), gateAt(buffer, 0, 0));
    EXPECT_GT(gateAt(buffer, 0, kMinGateGap - 1), 0.1f) << "no dead time where the gap would have been";
}

// ===========================================================================
// End-to-end: the gate edge as a downstream ADSR actually sees it.
// ===========================================================================

namespace {

void setParam(juce::AudioProcessor& p, const juce::String& id, float value) {
    auto* param = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&p, id));
    ASSERT_NE(param, nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

// Runs PolyMidi -> poly ADSR block by block (voice-0 gate CV into the ADSR's gate input) and
// returns the voice-0 envelope for the whole run.
std::vector<float> renderPolyIntoAdsr(const std::vector<std::vector<Event>>& blocks) {
    PolyMidiModule poly;
    ADSRModule adsr;
    poly.prepareToPlay(kSampleRate, kBlockSize);
    adsr.prepareToPlay(kSampleRate, kBlockSize);
    setParam(adsr, "poly", 1.0f);
    setParam(adsr, "attack", 0.01f); // parameter minimum — 441 samples
    setParam(adsr, "decay", 0.01f);
    setParam(adsr, "sustain", 1.0f);
    setParam(adsr, "release", 0.01f);

    juce::AudioBuffer<float> polyBuf(kNumChannels, kBlockSize);
    juce::AudioBuffer<float> adsrBuf(8, kBlockSize);
    std::vector<float> env;

    for (const auto& events : blocks) {
        pushBlock(poly, polyBuf, events);
        adsrBuf.clear();
        adsrBuf.copyFrom(0, 0, polyBuf, 8, 0, kBlockSize); // voice-0 gate CV -> ADSR gate input
        juce::MidiBuffer noMidi;
        adsr.processBlock(adsrBuf, noMidi);
        env.insert(env.end(), adsrBuf.getReadPointer(0), adsrBuf.getReadPointer(0) + kBlockSize);
    }
    return env;
}

float minOverBlock(const std::vector<float>& env, int block, int from = 0) {
    float lowest = 1.0f;
    for (int s = from; s < kBlockSize; ++s)
        lowest = std::min(lowest, env[(size_t)(block * kBlockSize + s)]);
    return lowest;
}

float endOfBlock(const std::vector<float>& env, int block) { return env[(size_t)((block + 1) * kBlockSize - 1)]; }

} // namespace

// Documents the *real* end-to-end behaviour, which is asymmetric on purpose:
//
// ADSRModule's poly branch samples the gate CV exactly once per block (`gateData[0]`, ADSRModule.h
// §"Edge detection at start of block"). So PolyMidi's 1 ms gap re-articulates the envelope only
// when it covers a block boundary; a retrigger in the middle of a block is invisible to that ADSR,
// because the gate is back up before the next block starts. This is why the contract pins the gap
// in absolute samples rather than trusting the smoothed shape, and it is what the Track In node
// must schedule against.
//
// The second half of this test pins a limitation, not a desired behaviour: if ADSRModule ever gains
// per-sample edge detection, delete it rather than "fixing" it.
TEST(PolyMidiToAdsrTest, RetriggerReArticulatesAdsrWhenTheGapSpansABlockBoundary) {
    // Retrigger 8 samples before the block ends: the 44-sample gap straddles the boundary, so block
    // 3 starts with the gate low and the ADSR sees fall-then-rise on consecutive blocks.
    const auto spanning = renderPolyIntoAdsr({{{60, 0, true}}, {}, {{60, kBlockSize - 8, true}}, {}, {}});
    EXPECT_GT(endOfBlock(spanning, 1), 0.9f) << "envelope should be sustaining before the retrigger";
    EXPECT_LT(minOverBlock(spanning, 3, kBlockSize - 32), 0.05f) << "ADSR must release on the gate's falling edge";
    EXPECT_GT(endOfBlock(spanning, 4), 0.9f) << "and re-attack on the rise — a real retrigger";

    // Same retrigger in mid-block: the gate is fully back up before block 3 is sampled, so this ADSR
    // never observes the edge and the note simply sustains through.
    const auto midBlock = renderPolyIntoAdsr({{{60, 0, true}}, {}, {{60, 128, true}}, {}, {}});
    EXPECT_GT(minOverBlock(midBlock, 3), 0.9f) << "known: ADSR samples the gate once per block";
    EXPECT_GT(minOverBlock(midBlock, 4), 0.9f);
}
