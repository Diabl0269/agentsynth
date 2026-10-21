// SequencerPreviewTests.cpp -- the audition path: a chord the UI queues is played into the module's MIDI output at
// the start of the next block and ended about 300 ms later, whether Run is on or off, without cutting or leaking
// notes the pattern plays.
#include "Modules/PolySequencerModule.h"
#include "Modules/SequencerModule.h"
#include "SequencerTestSupport.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <thread>

using namespace seqtest;

namespace {

constexpr int kRate = 44100;
constexpr int kPreviewSamples = 13230; // 300 ms

class SequencerPreviewTest : public ::testing::Test {
protected:
    void SetUp() override {
        seq = std::make_unique<PolySequencerModule>();
        seq->prepareToPlay(kRate, 512);
    }

    static std::vector<int> pitches(const std::vector<Event>& events) {
        std::vector<int> out;
        for (const auto& e : events)
            out.push_back(e.note);
        std::sort(out.begin(), out.end());
        return out;
    }

    /** The last event of every pitch is a note-off: a synth that treats a repeated note-on as a retrigger has nothing
     *  left sounding. */
    static void expectBalanced(const std::vector<Event>& events) {
        std::map<int, bool> sounding;
        for (const auto& e : events)
            if (e.isOn || e.isOff)
                sounding[e.note] = e.isOn;
        for (const auto& [note, on] : sounding)
            EXPECT_FALSE(on) << "note " << note << " left sounding";
    }

    std::unique_ptr<PolySequencerModule> seq;
};

} // namespace

TEST_F(SequencerPreviewTest, AQueuedPreviewIsTheChordsNoteOnsInTheNextBlockAndTheOffsThreeHundredMsLater) {
    const int chord[] = {60, 64, 67};
    seq->previewNotes(chord, 3, 0.8f);
    const auto events = render(*seq, 20000);

    const auto ons = onsOnly(events);
    ASSERT_EQ(ons.size(), 3u);
    for (const auto& on : ons)
        EXPECT_EQ(on.sample, 0);
    EXPECT_EQ(pitches(ons), (std::vector<int>{60, 64, 67}));

    const auto offs = offsOnly(events);
    ASSERT_EQ(offs.size(), 3u);
    for (const auto& off : offs)
        EXPECT_EQ(off.sample, kPreviewSamples);
    EXPECT_EQ(pitches(offs), (std::vector<int>{60, 64, 67}));
}

TEST_F(SequencerPreviewTest, TheVelocityIsTheRequestedOne) {
    const int chord[] = {60};
    seq->previewNotes(chord, 1, 0.5f);
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    seq->processBlock(buffer, midi);
    int velocity = -1;
    for (const auto meta : midi)
        if (meta.getMessage().isNoteOn())
            velocity = meta.getMessage().getVelocity();
    EXPECT_EQ(velocity, 64);
}

TEST_F(SequencerPreviewTest, ASecondPreviewReplacesTheFirstWithOffsForTheFirst) {
    const int first[] = {60, 64, 67};
    const int second[] = {62, 65};
    seq->previewNotes(first, 3, 0.8f);
    auto events = render(*seq, 512);
    seq->previewNotes(second, 2, 0.8f);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    seq->processBlock(buffer, midi);
    std::vector<Event> block;
    collect(midi, 0, block);

    // The first chord's offs come before the second chord's ons, all at the block start.
    ASSERT_EQ(block.size(), 5u);
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(block[(size_t)i].isOff) << i;
    EXPECT_EQ(pitches(offsOnly(block)), (std::vector<int>{60, 64, 67}));
    EXPECT_EQ(pitches(onsOnly(block)), (std::vector<int>{62, 65}));

    // Only the second chord ends later, 300 ms after it started.
    const auto later = render(*seq, 20000);
    EXPECT_TRUE(onsOnly(later).empty());
    EXPECT_EQ(pitches(offsOnly(later)), (std::vector<int>{62, 65}));
    events.insert(events.end(), block.begin(), block.end());
    events.insert(events.end(), later.begin(), later.end());
    expectBalanced(events);
}

TEST_F(SequencerPreviewTest, StopPreviewEndsTheChordInTheNextBlock) {
    const int chord[] = {60, 64};
    seq->previewNotes(chord, 2, 0.8f);
    auto events = render(*seq, 512);
    seq->stopPreview();
    const auto after = render(*seq, 512);
    EXPECT_EQ(pitches(offsOnly(after)), (std::vector<int>{60, 64}));
    EXPECT_TRUE(onsOnly(after).empty());
    events.insert(events.end(), after.begin(), after.end());
    const auto rest = render(*seq, 20000);
    EXPECT_TRUE(rest.empty()) << "no second off at the old end time";
    expectBalanced(events);
}

