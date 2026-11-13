// A drum clip's swing: the setting (range, round trip, the shared and copied fields) and how
// TimelineSnapshot::buildFrom applies it (every odd grid step later, off-grid notes untouched, never past the clip end,
// the run kept sorted).

#include "Timeline/TimelineSnapshot.h"
#include "TimelineDocTestHelpers.h"

namespace {

synth::MidiNote hit(double beat, int pitch, double length = 0.25) {
    synth::MidiNote n;
    n.startBeat = beat;
    n.pitch = pitch;
    n.lengthBeats = length;
    return n;
}

struct SwingFixture {
    TimelineDoc doc;
    synth::TrackId track = doc.addTrack(TrackKind::Midi, "Drums");
    ClipId clip = doc.addClip(track, 8.0, 4.0, "Beat");

    std::vector<synth::TimelineSnapshot::NoteEvent> events(double beatsPerBar = 4.0) {
        const auto snapshot = synth::TimelineSnapshot::buildFrom(doc, beatsPerBar);
        EXPECT_TRUE(snapshot->selfCheck());
        return {snapshot->notes.begin(), snapshot->notes.end()};
    }
};

} // namespace

TEST(ClipSwingTest, DefaultIsStraightAndOmittedFromTheFile) {
    SwingFixture f;
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.clip)->swing, 0.5);
    EXPECT_FALSE(juce::JSON::toString(f.doc.toVar()).contains("swing"));
}

TEST(ClipSwingTest, RoundTripsThroughToVarAndFromVar) {
    SwingFixture f;
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.62));
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(f.doc.toVar()));
    EXPECT_DOUBLE_EQ(loaded.getClip(f.clip)->swing, 0.62);
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.5));
    EXPECT_FALSE(juce::JSON::toString(f.doc.toVar()).contains("swing")) << "back to straight omits it again";
}

TEST(ClipSwingTest, OutOfRangeValuesAreRejectedByTheSetterAndTheLoader) {
    SwingFixture f;
    EXPECT_FALSE(f.doc.setClipSwing(f.clip, 0.49));
    EXPECT_FALSE(f.doc.setClipSwing(f.clip, 0.76));
    EXPECT_FALSE(f.doc.setClipSwing(f.clip, std::nan("")));
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.clip)->swing, 0.5);

    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.7));
    auto state = f.doc.toVar();
    auto text = juce::JSON::toString(state).replace("0.7", "0.9");
    juce::var bad;
    ASSERT_TRUE(juce::JSON::parse(text, bad).wasOk());
    TimelineDoc loaded;
    EXPECT_FALSE(loaded.fromVar(bad));
}

TEST(ClipSwingTest, LinkedClipsShareItAndDuplicateAndSplitCarryIt) {
    SwingFixture f;
    f.doc.addNote(f.clip, hit(0.0, 36));
    const auto twin = f.doc.duplicateClipLinked(f.clip);
    ASSERT_TRUE(twin.isValid());
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.6));
    EXPECT_DOUBLE_EQ(f.doc.getClip(twin)->swing, 0.6) << "a linked clip's group shares swing";
    ASSERT_TRUE(f.doc.setClipSwing(twin, 0.55));
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.clip)->swing, 0.55);

    const auto copy = f.doc.duplicateClip(f.clip);
    ASSERT_TRUE(copy.isValid());
    EXPECT_DOUBLE_EQ(f.doc.getClip(copy)->swing, 0.55);
    const auto halves = f.doc.splitClip(f.clip, 2.0);
    ASSERT_TRUE(halves.second.isValid());
    EXPECT_DOUBLE_EQ(f.doc.getClip(halves.second)->swing, 0.55);
}

