#include "Modules/SequencerModule.h"
#include "SequencerTestSupport.h"
#include <algorithm>

using namespace seqtest;

namespace {

constexpr int kRate = 44100;
constexpr int kQuarter = 22050; // samples per step at 120 BPM, Rate 1/4

class SequencerModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        seq = std::make_unique<SequencerModule>();
        seq->prepareToPlay(kRate, 512);
    }
    std::unique_ptr<SequencerModule> seq;
};

} // namespace

TEST_F(SequencerModuleTest, StoppedByDefaultAndProducesMidi) {
    EXPECT_TRUE(seq->producesMidi());
    EXPECT_FALSE(seq->acceptsMidi());
    EXPECT_TRUE(render(*seq, 4096).empty());
}

TEST_F(SequencerModuleTest, ParametersAreTheWholePatternSettingsOnly) {
    std::vector<juce::String> ids;
    for (auto* p : seq->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            ids.push_back(withId->paramID);
    for (const char* id : {"run", "bpm", "syncToTransport", "steps", "rate", "transpose", "gate", "velocity", "chance",
                           "scaleRoot", "scale"})
        EXPECT_NE(std::find(ids.begin(), ids.end(), id), ids.end()) << id;
    for (const auto& id : ids) {
        EXPECT_FALSE(id.startsWith("Pitch")) << id;
        EXPECT_FALSE(id.startsWith("F.Env")) << id;
        EXPECT_FALSE(id.startsWith("Gate ")) << id;
    }
    auto* sync = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(seq.get(), "syncToTransport"));
    ASSERT_NE(sync, nullptr);
    EXPECT_FALSE(sync->get());
}

// The old free-running clock checked step boundaries once per block, so events slid by up to a block. The new clock
// is sample-accurate: step k starts exactly at k * 22050, its gate ends half a step later, and the filter CC goes out
// just before the note.
TEST_F(SequencerModuleTest, DefaultPatternPlaysTheOldEightNotesSampleAccurately) {
    run(*seq, true);
    const int total = 9 * kQuarter + 1000;
    const auto events = render(*seq, total);

    const int pitches[8] = {53, 65, 54, 61, 53, 57, 54, 60};
    std::vector<Event> expected;
    for (int k = 0; k <= 9; ++k) {
        const juce::int64 start = (juce::int64)k * kQuarter;
        expected.push_back({start, false, false, true, 64}); // filter 0.5 -> CC 74 value 64
        expected.push_back({start, true, false, false, pitches[k % 8]});
        if (start + kQuarter / 2 < total)
            expected.push_back({start + kQuarter / 2, false, true, false, pitches[k % 8]});
    }
    ASSERT_EQ(events.size(), expected.size());
    for (size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(events[i].sample, expected[i].sample) << "event " << i;
        EXPECT_EQ(events[i].isOn, expected[i].isOn) << "event " << i;
        EXPECT_EQ(events[i].isOff, expected[i].isOff) << "event " << i;
        EXPECT_EQ(events[i].isCc, expected[i].isCc) << "event " << i;
        EXPECT_EQ(events[i].note, expected[i].note) << "event " << i;
    }
}

TEST_F(SequencerModuleTest, EventsDoNotDependOnTheBlockSize) {
    run(*seq, true);
    const auto reference = render(*seq, 6 * kQuarter, 512);
    for (int blockSize : {64, 333, 1024, 4096}) {
        seq->prepareToPlay(kRate, blockSize);
        const auto other = render(*seq, 6 * kQuarter, blockSize);
        ASSERT_EQ(other.size(), reference.size()) << blockSize;
        for (size_t i = 0; i < other.size(); ++i) {
            EXPECT_EQ(other[i].sample, reference[i].sample) << blockSize << " event " << i;
            EXPECT_EQ(other[i].note, reference[i].note) << blockSize << " event " << i;
            EXPECT_EQ(other[i].isOn, reference[i].isOn) << blockSize << " event " << i;
        }
    }
}

