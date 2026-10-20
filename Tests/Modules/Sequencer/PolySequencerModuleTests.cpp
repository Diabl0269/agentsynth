#include "Modules/PolySequencerModule.h"
#include "SequencerTestSupport.h"
#include <algorithm>

using namespace seqtest;
using synth::SequencerChord;

namespace {

constexpr int kRate = 44100;
constexpr int kQuarter = 22050;

class PolySequencerModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        seq = std::make_unique<PolySequencerModule>();
        seq->prepareToPlay(kRate, 512);
    }

    /** Sets step 0 to `chord` on `root` and returns the pitches of the first step's note-ons, ascending. */
    std::vector<int> chordNotes(SequencerChord chord, int root) {
        auto pattern = seq->getPattern();
        pattern.steps[0].chord = chord;
        pattern.steps[0].pitch = (std::uint8_t)root;
        seq->setPattern(pattern);
        seq->prepareToPlay(kRate, 512);
        run(*seq, true);
        std::vector<int> notes;
        for (const auto& on : onsOnly(render(*seq, 512)))
            notes.push_back(on.note);
        std::sort(notes.begin(), notes.end());
        run(*seq, false);
        render(*seq, 512);
        return notes;
    }

    std::unique_ptr<PolySequencerModule> seq;
};

} // namespace

TEST_F(PolySequencerModuleTest, ProducesMidiAndHasNoPerStepParameters) {
    EXPECT_TRUE(seq->producesMidi());
    for (auto* p : seq->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p)) {
            EXPECT_FALSE(withId->paramID.startsWith("Step ")) << withId->paramID;
            EXPECT_FALSE(withId->paramID.startsWith("Gate ")) << withId->paramID;
        }
    EXPECT_NE(findParameterByID(seq.get(), "scale"), nullptr);
}

TEST_F(PolySequencerModuleTest, DefaultPatternPlaysTheOldRootsAtQuarterNotes) {
    run(*seq, true);
    const auto ons = onsOnly(render(*seq, 8 * kQuarter));
    const int roots[8] = {48, 52, 55, 60, 48, 55, 52, 60};
    ASSERT_EQ(ons.size(), 8u);
    for (int k = 0; k < 8; ++k) {
        EXPECT_EQ(ons[(size_t)k].sample, (juce::int64)k * kQuarter);
        EXPECT_EQ(ons[(size_t)k].note, roots[k]);
    }
    EXPECT_TRUE(ccsOnly(render(*seq, 100)).empty()); // no filter CC from the chord sequencer
}

TEST_F(PolySequencerModuleTest, LegacyChordsKeepTheirIntervals) {
    EXPECT_EQ(chordNotes(SequencerChord::Unison, 60), (std::vector<int>{60}));
    EXPECT_EQ(chordNotes(SequencerChord::Major, 60), (std::vector<int>{60, 64, 67}));
    EXPECT_EQ(chordNotes(SequencerChord::Minor, 60), (std::vector<int>{60, 63, 67}));
    EXPECT_EQ(chordNotes(SequencerChord::Maj7, 60), (std::vector<int>{60, 64, 67, 71}));
    EXPECT_EQ(chordNotes(SequencerChord::Min7, 60), (std::vector<int>{60, 63, 67, 70}));
    EXPECT_EQ(chordNotes(SequencerChord::Fifths, 60), (std::vector<int>{60, 67}));
    EXPECT_EQ(chordNotes(SequencerChord::Octaves, 60), (std::vector<int>{60, 72}));
}

TEST_F(PolySequencerModuleTest, FixedQualitiesStackOnTheRoot) {
    EXPECT_EQ(chordNotes(SequencerChord::Dom7, 48), (std::vector<int>{48, 52, 55, 58}));
    EXPECT_EQ(chordNotes(SequencerChord::Dim, 48), (std::vector<int>{48, 51, 54}));
    EXPECT_EQ(chordNotes(SequencerChord::Aug, 48), (std::vector<int>{48, 52, 56}));
    EXPECT_EQ(chordNotes(SequencerChord::Sus2, 48), (std::vector<int>{48, 50, 55}));
    EXPECT_EQ(chordNotes(SequencerChord::Sus4, 48), (std::vector<int>{48, 53, 55}));
    EXPECT_EQ(chordNotes(SequencerChord::Sixth, 48), (std::vector<int>{48, 52, 55, 57}));
    EXPECT_EQ(chordNotes(SequencerChord::Min6, 48), (std::vector<int>{48, 51, 55, 57}));
    EXPECT_EQ(chordNotes(SequencerChord::Add9, 48), (std::vector<int>{48, 52, 55, 62}));
    EXPECT_EQ(chordNotes(SequencerChord::Dim7, 48), (std::vector<int>{48, 51, 54, 57}));
    EXPECT_EQ(chordNotes(SequencerChord::HalfDim7, 48), (std::vector<int>{48, 51, 54, 58}));
    EXPECT_EQ(chordNotes(SequencerChord::MinMaj7, 48), (std::vector<int>{48, 51, 55, 59}));
}

