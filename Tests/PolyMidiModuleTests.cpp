#include "Modules/PolyMidiModule.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr int kBlockSize = 512;
constexpr int kNumChannels = 16;

float noteHz(int note) { return static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(note)); }

// The pitch/gate a voice is holding at the end of the last rendered block. Both are 5 ms-smoothed,
// so reading the final sample of a 512-sample block gives the settled value.
float voicePitch(const juce::AudioBuffer<float>& b, int voice) { return b.getSample(voice, b.getNumSamples() - 1); }

// One MIDI event inside a block.
struct Event {
    int note;
    int offset; // sample offset within the block
    bool on;
};

void setStealMode(PolyMidiModule& m, PolyMidiModule::StealMode mode) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&m, "voiceSteal"));
    ASSERT_NE(p, nullptr);
    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(static_cast<int>(mode))));
}

void pushBlock(PolyMidiModule& m, juce::AudioBuffer<float>& buffer, const std::vector<Event>& events) {
    juce::MidiBuffer midi;
    for (const auto& e : events)
        midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, 0.8f) : juce::MidiMessage::noteOff(1, e.note),
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
