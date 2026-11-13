// WavetableOscillatorModuleUnisonCableTests.cpp
// The Unison knob takes a cable on its own channel after the jacks. The cable is read on the sample a note starts and
// rounded to a whole voice count, and the count stays until the next note starts: a cable that moves mid-note changes
// nothing until then.

#include "WavetableOscillatorModuleTestFixture.h"
#include <functional>
#include <limits>

namespace {

constexpr int kNoteBlock = 2;

void setUnison(WavetableOscillatorModule& module, int voices) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, "unison"));
    ASSERT_NE(p, nullptr);
    p->setValueNotifyingHost(p->convertTo0to1((float)voices));
}

// Renders `blocks` blocks of channel 0. `cable(block)` is the Unison cable's value in each block, `noteBlocks` are the
// blocks a MIDI note-on lands in, and `beforeBlock` runs ahead of each block.
std::vector<float> renderCable(WavetableOscillatorModule& module, int blocks, const std::function<float(int)>& cable,
                               const std::vector<int>& noteBlocks,
                               const std::function<void(int)>& beforeBlock = nullptr) {
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b) {
        if (beforeBlock)
            beforeBlock(b);
        juce::AudioBuffer<float> block(module.getTotalNumInputChannels(), kBlockSize);
        block.clear();
        if (cable)
            juce::FloatVectorOperations::fill(block.getWritePointer(WavetableOscillatorModule::kUnisonCvChannel),
                                              cable(b), kBlockSize);
        juce::MidiBuffer midi;
        if (std::find(noteBlocks.begin(), noteBlocks.end(), b) != noteBlocks.end())
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 10);
        module.processBlock(block, midi);
        out.insert(out.end(), block.getReadPointer(0), block.getReadPointer(0) + kBlockSize);
    }
    return out;
}

double maxDifference(const std::vector<float>& a, const std::vector<float>& b, size_t from,
                     size_t to = std::numeric_limits<size_t>::max()) {
    double worst = 0.0;
    for (size_t i = from; i < a.size() && i < b.size() && i < to; ++i)
        worst = std::max(worst, (double)std::fabs(a[i] - b[i]));
    return worst;
}

} // namespace

TEST_F(WavetableOscillatorModuleTest, UnisonIsAKnobOnlyTargetOnAChannelAfterEveryExistingOne) {
    const auto targets = module->getModulationTargets();
    const auto it = std::find_if(targets.begin(), targets.end(), [](const auto& t) { return t.paramId == "unison"; });
    ASSERT_NE(it, targets.end());
    EXPECT_TRUE(it->knobOnly);
    EXPECT_EQ(it->channelIndex, 31);
    EXPECT_EQ(WavetableOscillatorModule::kUnisonCvChannel, 31);
    EXPECT_EQ(module->getTotalNumInputChannels(), 32);
    EXPECT_EQ(module->getInputPortLabel(31), "Unison");
    EXPECT_EQ(module->getVisibleInputPortCount(), WavetableOscillatorModule::kNumJacks) << "no jack on the card";
    EXPECT_EQ(module->mapInputChannel(31).role, PortRole::ModCV);

    // Every channel that existed before still sits where it did (saved cables, Audio R routings, the poly Gate fan).
    EXPECT_EQ(WavetableOscillatorModule::kPolyModCVBase, 8);
    EXPECT_EQ(WavetableOscillatorModule::kGateBase, 23);
    EXPECT_EQ(WavetableOscillatorModule::kRightBase, 23);
    EXPECT_EQ(module->rightAudioLegChannel(), 23);
    EXPECT_EQ(module->polyGateInputChannel(), 23);
    EXPECT_EQ(module->modulationChannelForParam("position"), 1);
    EXPECT_EQ(module->modulationChannelForParam("pan"), 14);
    for (const auto& t : targets)
        if (t.paramId != "unison")
            EXPECT_LT(t.channelIndex, 31) << t.paramId;
}

