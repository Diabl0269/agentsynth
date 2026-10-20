#include "Modules/PolySequencerModule.h"
#include "Modules/SequencerModule.h"
#include "SequencerTestSupport.h"

using namespace seqtest;

namespace {

constexpr int kBlock = 512;
constexpr double kSampleRate = 48000.0;
constexpr int kBeat = 24000; // samples per beat at 120 BPM, 48 kHz

class SequencerSyncedTest : public ::testing::Test {
protected:
    void SetUp() override {
        transport.prepare(kSampleRate, kBlock);
        seq.prepareToPlay(kSampleRate, kBlock);
        seq.setPlayHead(&transport);
        run(seq, true);
        setNumber(seq, "syncToTransport", 1.0f);
    }

    /** Ticks the transport and renders `blocks` blocks; events carry the absolute sample since the first tick. */
    std::vector<Event> play(int blocks, std::vector<synth::BlockTimeInfo>* infos = nullptr) {
        std::vector<Event> events;
        for (int b = 0; b < blocks; ++b) {
            const auto info = transport.tick(kBlock);
            if (infos != nullptr)
                infos->push_back(info);
            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::MidiBuffer midi;
            seq.processBlock(buffer, midi);
            collect(midi, (juce::int64)b * kBlock, events);
        }
        return events;
    }

    synth::TransportService transport;
    SequencerModule seq;
};

} // namespace

TEST_F(SequencerSyncedTest, StepsFollowTransportBeatsSampleAccurately) {
    transport.play();
    const auto ons = onsOnly(play(60));
    ASSERT_GE(ons.size(), 2u);
    EXPECT_EQ(ons[0].sample, 0);
    EXPECT_EQ(ons[0].note, 53);
    EXPECT_EQ(ons[1].sample, kBeat);
    EXPECT_EQ(ons[1].note, 65);
}

TEST_F(SequencerSyncedTest, IgnoresTheBpmParameter) {
    setNumber(seq, "bpm", 240.0f);
    transport.play();
    const auto ons = onsOnly(play(60));
    ASSERT_GE(ons.size(), 2u);
    EXPECT_EQ(ons[1].sample - ons[0].sample, kBeat);
}

TEST_F(SequencerSyncedTest, RateDividesTheBeat) {
    setChoice(seq, "rate", "1/8");
    transport.play();
    const auto ons = onsOnly(play(60));
    ASSERT_GE(ons.size(), 3u);
    EXPECT_EQ(ons[1].sample, kBeat / 2);
    EXPECT_EQ(ons[2].sample, kBeat);
}

TEST_F(SequencerSyncedTest, LocateJumpsToTheStepOfThatBeat) {
    transport.play();
    transport.locateBeat(5.5);
    const auto ons = onsOnly(play(60));
    ASSERT_FALSE(ons.empty());
    EXPECT_EQ(ons[0].note, 54); // beat 6 -> step 6
    EXPECT_EQ(ons[0].sample, kBeat / 2);
}

TEST_F(SequencerSyncedTest, StopSendsTheNoteOffOnce) {
    transport.play();
    const auto first = play(1);
    ASSERT_EQ(onsOnly(first).size(), 1u);

    transport.stop();
    const auto stopped = play(1);
    ASSERT_EQ(offsOnly(stopped).size(), 1u);
    EXPECT_EQ(offsOnly(stopped)[0].note, 53);
    EXPECT_TRUE(play(5).empty());
}

TEST_F(SequencerSyncedTest, LoopWrapFiresTheLoopStartStepWithoutDoubleOrSkip) {
    transport.setLoop(0.0, 2.0, true);
    transport.play();
    std::vector<synth::BlockTimeInfo> infos;
    const auto events = play(300, &infos);

    int wraps = 0;
    for (const auto& info : infos)
        wraps += info.loopWrapSample >= 0 ? 1 : 0;
    ASSERT_GE(wraps, 2);

    const auto ons = onsOnly(events);
    ASSERT_GE(ons.size(), 4u);
    for (size_t i = 0; i < ons.size(); ++i)
        EXPECT_EQ(ons[i].note, i % 2 == 0 ? 53 : 65) << i;
}

TEST_F(SequencerSyncedTest, OneStepLoopRefiresAtEveryWrap) {
    setChoice(seq, "rate", "1/4");
    transport.setLoop(0.0, 1.0, true);
    transport.play();
    const auto ons = onsOnly(play(200)); // 200 blocks = 102400 samples, about 4.3 loops
    EXPECT_GE(ons.size(), 4u);
    for (const auto& on : ons)
        EXPECT_EQ(on.note, 53);
}

TEST_F(SequencerSyncedTest, ForeignPlayheadFallsBackToTheFreeRunningClock) {
    seq.setPlayHead(nullptr);
    const auto events = render(seq, 48000);
    EXPECT_FALSE(onsOnly(events).empty());
}

TEST_F(SequencerSyncedTest, PolySequencerLocksTheWholeChordTogether) {
    PolySequencerModule poly;
    poly.prepareToPlay(kSampleRate, kBlock);
    poly.setPlayHead(&transport);
    run(poly, true);
    setNumber(poly, "syncToTransport", 1.0f);
    auto pattern = poly.getPattern();
    pattern.steps[0].chord = synth::SequencerChord::Minor;
    poly.setPattern(pattern);

    transport.play();
    std::vector<Event> events;
    for (int b = 0; b < 4; ++b) {
        transport.tick(kBlock);
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        poly.processBlock(buffer, midi);
        collect(midi, (juce::int64)b * kBlock, events);
    }
    const auto ons = onsOnly(events);
    ASSERT_EQ(ons.size(), 3u);
    for (const auto& on : ons)
        EXPECT_EQ(on.sample, 0);
    EXPECT_EQ(ons[0].note, 48);
    EXPECT_EQ(ons[1].note, 51);
    EXPECT_EQ(ons[2].note, 55);
}