TEST_F(SequencerModuleTest, FortyStepsAtTripletEighthsPlayTheWholePatternThenLoop) {
    auto pattern = seq->getPattern();
    for (int i = 0; i < 40; ++i)
        pattern.steps[(size_t)i].pitch = (std::uint8_t)(30 + i);
    seq->setPattern(pattern);
    setNumber(*seq, "steps", 40);
    setChoice(*seq, "rate", "1/8T");
    run(*seq, true);

    constexpr int kStep = 7350; // 1/8 triplet = 1/3 beat = 7350 samples at 120 BPM
    const auto ons = onsOnly(render(*seq, 41 * kStep + 100));
    ASSERT_EQ(ons.size(), 42u);
    for (int k = 0; k < 41; ++k) {
        EXPECT_EQ(ons[(size_t)k].sample, (juce::int64)k * kStep) << k;
        EXPECT_EQ(ons[(size_t)k].note, 30 + (k % 40)) << k;
    }
}

TEST_F(SequencerModuleTest, PlayheadFollowsTheFiredStep) {
    run(*seq, true);
    render(*seq, 10);
    EXPECT_EQ(seq->currentActiveStep.load(), 0);
    render(*seq, kQuarter);
    EXPECT_EQ(seq->currentActiveStep.load(), 1);
}

TEST_F(SequencerModuleTest, BpmChangesTheStepLength) {
    setNumber(*seq, "bpm", 240.0f);
    run(*seq, true);
    const auto ons = onsOnly(render(*seq, 4 * 11025 + 10));
    ASSERT_GE(ons.size(), 4u);
    EXPECT_EQ(ons[1].sample - ons[0].sample, 11025);
}

TEST_F(SequencerModuleTest, StepOffIsASilentRest) {
    auto pattern = seq->getPattern();
    pattern.steps[0].on = false;
    seq->setPattern(pattern);
    run(*seq, true);
    const auto ons = onsOnly(render(*seq, 2 * kQuarter));
    ASSERT_FALSE(ons.empty());
    EXPECT_EQ(ons[0].sample, kQuarter); // step 1 is the first sound
    EXPECT_EQ(ons[0].note, 65);
}

TEST_F(SequencerModuleTest, StopSendsOneNoteOffThenStaysSilent) {
    run(*seq, true);
    const auto first = render(*seq, 512);
    ASSERT_EQ(onsOnly(first).size(), 1u);
    const int held = onsOnly(first)[0].note;

    run(*seq, false);
    const auto stopped = render(*seq, 512);
    ASSERT_EQ(offsOnly(stopped).size(), 1u);
    EXPECT_EQ(offsOnly(stopped)[0].note, held);
    EXPECT_EQ(offsOnly(stopped)[0].sample, 0);
    EXPECT_TRUE(render(*seq, 2048).empty());
}

TEST_F(SequencerModuleTest, RestartBeginsAtStepZeroWithNoStrayNoteOff) {
    run(*seq, true);
    render(*seq, 3 * kQuarter);
    run(*seq, false);
    render(*seq, 512);
    run(*seq, true);
    const auto events = render(*seq, 512);
    ASSERT_EQ(onsOnly(events).size(), 1u);
    EXPECT_EQ(onsOnly(events)[0].note, 53);
    EXPECT_EQ(onsOnly(events)[0].sample, 0);
    EXPECT_TRUE(offsOnly(events).empty());
}

TEST_F(SequencerModuleTest, BypassReleasesHeldNotesOnce) {
    run(*seq, true);
    render(*seq, 512);
    seq->setBypassed(true);
    // Bypass takes effect over a few blocks (the click-free transition); the held note ends once, nothing new starts.
    const auto events = render(*seq, 8 * 512);
    EXPECT_EQ(offsOnly(events).size(), 1u);
    EXPECT_TRUE(onsOnly(events).empty());
}

