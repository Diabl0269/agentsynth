// How the snapshot expands a drum clip's per-hit settings (repeat, flam, nudge, chance, condition), humanise and the
// per-drum length and swing, and how notePlays() resolves chance and condition against a pass counter. The expansion
// is deterministic, so every expectation below is exact.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using synth::MidiNote;
using synth::TimelineDoc;
using synth::TimelineSnapshot;
using synth::TrackKind;

namespace {

constexpr double kStep = 0.25; // a 16-step bar of 4 beats
constexpr int kClosedHat = 42;

struct DrumDoc {
    TimelineDoc doc;
    synth::TrackId track = doc.addTrack(TrackKind::Midi, "Drums");
    synth::ClipId clip = doc.addClip(track, 0.0, 4.0, "Beat"); // one 4/4 bar, 16 steps

    synth::NoteId add(int cell, int pitch, int velocity = 100, double lengthBeats = 0.1) {
        MidiNote note;
        note.startBeat = cell * kStep;
        note.lengthBeats = lengthBeats;
        note.pitch = pitch;
        note.velocity = velocity;
        return doc.addNote(clip, note);
    }

    std::vector<TimelineSnapshot::NoteEvent> events(double beatsPerBar = 4.0) const {
        const auto snapshot = TimelineSnapshot::buildFrom(doc, beatsPerBar);
        EXPECT_TRUE(snapshot->selfCheck());
        return snapshot->notes;
    }
};

std::vector<TimelineSnapshot::NoteEvent> ofPitch(const std::vector<TimelineSnapshot::NoteEvent>& all, int pitch) {
    std::vector<TimelineSnapshot::NoteEvent> out;
    for (const auto& e : all)
        if (e.pitch == pitch)
            out.push_back(e);
    return out;
}

int cellOf(const TimelineSnapshot::NoteEvent& e) { return static_cast<int>(std::lround(e.startBeat / kStep)); }

} // namespace

// The ticket's own check: Closed hat with a length of 12 in a one-bar 16-step clip.
TEST(TimelineSnapshotDrum, ARowLengthOf12RepeatsStepsOneToFourAtThirteenToSixteenAndDropsAStepFourteenNote) {
    DrumDoc d;
    for (int cell = 0; cell < 4; ++cell) // steps 1..4
        d.add(cell, kClosedHat, 101 + cell);
    d.add(13, kClosedHat, 77); // step 14: past the drum's length
    d.add(0, 36);              // a kick on its own, untouched
    ASSERT_TRUE(d.doc.setClipRowLength(d.clip, kClosedHat, 12));

    const auto hats = ofPitch(d.events(), kClosedHat);
    ASSERT_EQ(hats.size(), 8u) << "four stored hits and their four repeats; the step-14 note adds none";
    std::vector<int> cells;
    for (const auto& e : hats)
        cells.push_back(cellOf(e));
    EXPECT_EQ(cells, (std::vector<int>{0, 1, 2, 3, 12, 13, 14, 15}));
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(hats[static_cast<std::size_t>(4 + i)].velocity, 101 + i)
            << "step " << 13 + i << " copies step " << 1 + i;
    EXPECT_EQ(hats[5].velocity, 102) << "step 14 plays the copy of step 2, not the stored note (velocity 77)";
    EXPECT_EQ(ofPitch(d.events(), 36).size(), 1u);
}

TEST(TimelineSnapshotDrum, ARowLengthLongerThanTheClipChangesNothing) {
    DrumDoc d;
    d.add(2, kClosedHat);
    ASSERT_TRUE(d.doc.setClipRowLength(d.clip, kClosedHat, 16));
    EXPECT_EQ(ofPitch(d.events(), kClosedHat).size(), 1u);
}

TEST(TimelineSnapshotDrum, ARowLengthOf4InATwoBarClipRepeatsEveryBeat) {
    DrumDoc d;
    ASSERT_TRUE(d.doc.resizeClip(d.clip, 8.0));
    d.add(0, kClosedHat);
    ASSERT_TRUE(d.doc.setClipRowLength(d.clip, kClosedHat, 4));
    const auto hats = ofPitch(d.events(), kClosedHat);
    ASSERT_EQ(hats.size(), 8u);
    for (std::size_t i = 0; i < hats.size(); ++i)
        EXPECT_DOUBLE_EQ(hats[i].startBeat, static_cast<double>(i));
}

