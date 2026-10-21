// The drum machine's ideas as pure functions: a starter per style, variations that keep the groove, and a randomized
// pattern that can keep the kick and snare. No component, no document.

#include "UI/DrumMachine/DrumIdeas.h"
#include <gtest/gtest.h>
#include <set>

namespace {

using namespace synth::ui::drumideas;

Grid fourBars() { return {16.0, 4.0, 0.25}; }

bool hasNote(const std::vector<synth::MidiNote>& notes, double start, int pitch) {
    return std::any_of(notes.begin(), notes.end(),
                       [&](const auto& n) { return n.pitch == pitch && std::abs(n.startBeat - start) < 1.0e-6; });
}

double meanChanges(const std::vector<synth::MidiNote>& base, double amount) {
    double total = 0.0;
    for (int seed = 1; seed <= 40; ++seed)
        total += static_cast<double>(vary(base, fourBars(), amount, seed).changed.size());
    return total / 40.0;
}

} // namespace

TEST(DrumIdeasStarterTest, EveryStyleFillsTheClipOnTheDefaultRowsAndRepeatsPerBar) {
    const std::set<int> rows{36, 37, 38, 39, 42, 45, 46, 49, 56};
    for (int i = 0; i < kStyleCount; ++i) {
        const auto style = styleAt(i);
        const auto oneBar = starter(style, {4.0, 4.0, 0.25});
        const auto fourBarsOfIt = starter(style, fourBars());
        EXPECT_FALSE(oneBar.empty()) << styleName(style).toStdString();
        EXPECT_EQ(fourBarsOfIt.size(), oneBar.size() * 4) << styleName(style).toStdString() << " repeats once per bar";
        for (const auto& note : fourBarsOfIt) {
            EXPECT_TRUE(rows.count(note.pitch) > 0) << styleName(style).toStdString() << " pitch " << note.pitch;
            EXPECT_GE(note.velocity, 1);
            EXPECT_LE(note.velocity, 127);
            EXPECT_GE(note.startBeat, 0.0);
            EXPECT_LT(note.startBeat, 16.0);
        }
        EXPECT_TRUE(hasNote(oneBar, 0.0, 36)) << styleName(style).toStdString() << " starts on a kick";
        EXPECT_TRUE(std::is_sorted(oneBar.begin(), oneBar.end(),
                                   [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; }));
    }
}

TEST(DrumIdeasStarterTest, IsDeterministicAndACutOffPartialBarKeepsNothingPastTheEnd) {
    EXPECT_EQ(starter(Style::House, fourBars()).size(), starter(Style::House, fourBars()).size());
    const auto cut = starter(Style::Rock, {6.0, 4.0, 0.25});
    for (const auto& note : cut)
        EXPECT_LT(note.startBeat, 6.0);
    EXPECT_GT(cut.size(), starter(Style::Rock, {4.0, 4.0, 0.25}).size());
}

TEST(DrumIdeasStarterTest, TrapNeedsThirtyTwoStepsAndRollsItsHats) {
    EXPECT_EQ(requiredStepsPerBar(Style::Trap), 32);
    EXPECT_EQ(requiredStepsPerBar(Style::House), 16);
    const auto trap = starter(Style::Trap, {4.0, 4.0, 0.125});
    EXPECT_TRUE(hasNote(trap, 14.5 * 0.25, 42)) << "a hat roll lands on a thirty-second";
}

TEST(DrumIdeasVaryTest, SubtleAndMediumVariationsKeepTheFirstBeatKickAndTheBackbeats) {
    const auto base = starter(Style::Rock, fourBars());
    for (const double amount : {0.0, 0.2, 0.5, 0.9})
        for (int seed = 1; seed <= 25; ++seed) {
            const auto out = vary(base, fourBars(), amount, seed).notes;
            for (int bar = 0; bar < 4; ++bar) {
                EXPECT_TRUE(hasNote(out, bar * 4.0, 36)) << "amount " << amount << " seed " << seed;
                EXPECT_TRUE(hasNote(out, bar * 4.0 + 1.0, 38)) << "amount " << amount << " seed " << seed;
                EXPECT_TRUE(hasNote(out, bar * 4.0 + 3.0, 38)) << "amount " << amount << " seed " << seed;
            }
        }
}

TEST(DrumIdeasVaryTest, WildMayBreakTheGrooveToo) {
    const auto base = starter(Style::Rock, fourBars());
    bool broke = false;
    for (int seed = 1; seed <= 60 && !broke; ++seed) {
        const auto out = vary(base, fourBars(), 1.0, seed).notes;
        for (int bar = 0; bar < 4; ++bar)
            broke = broke || !hasNote(out, bar * 4.0, 36) || !hasNote(out, bar * 4.0 + 1.0, 38) ||
                    !hasNote(out, bar * 4.0 + 3.0, 38);
    }
    EXPECT_TRUE(broke);
}