// Regression test for FRO1556: the Wavetable's Unison took no cable.
TEST_F(WavetableOscillatorModuleTest, ACableAtNoteStartSetsTheVoiceCountLikeTheKnob) {
    const auto cabled = renderCable(*module, 6, [](int b) { return b >= kNoteBlock ? 0.3f : 0.0f; }, {kNoteBlock});

    WavetableOscillatorModule knob;
    knob.prepareToPlay(kSampleRate, kBlockSize);
    const auto reference =
        renderCable(knob, 6, nullptr, {kNoteBlock}, [&](int b) { b == kNoteBlock ? setUnison(knob, 3) : void(); });

    EXPECT_LT(maxDifference(cabled, reference, (size_t)kNoteBlock * kBlockSize), 1e-6)
        << "cable 0.3 on a 1..8 knob is three voices";

    WavetableOscillatorModule plain;
    plain.prepareToPlay(kSampleRate, kBlockSize);
    const auto single = renderCable(plain, 6, nullptr, {kNoteBlock});
    EXPECT_GT(maxDifference(cabled, single, (size_t)kNoteBlock * kBlockSize), 0.01)
        << "three voices sound different from one";
}

TEST_F(WavetableOscillatorModuleTest, TheCableRoundsToTheNearestWholeVoice) {
    for (const auto& [cv, voices] : std::vector<std::pair<float, int>>{{0.34f, 3}, {0.4f, 4}, {0.45f, 4}, {1.0f, 8}}) {
        SCOPED_TRACE(cv);
        WavetableOscillatorModule cabled;
        cabled.prepareToPlay(kSampleRate, kBlockSize);
        const auto a = renderCable(cabled, 5, [cv](int) { return cv; }, {1});
        WavetableOscillatorModule knob;
        knob.prepareToPlay(kSampleRate, kBlockSize);
        setUnison(knob, voices);
        const auto b = renderCable(knob, 5, nullptr, {1});
        EXPECT_LT(maxDifference(a, b, 0), 1e-6);
    }
}

TEST_F(WavetableOscillatorModuleTest, ACableThatMovesMidNoteWaitsForTheNextNote) {
    // The note starts at block 1 with the cable at 0.3 (three voices); from block 3 it sits at 0.9 (seven).
    const auto cable = [](int b) { return b >= 3 ? 0.9f : 0.3f; };
    const auto cabled = renderCable(*module, 8, cable, {1, 5});

    WavetableOscillatorModule knob;
    knob.prepareToPlay(kSampleRate, kBlockSize);
    setUnison(knob, 3);
    const auto reference = renderCable(knob, 8, nullptr, {1, 5}, [&](int b) { b == 5 ? setUnison(knob, 7) : void(); });

    EXPECT_LT(maxDifference(cabled, reference, 0), 1e-6)
        << "three voices through blocks 1-4, seven from the note in block 5";

    WavetableOscillatorModule stays;
    stays.prepareToPlay(kSampleRate, kBlockSize);
    setUnison(stays, 3);
    const auto threeThroughout = renderCable(stays, 8, nullptr, {1, 5});
    EXPECT_LT(maxDifference(cabled, threeThroughout, 0, 5 * kBlockSize), 1e-6) << "nothing changed before the note";
    EXPECT_GT(maxDifference(cabled, threeThroughout, 5 * kBlockSize), 0.01) << "the second note is a different sound";
}

TEST_F(WavetableOscillatorModuleTest, WithoutACableTheKnobStillFollowsLiveInAHeldNote) {
    const auto a = renderCable(*module, 6, nullptr, {1}, [&](int b) { b == 3 ? setUnison(*module, 5) : void(); });
    WavetableOscillatorModule fixed;
    fixed.prepareToPlay(kSampleRate, kBlockSize);
    const auto b = renderCable(fixed, 6, nullptr, {1});
    EXPECT_GT(maxDifference(a, b, 3 * kBlockSize), 0.01) << "turning Unison during a note still changes the sound";
    EXPECT_LT(maxDifference(a, b, 0, 3 * kBlockSize), 1e-6);
}