TEST(ClipSwingTest, SixteenStepsPushEveryOddStepLaterAndLeaveTheOthers) {
    SwingFixture f;
    for (int step = 0; step < 4; ++step)
        f.doc.addNote(f.clip, hit(0.25 * step, 42));
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.6));
    auto e = f.events();
    ASSERT_EQ(e.size(), 4u);
    EXPECT_NEAR(e[0].startBeat, 8.0, 1e-9);
    EXPECT_NEAR(e[1].startBeat, 8.25 + 0.05, 1e-9) << "(0.6 - 0.5) * 2 steps of 0.25 beat";
    EXPECT_NEAR(e[2].startBeat, 8.5, 1e-9);
    EXPECT_NEAR(e[3].startBeat, 8.75 + 0.05, 1e-9);
    EXPECT_NEAR(e[1].endBeat - e[1].startBeat, 0.25, 1e-9) << "the length is kept";

    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    e = f.events();
    EXPECT_NEAR(e[1].startBeat, 8.0 + 0.375, 1e-9) << "a triplet feel: the off-beat sits at 75% of its pair";
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.5));
    EXPECT_NEAR(f.events()[1].startBeat, 8.25, 1e-9) << "straight leaves it where it is";
}

TEST(ClipSwingTest, EightStepsPerBarUseHalfBeatSteps) {
    SwingFixture f;
    ASSERT_TRUE(f.doc.setClipStepsPerBar(f.clip, 8));
    f.doc.addNote(f.clip, hit(0.0, 42, 0.5));
    f.doc.addNote(f.clip, hit(0.5, 42, 0.5));
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    const auto e = f.events();
    ASSERT_EQ(e.size(), 2u);
    EXPECT_NEAR(e[0].startBeat, 8.0, 1e-9);
    EXPECT_NEAR(e[1].startBeat, 8.75, 1e-9);
}

TEST(ClipSwingTest, TheTimeSignatureSizesTheSteps) {
    SwingFixture f;
    f.doc.addNote(f.clip, hit(0.1875, 42, 0.1)); // step 1 of 16 in a 3/4 bar (0.1875 beat steps)
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    EXPECT_NEAR(f.events(3.0)[0].startBeat, 8.0 + 0.1875 + 0.1875 * 0.5, 1e-9);
    EXPECT_NEAR(f.events(4.0)[0].startBeat, 8.1875, 1e-9) << "off the 4/4 grid: untouched";
}

TEST(ClipSwingTest, NotesOffTheGridAreUntouched) {
    SwingFixture f;
    f.doc.addNote(f.clip, hit(0.3, 42));
    f.doc.addNote(f.clip, hit(0.26, 46));
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    const auto e = f.events();
    ASSERT_EQ(e.size(), 2u);
    EXPECT_NEAR(e[0].startBeat, 8.26, 1e-9);
    EXPECT_NEAR(e[1].startBeat, 8.3, 1e-9);
}

TEST(ClipSwingTest, ASwungNoteNeverStartsPastTheClipEnd) {
    TimelineDoc doc;
    const auto clip = doc.addClip(doc.addTrack(TrackKind::Midi, "Drums"), 0.0, 0.3, "Short");
    doc.addNote(clip, hit(0.25, 42));
    ASSERT_TRUE(doc.setClipSwing(clip, 0.75)); // 0.25 + 0.125 would be 0.375, past the end at 0.3
    const auto snapshot = synth::TimelineSnapshot::buildFrom(doc);
    ASSERT_EQ(snapshot->notes.size(), 1u);
    EXPECT_NEAR(snapshot->notes[0].startBeat, 0.25, 1e-9);
    EXPECT_LE(snapshot->notes[0].endBeat, 0.3 + 1e-9);
}

TEST(ClipSwingTest, TheRunStaysSortedWhenASwungNoteOvertakesAnOffGridOne) {
    SwingFixture f;
    f.doc.addNote(f.clip, hit(0.25, 36));
    f.doc.addNote(f.clip, hit(0.3, 38));
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    const auto e = f.events();
    ASSERT_EQ(e.size(), 2u);
    EXPECT_EQ(e[0].pitch, 38) << "0.3 now plays before the swung 0.375";
    EXPECT_EQ(e[1].pitch, 36);
}

TEST(ClipSwingTest, SwingIsNeverWrittenIntoTheNotes) {
    SwingFixture f;
    f.doc.addNote(f.clip, hit(0.25, 42));
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.clip)->notes[0].startBeat, 0.25);
}