TEST_F(SequencerModuleTest, ScaleSnapsAtPlayTimeAndLeavesTheWrittenPitchAlone) {
    auto pattern = patternOf(*seq, {64}); // E
    seq->setPattern(pattern);
    setNumber(*seq, "steps", 1);
    run(*seq, true);

    EXPECT_EQ(onsOnly(render(*seq, 512))[0].note, 64); // No scale: E

    seq->prepareToPlay(kRate, 512);
    setChoice(*seq, "scale", "Natural Minor"); // C natural minor has Eb, not E
    EXPECT_EQ(onsOnly(render(*seq, 512))[0].note, 63);
    EXPECT_EQ(seq->getPattern().steps[0].pitch, 64);

    seq->prepareToPlay(kRate, 512);
    setNumber(*seq, "transpose", 1.0f); // E + 1 = F, which the scale has
    EXPECT_EQ(onsOnly(render(*seq, 512))[0].note, 65);
}

TEST_F(SequencerModuleTest, CustomScaleUsesTheMaskInThePattern) {
    auto pattern = patternOf(*seq, {61});
    pattern.customMask = 0b000000000101; // root and the major second
    seq->setPattern(pattern);
    setNumber(*seq, "steps", 1);
    setChoice(*seq, "scale", "Custom");
    run(*seq, true);
    EXPECT_EQ(onsOnly(render(*seq, 512))[0].note, 60); // C# snaps down to C (D is equally near; lower wins)
}

TEST_F(SequencerModuleTest, TransposeAndKeyMoveTheScale) {
    auto pattern = patternOf(*seq, {60});
    seq->setPattern(pattern);
    setNumber(*seq, "steps", 1);
    setNumber(*seq, "transpose", 5.0f);
    run(*seq, true);
    EXPECT_EQ(onsOnly(render(*seq, 512))[0].note, 65);

    seq->prepareToPlay(kRate, 512);
    setChoice(*seq, "scaleRoot", "D");
    setChoice(*seq, "scale", "Major"); // D major: F is out, F# (66) and E (64) are in; tie goes down
    EXPECT_EQ(onsOnly(render(*seq, 512))[0].note, 64);
}

TEST_F(SequencerModuleTest, VelocityAndGateAmountsScaleTheSteps) {
    auto pattern = seq->getPattern();
    pattern.steps[0].velocity = 80;
    pattern.steps[0].gate = 0.5f;
    seq->setPattern(pattern);
    setNumber(*seq, "velocity", 0.5f);
    setNumber(*seq, "gate", 0.5f);
    run(*seq, true);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    seq->processBlock(buffer, midi);
    int velocity = -1;
    for (const auto meta : midi)
        if (meta.getMessage().isNoteOn())
            velocity = meta.getMessage().getVelocity();
    EXPECT_EQ(velocity, 40);

    const auto offs = offsOnly(render(*seq, kQuarter));
    ASSERT_FALSE(offs.empty());
    EXPECT_NEAR((double)offs[0].sample, kQuarter * 0.25, 600.0); // block 1 was already consumed above
}

TEST_F(SequencerModuleTest, ChanceIsDeterministicAndRoughlyProportional) {
    auto pattern = seq->getPattern();
    for (auto& step : pattern.steps)
        step.chance = 50;
    seq->setPattern(pattern);
    setNumber(*seq, "steps", 128);
    setChoice(*seq, "rate", "1/16");
    run(*seq, true);

    constexpr int kSteps = 128;
    constexpr int kStep = kQuarter / 4;
    const auto a = onsOnly(render(*seq, kSteps * kStep));
    EXPECT_GT(a.size(), 40u);
    EXPECT_LT(a.size(), 88u);

    seq->prepareToPlay(kRate, 512);
    const auto b = onsOnly(render(*seq, kSteps * kStep));
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
        EXPECT_EQ(a[i].sample, b[i].sample);

    setNumber(*seq, "chance", 0.0f);
    seq->prepareToPlay(kRate, 512);
    EXPECT_TRUE(onsOnly(render(*seq, kSteps * kStep)).empty());
}