TEST_F(SequencerPreviewTest, ANoteTheRequestRepeatsOrTheSetOverflowsIsClamped) {
    const int many[] = {60, 61, 62, 63, 64, 65, 66, 67, 68, 69};
    seq->previewNotes(many, 10, 0.8f);
    const auto events = render(*seq, 512);
    EXPECT_EQ(onsOnly(events).size(), 8u);
}

TEST_F(SequencerPreviewTest, WhileThePatternPlaysTheSamePitchThePreviewNeverCutsItAndNothingHangs) {
    // One step on C3 (60) at 1/4 and 120 bpm: the pattern holds 60 for each whole 22050-sample step.
    setNumber(*seq, "steps", 1);
    setNumber(*seq, "bpm", 120.0f);
    setNumber(*seq, "gate", 2.0f); // the default half-step gate doubled: each note rings the whole step
    seq->setPattern(patternOf(*seq, {60}));
    run(*seq, true);

    const int chord[] = {60, 64};
    seq->previewNotes(chord, 2, 0.8f);
    auto events = render(*seq, 30000);

    for (const auto& off : offsOnly(events))
        if (off.note == 60)
            EXPECT_GE(off.sample, 22050) << "the preview's end must not cut the pattern's note";
    int previewOffs64 = 0;
    for (const auto& off : offsOnly(events))
        if (off.note == 64) {
            ++previewOffs64;
            EXPECT_EQ(off.sample, kPreviewSamples);
        }
    EXPECT_EQ(previewOffs64, 1);

    run(*seq, false);
    const auto tail = render(*seq, 2048);
    events.insert(events.end(), tail.begin(), tail.end());
    expectBalanced(events);
}

TEST_F(SequencerPreviewTest, ThePreviewWorksWithRunOffAndOnTheMonoSequencerToo) {
    SequencerModule mono;
    mono.prepareToPlay(kRate, 512);
    const int chord[] = {55};
    mono.previewNotes(chord, 1, 0.8f);
    const auto events = render(mono, 20000);
    ASSERT_EQ(onsOnly(events).size(), 1u);
    EXPECT_EQ(onsOnly(events)[0].note, 55);
    ASSERT_EQ(offsOnly(events).size(), 1u);
    EXPECT_EQ(offsOnly(events)[0].sample, kPreviewSamples);
}

TEST_F(SequencerPreviewTest, BypassEmitsNothingForANewPreviewAndEndsOneThatIsSounding) {
    seq->setBypassed(true);
    render(*seq, 4 * 512); // let the click-free transition land
    const int chord[] = {60, 64};
    seq->previewNotes(chord, 2, 0.8f);
    EXPECT_TRUE(render(*seq, 20000).empty()) << "bypassed: nothing is played";

    seq->setBypassed(false);
    render(*seq, 4 * 512);
    seq->previewNotes(chord, 2, 0.8f);
    auto events = render(*seq, 512);
    EXPECT_EQ(onsOnly(events).size(), 2u);
    seq->setBypassed(true);
    const auto rest = render(*seq, 20000);
    events.insert(events.end(), rest.begin(), rest.end());
    expectBalanced(events);
}

// Run under -fsanitize=thread: the message thread keeps replacing the request while the audio thread renders, and
// every chord the audio thread starts is a whole request, never a mix of two.
TEST_F(SequencerPreviewTest, RequestsFromAnotherThreadAreNeverReadHalfWritten) {
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; i < 20000; ++i) {
            const int root = 40 + (i % 24);
            const int chord[] = {root, root + 4, root + 7};
            seq->previewNotes(chord, 3, 0.8f);
        }
        done.store(true);
    });
    int chordsStarted = 0;
    while (!done.load() || chordsStarted == 0) {
        const auto ons = onsOnly(render(*seq, 512));
        if (ons.empty())
            continue;
        ++chordsStarted;
        const auto notes = pitches(ons);
        ASSERT_EQ(notes.size(), 3u);
        EXPECT_EQ(notes[1] - notes[0], 4);
        EXPECT_EQ(notes[2] - notes[0], 7);
    }
    writer.join();
    EXPECT_GT(chordsStarted, 0);
}