TEST(DrumIdeasVaryTest, TheNumberOfChangesGrowsWithTheAmount) {
    const auto base = starter(Style::Funk, fourBars());
    const double subtle = meanChanges(base, 0.1);
    const double medium = meanChanges(base, 0.5);
    const double wild = meanChanges(base, 1.0);
    EXPECT_GT(subtle, 0.0);
    EXPECT_LT(subtle, medium);
    EXPECT_LT(medium, wild);
}

TEST(DrumIdeasVaryTest, TheSameSeedGivesTheSameTakeAndAnotherSeedAnotherOne) {
    const auto base = starter(Style::HipHop, fourBars());
    const auto a = vary(base, fourBars(), 0.5, 7);
    const auto b = vary(base, fourBars(), 0.5, 7);
    ASSERT_EQ(a.notes.size(), b.notes.size());
    for (size_t i = 0; i < a.notes.size(); ++i) {
        EXPECT_EQ(a.notes[i].startBeat, b.notes[i].startBeat);
        EXPECT_EQ(a.notes[i].pitch, b.notes[i].pitch);
        EXPECT_EQ(a.notes[i].velocity, b.notes[i].velocity);
    }
    EXPECT_EQ(a.changed, b.changed);
    EXPECT_NE(a.changed, vary(base, fourBars(), 0.5, 8).changed);
}

TEST(DrumIdeasVaryTest, TheChangedCellsAreExactlyTheCellsThatDiffer) {
    const auto base = starter(Style::House, fourBars());
    const auto result = vary(base, fourBars(), 0.4, 3);
    EXPECT_EQ(result.changed, changedCells(base, result.notes, 0.25));
    EXPECT_TRUE(changedCells(base, base, 0.25).empty());
    auto moved = base;
    moved[0].startBeat += 0.25;
    EXPECT_EQ(changedCells(base, moved, 0.25).size(), 2u) << "the cell it left and the cell it entered";
}

TEST(DrumIdeasRandomizeTest, KeepKickAndSnareKeepsThemExactlyAndRandomizesTheRest) {
    const auto base = starter(Style::Rock, fourBars());
    std::vector<synth::MidiNote> expected;
    for (const auto& n : base)
        if (isKick(n.pitch) || isSnare(n.pitch))
            expected.push_back(n);
    for (int seed = 1; seed <= 20; ++seed) {
        const auto out = randomize(base, fourBars(), 0.6, true, seed).notes;
        std::vector<synth::MidiNote> keptOnes;
        for (const auto& n : out)
            if (isKick(n.pitch) || isSnare(n.pitch))
                keptOnes.push_back(n);
        ASSERT_EQ(keptOnes.size(), expected.size()) << "seed " << seed << " adds no kick or snare";
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(keptOnes[i].startBeat, expected[i].startBeat);
            EXPECT_EQ(keptOnes[i].pitch, expected[i].pitch);
            EXPECT_EQ(keptOnes[i].velocity, expected[i].velocity);
        }
    }
    const auto a = randomize(base, fourBars(), 0.6, true, 1).notes;
    const auto b = randomize(base, fourBars(), 0.6, true, 2).notes;
    EXPECT_NE(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                                 [](const auto& x, const auto& y) {
                                                     return x.startBeat == y.startBeat && x.pitch == y.pitch;
                                                 }),
              true)
        << "the other drums change with the seed";
}

TEST(DrumIdeasRandomizeTest, WithoutKeepItBuildsAKickAndSnareToo) {
    const auto out = randomize({}, fourBars(), 0.5, false, 5).notes;
    EXPECT_TRUE(hasNote(out, 0.0, 36));
    EXPECT_TRUE(std::any_of(out.begin(), out.end(), [](const auto& n) { return isSnare(n.pitch); }));
}

TEST(DrumIdeasRandomizeTest, DensityScalesHowManyHits) {
    size_t sparse = 0, busy = 0;
    for (int seed = 1; seed <= 20; ++seed) {
        sparse += randomize({}, fourBars(), 0.05, false, seed).notes.size();
        busy += randomize({}, fourBars(), 0.95, false, seed).notes.size();
        EXPECT_LT(randomize({}, fourBars(), 0.05, false, seed).notes.size(),
                  randomize({}, fourBars(), 0.95, false, seed).notes.size())
            << "seed " << seed;
    }
    EXPECT_GT(busy, sparse * 2);
    EXPECT_GT(sparse, 0u);
}

TEST(DrumIdeasRandomizeTest, IsDeterministicForASeed) {
    const auto a = randomize({}, fourBars(), 0.5, false, 11);
    const auto b = randomize({}, fourBars(), 0.5, false, 11);
    EXPECT_EQ(a.notes.size(), b.notes.size());
    EXPECT_EQ(a.changed, b.changed);
}