TEST_F(SequencerModuleTest, ConditionTwoOfFourPlaysOnTheSecondLoopOfFour) {
    auto pattern = seq->getPattern();
    pattern.steps[0].condHit = 2;
    pattern.steps[0].condOf = 4;
    seq->setPattern(pattern);
    setNumber(*seq, "steps", 1);
    run(*seq, true);
    const auto ons = onsOnly(render(*seq, 10 * kQuarter));
    ASSERT_EQ(ons.size(), 3u);
    EXPECT_EQ(ons[0].sample, 1 * kQuarter);
    EXPECT_EQ(ons[1].sample, 5 * kQuarter);
    EXPECT_EQ(ons[2].sample, 9 * kQuarter);
}

TEST_F(SequencerModuleTest, RepeatThreeGivesThreeEvenHitsInsideTheStep) {
    auto pattern = seq->getPattern();
    pattern.steps[0].repeat = 3;
    seq->setPattern(pattern);
    run(*seq, true);
    const auto events = render(*seq, kQuarter - 10);
    const auto ons = onsOnly(events);
    ASSERT_EQ(ons.size(), 3u);
    EXPECT_EQ(ons[0].sample, 0);
    EXPECT_EQ(ons[1].sample, 7350);
    EXPECT_EQ(ons[2].sample, 14700);
    const auto offs = offsOnly(events);
    ASSERT_EQ(offs.size(), 3u);
    EXPECT_EQ(offs[0].sample, 3675); // gate 0.5 of a third of a step
}

TEST_F(SequencerModuleTest, NudgeMovesTheEventAndAnEarlyNudgeOnLoopTwoLandsInThePreviousStep) {
    auto late = seq->getPattern();
    late.steps[0].nudge = 0.25f;
    seq->setPattern(late);
    run(*seq, true);
    auto ons = onsOnly(render(*seq, kQuarter));
    ASSERT_EQ(ons.size(), 1u);
    EXPECT_NEAR((double)ons[0].sample, kQuarter * 0.25, 1.0);

    seq->prepareToPlay(kRate, 512);
    auto early = seq->getPattern();
    early.steps[0].nudge = -0.25f;
    seq->setPattern(early);
    setNumber(*seq, "steps", 2);
    ons = onsOnly(render(*seq, 3 * kQuarter + 100));
    // k=0 would be before the start and is skipped; k=2 (loop two, step 0) fires a quarter step early.
    ASSERT_EQ(ons.size(), 3u);
    EXPECT_EQ(ons[0].note, 65); // step 1 at k=1
    EXPECT_NEAR((double)ons[1].sample, 1.75 * kQuarter, 2.0);
    EXPECT_EQ(ons[1].note, 53);
    EXPECT_EQ(ons[2].note, 65); // k=3
}

TEST_F(SequencerModuleTest, SlideStartsTheNextNoteBeforeEndingTheSlidNote) {
    auto pattern = seq->getPattern();
    pattern.steps[0].slide = true;
    seq->setPattern(pattern);
    run(*seq, true);
    const auto events = render(*seq, kQuarter + 100);

    // Step 0's gate would end at 11025, but a slide holds it until step 1 starts, and the off follows the on.
    std::vector<Event> notes;
    for (const auto& e : events)
        if (e.isOn || e.isOff)
            notes.push_back(e);
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_TRUE(notes[0].isOn);
    EXPECT_TRUE(notes[1].isOn);
    EXPECT_EQ(notes[1].sample, kQuarter);
    EXPECT_TRUE(notes[2].isOff);
    EXPECT_EQ(notes[2].sample, kQuarter);
    EXPECT_EQ(notes[2].note, 53);
}

TEST_F(SequencerModuleTest, WithoutSlideTheOffComesBeforeTheNextOn) {
    run(*seq, true);
    const auto events = render(*seq, kQuarter + 100);
    std::vector<Event> notes;
    for (const auto& e : events)
        if (e.isOn || e.isOff)
            notes.push_back(e);
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_TRUE(notes[1].isOff);
    EXPECT_EQ(notes[1].sample, kQuarter / 2);
}