TEST(TimelineSnapshotDrum, ARepeatSpreadsHitsEvenlyOverOneStepAndShortensThem) {
    DrumDoc d;
    const auto id = d.add(4, 38, 100, 1.0);
    ASSERT_TRUE(d.doc.setNoteRepeat(id, 4, false));
    const auto events = ofPitch(d.events(), 38);
    ASSERT_EQ(events.size(), 4u);
    for (int j = 0; j < 4; ++j) {
        EXPECT_DOUBLE_EQ(events[static_cast<std::size_t>(j)].startBeat, 1.0 + j * kStep / 4);
        EXPECT_DOUBLE_EQ(events[static_cast<std::size_t>(j)].endBeat - events[static_cast<std::size_t>(j)].startBeat,
                         kStep / 4);
    }
}

TEST(TimelineSnapshotDrum, AFlamAddsASofterGraceHitJustBeforeTheHit) {
    DrumDoc d;
    const auto id = d.add(4, 38, 100, 1.0);
    ASSERT_TRUE(d.doc.setNoteRepeat(id, 1, true));
    const auto events = ofPitch(d.events(), 38);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_NEAR(events[0].startBeat, 1.0 - 0.15 * kStep, 1e-12);
    EXPECT_EQ(events[0].velocity, 60);
    EXPECT_LE(events[0].endBeat, events[1].startBeat);
    EXPECT_DOUBLE_EQ(events[1].startBeat, 1.0);
    EXPECT_EQ(events[1].velocity, 100);
    EXPECT_EQ(events[0].rollSeed, events[1].rollSeed) << "the grace hit plays or skips with its hit";
}

TEST(TimelineSnapshotDrum, AFlamOnTheFirstStepOfTheClipHasNoRoomAndIsSkipped) {
    DrumDoc d;
    ASSERT_TRUE(d.doc.setNoteRepeat(d.add(0, 38), 1, true));
    EXPECT_EQ(ofPitch(d.events(), 38).size(), 1u);
}

TEST(TimelineSnapshotDrum, ANudgeMovesTheHitByAFractionOfAStepAndAnEarlyOneAtZeroPlaysAtZero) {
    DrumDoc d;
    ASSERT_TRUE(d.doc.setNoteNudge(d.add(4, 38), 0.5));
    ASSERT_TRUE(d.doc.setNoteNudge(d.add(8, 42), -0.5));
    ASSERT_TRUE(d.doc.setNoteNudge(d.add(0, 36), -0.5));
    const auto all = d.events();
    EXPECT_DOUBLE_EQ(ofPitch(all, 38)[0].startBeat, 1.0 + 0.5 * kStep);
    EXPECT_DOUBLE_EQ(ofPitch(all, 42)[0].startBeat, 2.0 - 0.5 * kStep);
    EXPECT_DOUBLE_EQ(ofPitch(all, 36)[0].startBeat, 0.0);
    EXPECT_TRUE(std::is_sorted(all.begin(), all.end(), [](const auto& a, const auto& b) {
        return a.startBeat < b.startBeat;
    })) << "the run stays sorted after a nudge reorders it";
}

TEST(TimelineSnapshotDrum, HumaniseIsDeterministicBoundedAndOffByDefault) {
    DrumDoc d;
    for (int cell = 0; cell < 16; ++cell)
        d.add(cell, 38, 64);
    const auto plain = d.events();
    ASSERT_TRUE(d.doc.setClipHumanise(d.clip, 1.0));
    const auto a = d.events();
    const auto b = d.events();
    ASSERT_EQ(a.size(), 16u);
    bool anyMoved = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_DOUBLE_EQ(a[i].startBeat, b[i].startBeat) << "same input, same result";
        EXPECT_EQ(a[i].velocity, b[i].velocity);
        EXPECT_NEAR(a[i].startBeat, plain[i].startBeat, 0.08 * kStep + 1e-9);
        EXPECT_LE(std::abs(a[i].velocity - 64), 24);
        EXPECT_GE(a[i].velocity, 1);
        anyMoved = anyMoved || a[i].velocity != 64 || a[i].startBeat != plain[i].startBeat;
    }
    EXPECT_TRUE(anyMoved);
    for (const auto& e : plain)
        EXPECT_EQ(e.velocity, 64);
}

TEST(TimelineSnapshotDrum, ARowSwingOverridesTheClipSwingForThatDrumOnly) {
    DrumDoc d;
    ASSERT_TRUE(d.doc.setClipSwing(d.clip, 0.5));
    d.add(1, 42);
    d.add(1, 38);
    ASSERT_TRUE(d.doc.setClipRowSwing(d.clip, 42, 0.75));
    const auto all = d.events();
    EXPECT_DOUBLE_EQ(ofPitch(all, 42)[0].startBeat, kStep + kStep * 0.5)
        << "0.75 swing delays an odd step by half a step";
    EXPECT_DOUBLE_EQ(ofPitch(all, 38)[0].startBeat, kStep) << "the other drum follows the straight clip";
}