TEST_F(PolySequencerModuleTest, ScaleChordsStackScaleNotes) {
    setChoice(*seq, "scale", "Major");
    EXPECT_EQ(chordNotes(SequencerChord::ScaleSeventh, 60), (std::vector<int>{60, 64, 67, 71}));
    EXPECT_EQ(chordNotes(SequencerChord::ScaleTriad, 62), (std::vector<int>{62, 65, 69})); // D minor in C major
    EXPECT_EQ(chordNotes(SequencerChord::ScaleSus4, 60), (std::vector<int>{60, 65, 67}));
    // C# is not in C major: the root snaps down to C before the chord is built.
    EXPECT_EQ(chordNotes(SequencerChord::ScaleTriad, 61), (std::vector<int>{60, 64, 67}));
}

TEST_F(PolySequencerModuleTest, ScaleChordsWithoutAScaleFallBackToMajor) {
    EXPECT_EQ(chordNotes(SequencerChord::ScaleTriad, 62), (std::vector<int>{62, 66, 69}));
}

TEST_F(PolySequencerModuleTest, FixedChordsAreBuiltOnTheSnappedRootButNotSnapped) {
    setChoice(*seq, "scale", "Major");
    // Root C# snaps to C; the Dim intervals (0, 3, 6) are not pulled into the key.
    EXPECT_EQ(chordNotes(SequencerChord::Dim, 61), (std::vector<int>{60, 63, 66}));
}

TEST_F(PolySequencerModuleTest, NotesOutsideTheMidiRangeAreDropped) {
    EXPECT_EQ(chordNotes(SequencerChord::Octaves, 120), (std::vector<int>{120}));
    EXPECT_EQ(chordNotes(SequencerChord::Major, 127), (std::vector<int>{127}));
}

TEST_F(PolySequencerModuleTest, RandomChordIsDeterministicForTheSeed) {
    auto first = chordNotes(SequencerChord::Random, 60);
    auto second = chordNotes(SequencerChord::Random, 60);
    EXPECT_EQ(first, second);
    ASSERT_FALSE(first.empty());
    EXPECT_NE(std::find(first.begin(), first.end(), 60), first.end());
    for (int note : first) {
        EXPECT_GE(note, 49);
        EXPECT_LE(note, 71);
    }
}

TEST_F(PolySequencerModuleTest, EveryChordKindPlaysAtLeastOneNote) {
    for (int c = 0; c < synth::kSequencerChordCount; ++c)
        EXPECT_GE(chordNotes((SequencerChord)c, 60).size(), 1u) << synth::chordId((SequencerChord)c);
}

TEST_F(PolySequencerModuleTest, StopSendsEveryHeldNoteOffExactlyOnce) {
    auto pattern = seq->getPattern();
    pattern.steps[0].chord = SequencerChord::Min7;
    seq->setPattern(pattern);
    run(*seq, true);
    ASSERT_EQ(onsOnly(render(*seq, 512)).size(), 4u);

    run(*seq, false);
    const auto stopped = render(*seq, 512);
    EXPECT_EQ(offsOnly(stopped).size(), 4u);
    for (const auto& off : offsOnly(stopped))
        EXPECT_EQ(off.sample, 0);
    EXPECT_TRUE(render(*seq, 1024).empty());
}

TEST_F(PolySequencerModuleTest, ChordEndsTogetherAfterTheGate) {
    auto pattern = seq->getPattern();
    pattern.steps[0].chord = SequencerChord::Major;
    seq->setPattern(pattern);
    run(*seq, true);
    const auto offs = offsOnly(render(*seq, kQuarter - 100));
    ASSERT_EQ(offs.size(), 3u);
    for (const auto& off : offs)
        EXPECT_EQ(off.sample, kQuarter / 2);
}

TEST_F(PolySequencerModuleTest, SlideIsIgnored) {
    auto pattern = seq->getPattern();
    pattern.steps[0].slide = true;
    seq->setPattern(pattern);
    run(*seq, true);
    const auto offs = offsOnly(render(*seq, kQuarter - 100));
    ASSERT_EQ(offs.size(), 1u);
    EXPECT_EQ(offs[0].sample, kQuarter / 2);
}