TEST(TimelineSnapshotDrum, ARowSwingAtStraightBeatsAClipSwing) {
    DrumDoc d;
    ASSERT_TRUE(d.doc.setClipSwing(d.clip, 0.75));
    d.add(1, 42);
    ASSERT_TRUE(d.doc.setClipRowSwing(d.clip, 42, 0.5));
    EXPECT_DOUBLE_EQ(ofPitch(d.events(), 42)[0].startBeat, kStep);
}

TEST(TimelineSnapshotDrum, LinkedClipsContinueTheConditionCycleOneBarEach) {
    DrumDoc d;
    ASSERT_TRUE(d.doc.setNoteCondition(d.add(0, 38), 2, 2));
    const auto copy = d.doc.duplicateClipLinked(d.clip);
    ASSERT_TRUE(copy.isValid());
    const auto all = d.events();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].cycle, 0);
    EXPECT_EQ(all[1].cycle, 1) << "the second linked copy is the next cycle";
    EXPECT_FALSE(TimelineSnapshot::notePlays(all[0], 0));
    EXPECT_TRUE(TimelineSnapshot::notePlays(all[1], 0));
}

TEST(TimelineSnapshotDrum, ARowRepetitionIsItsOwnCycleSoAHalfTimeConditionAlternatesAcrossRepeats) {
    DrumDoc d;
    ASSERT_TRUE(d.doc.setNoteCondition(d.add(0, kClosedHat), 1, 2));
    ASSERT_TRUE(d.doc.setClipRowLength(d.clip, kClosedHat, 8));
    const auto hats = ofPitch(d.events(), kClosedHat);
    ASSERT_EQ(hats.size(), 2u);
    EXPECT_EQ(hats[0].cyclesPerPass, 2);
    EXPECT_TRUE(TimelineSnapshot::notePlays(hats[0], 0));
    EXPECT_FALSE(TimelineSnapshot::notePlays(hats[1], 0));
}

TEST(TimelineSnapshotDrum, NotePlaysAppliesConditionByPass) {
    TimelineSnapshot::NoteEvent e;
    EXPECT_TRUE(TimelineSnapshot::notePlays(e, 0)) << "no settings: always";
    e.condHit = 2;
    e.condOf = 2;
    EXPECT_FALSE(TimelineSnapshot::notePlays(e, 0));
    EXPECT_TRUE(TimelineSnapshot::notePlays(e, 1));
    EXPECT_FALSE(TimelineSnapshot::notePlays(e, 2));
    EXPECT_TRUE(TimelineSnapshot::notePlays(e, 3));
    e.condHit = 1;
    e.condOf = 3;
    e.cyclesPerPass = 2; // two cycles per pass: pass 1 starts at cycle 2
    EXPECT_TRUE(TimelineSnapshot::notePlays(e, 0));
    EXPECT_FALSE(TimelineSnapshot::notePlays(e, 1));
    EXPECT_FALSE(TimelineSnapshot::notePlays(e, 2));
    EXPECT_TRUE(TimelineSnapshot::notePlays(e, 3));
}

TEST(TimelineSnapshotDrum, NotePlaysChanceIsDeterministicAndRoughlyThePercentage) {
    TimelineSnapshot::NoteEvent e;
    e.rollSeed = 12345;
    e.chance = 0;
    for (int pass = 0; pass < 200; ++pass)
        EXPECT_FALSE(TimelineSnapshot::notePlays(e, pass));
    e.chance = 100;
    for (int pass = 0; pass < 200; ++pass)
        EXPECT_TRUE(TimelineSnapshot::notePlays(e, pass));
    e.chance = 30;
    int plays = 0;
    for (int pass = 0; pass < 10000; ++pass) {
        const bool first = TimelineSnapshot::notePlays(e, pass);
        EXPECT_EQ(first, TimelineSnapshot::notePlays(e, pass)) << "the same pass always rolls the same";
        plays += first ? 1 : 0;
    }
    EXPECT_NEAR(plays, 3000, 300);
}

TEST(TimelineSnapshotDrum, DifferentNotesRollIndependently) {
    DrumDoc d;
    for (int cell = 0; cell < 16; ++cell)
        ASSERT_TRUE(d.doc.setNoteChance(d.add(cell, 42), 50));
    const auto events = d.events();
    std::vector<std::uint32_t> seeds;
    for (const auto& e : events)
        seeds.push_back(e.rollSeed);
    std::sort(seeds.begin(), seeds.end());
    EXPECT_EQ(std::adjacent_find(seeds.begin(), seeds.end()), seeds.end()) << "every hit has its own seed";
}
