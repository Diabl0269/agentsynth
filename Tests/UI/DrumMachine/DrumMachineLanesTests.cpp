// The lanes under the drum machine's grid: the five chips, lanes stacking in chip order, each lane's drag and keys as
// one undo step with the committed values equal to the previewed ones, the screen-reader text, and the marks a hit
// carries in its cell.

#include "../Accessibility/AccessibilityAudit.h"
#include "DrumMachineGrooveFixture.h"
#include "UI/DrumMachine/DrumCellMarks.h"

using drumtest::AnimationModeGuard;
using drumtest::GrooveFixture;
using drumtest::inLane;
using synth::ui::DrumCellMarks;
using synth::ui::drumLaneSpec;

namespace {

constexpr DrumLaneKind kAllKinds[] = {DrumLaneKind::Loudness, DrumLaneKind::Chance, DrumLaneKind::Repeat,
                                      DrumLaneKind::Nudge, DrumLaneKind::Condition};

// The lane's value for a note, as the model holds it (the encoding in DrumLaneSpec.h).
int modelValue(DrumLaneKind kind, const synth::MidiNote& n) {
    switch (kind) {
    case DrumLaneKind::Loudness:
        return n.velocity;
    case DrumLaneKind::Chance:
        return n.chance;
    case DrumLaneKind::Repeat:
        return n.repeat * 2 + (n.flam ? 1 : 0);
    case DrumLaneKind::Nudge:
        return static_cast<int>(std::lround(n.nudge * 100.0));
    case DrumLaneKind::Condition:
        return n.condOf * 16 + n.condHit;
    }
    return 0;
}

std::vector<int> modelValues(GrooveFixture& f, DrumLaneKind kind, int pitch) {
    std::vector<int> out;
    for (const auto& n : f.c().notes)
        if (n.pitch == pitch)
            out.push_back(modelValue(kind, n));
    return out;
}

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

void selectRow(GrooveFixture& f, int pitch) { f.grid.getRowLabels().getNameButton(pitch)->onClick(); }

} // namespace

//==============================================================================
// Chips and stacking

TEST(DrumMachineLanesTest, FiveChipsInOrderEachNamedAndTipped) {
    GrooveFixture f;
    auto& chips = f.grid.getLaneChips();
    ASSERT_EQ(chips.getNumChips(), 5);
    const char* names[] = {"Loudness", "Chance", "Repeat", "Nudge", "Condition"};
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(chips.getChip(i)->getButtonText(), names[i]);
        EXPECT_EQ(chips.getChip(i)->getTitle(), juce::String(names[i]) + " lane");
        EXPECT_EQ(chips.getChip(i)->getTooltip(), "Show the " + juce::String(names[i]) + " lane");
        EXPECT_FALSE(f.grid.getLane(static_cast<DrumLaneKind>(i)).isVisible());
    }
}

TEST(DrumMachineLanesTest, EveryChipGrowsItsLaneAndFoldsItBack) {
    for (const auto kind : kAllKinds) {
        SCOPED_TRACE(drumLaneSpec(kind).name.toStdString());
        GrooveFixture f;
        const int closedBand = f.grid.bottomBandHeight();
        f.clickChip(kind);
        f.grid.finishMotionsForTest();
        auto& lane = f.grid.getLane(kind);
        EXPECT_TRUE(f.grid.isLaneShown(kind));
        EXPECT_TRUE(lane.isVisible());
        EXPECT_EQ(f.grid.bottomBandHeight() - closedBand, DrumMachineComponent::kLaneHeight);
        EXPECT_LE(lane.getBottom(), f.grid.getLaneChips().getY()) << "the lane sits under the grid, above the chips";
        EXPECT_LE(f.grid.gridRegion().getBottom(), lane.getY());
        f.clickChip(kind);
        f.grid.finishMotionsForTest();
        EXPECT_FALSE(lane.isVisible());
        EXPECT_EQ(f.grid.bottomBandHeight(), closedBand);
    }
}

TEST(DrumMachineLanesTest, SeveralLanesStackUnderTheGridInChipOrderWhateverOrderTheyWereTurnedOn) {
    GrooveFixture f(800);
    f.showLane(DrumLaneKind::Condition);
    f.showLane(DrumLaneKind::Chance);
    f.showLane(DrumLaneKind::Loudness);
    f.showLane(DrumLaneKind::Nudge);
    f.showLane(DrumLaneKind::Repeat);
    int expectedTop = f.grid.gridRegion().getBottom();
    for (const auto kind : kAllKinds) {
        auto& lane = f.grid.getLane(kind);
        EXPECT_EQ(lane.getY(), expectedTop) << drumLaneSpec(kind).name.toStdString();
        EXPECT_EQ(lane.getHeight(), DrumMachineComponent::kLaneHeight);
        expectedTop = lane.getBottom() + 0;
    }
    EXPECT_LE(expectedTop, f.grid.getLaneChips().getY());
}

TEST(DrumMachineLanesTest, AShortPanelKeepsTheGridUsableAndSqueezesTheLanes) {
    GrooveFixture f(360);
    for (const auto kind : kAllKinds)
        f.showLane(kind);
    EXPECT_GE(f.grid.gridRegion().getHeight(), DrumMachineComponent::kMinGridHeight);
    int previousBottom = f.grid.gridRegion().getBottom();
    for (const auto kind : kAllKinds) {
        auto& lane = f.grid.getLane(kind);
        EXPECT_GE(lane.getY(), previousBottom) << "lanes never overlap the grid or each other";
        EXPECT_GE(lane.getHeight(), DrumMachineComponent::kMinLaneHeight);
        EXPECT_LE(lane.getHeight(), DrumMachineComponent::kLaneHeight);
        previousBottom = lane.getBottom();
    }
    EXPECT_LE(previousBottom, f.grid.getLaneChips().getY());
    EXPECT_GE(f.grid.gridRegion().getY(), DrumMachineComponent::kToolbarHeight);
    // The grid still scrolls inside its shrunk area, so every row stays reachable.
    f.grid.setFocusCell({f.grid.getLaneCount() - 1, 0});
    const auto cell = f.grid.cellBounds(f.grid.getLaneCount() - 1, 0);
    EXPECT_TRUE(f.grid.gridRegion().contains(cell));
}

TEST(DrumMachineLanesTest, LanesGrowOverTimeAndAFoldingLaneDoesNotStopAGrowingOne) {
    AnimationModeGuard fullMotion(synth::ui::AnimationMode::full);
    GrooveFixture f(800);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.clickChip(DrumLaneKind::Loudness);
    EXPECT_TRUE(f.grid.isLaneMotionRunning());
    f.clickChip(DrumLaneKind::Chance); // while the first still grows
    EXPECT_TRUE(f.grid.isLaneMotionRunning());
    f.grid.finishMotionsForTest();
    EXPECT_FALSE(f.grid.isLaneMotionRunning());
    EXPECT_EQ(f.grid.getLane(DrumLaneKind::Loudness).getHeight(), DrumMachineComponent::kLaneHeight);
    EXPECT_EQ(f.grid.getLane(DrumLaneKind::Chance).getHeight(), DrumMachineComponent::kLaneHeight);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumMachineLanesTest, LanesLandAtOnceWithAnimationsOff) {
    AnimationModeGuard off(synth::ui::AnimationMode::off);
    GrooveFixture f(800);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.clickChip(DrumLaneKind::Nudge);
    EXPECT_FALSE(f.grid.isLaneMotionRunning());
    EXPECT_EQ(f.grid.getLane(DrumLaneKind::Nudge).getHeight(), DrumMachineComponent::kLaneHeight);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

//==============================================================================
// Every lane: drag, keys, text

TEST(DrumMachineLanesTest, EveryLaneIsNamedTippedAndATabStop) {
    GrooveFixture f(800);
    for (const auto kind : kAllKinds)
        f.showLane(kind);
    for (const auto kind : kAllKinds) {
        auto& lane = f.grid.getLane(kind);
        EXPECT_EQ(lane.getTitle(), drumLaneSpec(kind).name + " lane");
        EXPECT_TRUE(lane.getTooltip().isNotEmpty());
        EXPECT_TRUE(lane.getWantsKeyboardFocus());
    }
    // The audit finds no unnamed or untipped control among the lanes and the two header knobs.
    for (const auto& gap : synth::test::auditAccessibility(f.grid)) {
        EXPECT_FALSE(gap.path.contains("DrumHitLane")) << gap.path;
        EXPECT_FALSE(gap.path.contains("drumHumanise")) << gap.path;
        EXPECT_FALSE(gap.path.contains("drumSwing")) << gap.path;
    }
    auto& humanise = f.grid.getHumaniseKnob();
    EXPECT_TRUE(humanise.isVisible());
    EXPECT_EQ(humanise.getTitle(), "Humanise");
    EXPECT_TRUE(humanise.getTooltip().isNotEmpty());
}

TEST(DrumMachineLanesTest, ADragAcrossAnyLaneSetsARampAsOneUndoStepAndCommitsWhatItPreviewed) {
    for (const auto kind : kAllKinds) {
        SCOPED_TRACE(drumLaneSpec(kind).name.toStdString());
        GrooveFixture f;
        f.addHits(42, {0, 2, 4, 6});
        selectRow(f, 42);
        f.showLane(kind);
        auto& lane = f.grid.getLane(kind);
        ASSERT_EQ(lane.getHits().size(), 4u);
        const auto before = modelValues(f, kind, 42);

        const auto first = inLane(lane, 0, 0.1f);
        const auto last = inLane(lane, 3, 0.9f);
        lane.mouseDown(leftClick(lane, first));
        for (int i = 1; i <= 8; ++i) {
            const float t = static_cast<float>(i) / 8.0f;
            lane.mouseDrag(leftDrag(lane, first + (last - first) * t, first));
        }
        std::vector<int> previewed;
        for (int i = 0; i < 4; ++i)
            previewed.push_back(lane.valueOf(i));
        EXPECT_EQ(modelValues(f, kind, 42), before) << "nothing is written while the drag is live";
        EXPECT_NE(previewed, before) << "the bars preview it";
        EXPECT_LT(previewed.front(), previewed.back()) << "the ramp rises from the low press to the high release";
        lane.mouseUp(leftDrag(lane, last, first));

        EXPECT_EQ(modelValues(f, kind, 42), previewed) << "the committed values are the previewed ones";
        EXPECT_NE(modelValues(f, kind, 42), before);
        f.undo.undo();
        EXPECT_EQ(modelValues(f, kind, 42), before);
        EXPECT_FALSE(f.undo.canUndo()) << "the whole drag was one undo step";
    }
}

TEST(DrumMachineLanesTest, KickIsPickedUntilARowNameIsClicked) {
    GrooveFixture f;
    EXPECT_EQ(f.grid.getSelectedPitch(), 36);
    auto* kick = f.grid.getRowLabels().getNameButton(36);
    auto* hat = f.grid.getRowLabels().getNameButton(42);
    ASSERT_NE(kick, nullptr);
    ASSERT_NE(hat, nullptr);
    EXPECT_TRUE(kick->getToggleState()) << "the picked row's name is the lit one, drawn in the accent colour";
    EXPECT_FALSE(hat->getToggleState());
    EXPECT_EQ(hat->getTitle(), "Closed hat row");
    hat->onClick();
    EXPECT_EQ(f.grid.getSelectedPitch(), 42);
    EXPECT_TRUE(hat->getToggleState());
    EXPECT_FALSE(kick->getToggleState());
}

TEST(DrumMachineLanesTest, DrawingAcrossFourHatsInTheLoudnessLaneSetsAStraightRamp) {
    GrooveFixture f;
    f.addHits(42, {0, 2, 4, 6});
    selectRow(f, 42);
    f.showLoudness();
    auto& lane = f.grid.getLane(DrumLaneKind::Loudness);
    ASSERT_EQ(lane.getHits().size(), 4u);
    const auto first = inLane(lane, 0, 0.1f);
    const auto last = inLane(lane, 3, 0.9f);
    lane.mouseDown(leftClick(lane, first));
    for (int i = 1; i <= 8; ++i)
        lane.mouseDrag(leftDrag(lane, first + (last - first) * (static_cast<float>(i) / 8.0f), first));
    EXPECT_EQ(f.velocities(42), (std::vector<int>{100, 100, 100, 100})) << "nothing is written while the drag is live";
    EXPECT_NE(lane.valueOf(0), 100) << "the bars preview it";
    lane.mouseUp(leftDrag(lane, last, first));
    const auto v = f.velocities(42);
    ASSERT_EQ(v.size(), 4u);
    EXPECT_LT(v[0], v[1]);
    EXPECT_LT(v[1], v[2]);
    EXPECT_LT(v[2], v[3]);
    EXPECT_NEAR(v[0], 13, 3);
    EXPECT_NEAR(v[3], 114, 3);
    EXPECT_NEAR(v[1] - v[0], v[2] - v[1], 3) << "a straight ramp";
}

TEST(DrumMachineLanesTest, TheLoudnessKeyboardMovesBetweenHitsAndChangesLoudnessByFive) {
    GrooveFixture f;
    f.addHits(36, {0, 4, 8});
    f.showLoudness();
    auto& lane = f.grid.getLane(DrumLaneKind::Loudness);
    EXPECT_TRUE(lane.getWantsKeyboardFocus());
    EXPECT_EQ(lane.getTitle(), "Loudness lane");
    lane.setFocusedHit(0);
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(lane.getFocusedHit(), 1);
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 5, loudness 100");
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::upKey)));
    EXPECT_EQ(f.velocities(36), (std::vector<int>{100, 105, 100}));
    EXPECT_EQ(lane.getFocusedHit(), 1) << "the keyboard stays on the hit it changed";
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 5, loudness 105");
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier)));
    EXPECT_EQ(f.velocities(36), (std::vector<int>{100, 80, 100}));
    f.undo.undo();
    EXPECT_EQ(f.velocities(36), (std::vector<int>{100, 105, 100})) << "each key press is its own undo step";
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_EQ(lane.getFocusedHit(), 0) << "Left stops at the first hit";
}

TEST(DrumMachineLanesTest, ALaneEditReachesEveryNoteOfTheCellAndNoOtherCell) {
    GrooveFixture f;
    f.addHits(36, {0, 4});
    f.doc.addNote(f.clip, makeNote(0.05, 36, 0.1)); // a second note in step 0
    f.grid.refreshFromDoc();
    f.showLane(DrumLaneKind::Chance);
    auto& lane = f.grid.getLane(DrumLaneKind::Chance);
    lane.setFocusedHit(0);
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::downKey)));
    int atZero = 0, others = 0;
    for (const auto& n : f.c().notes)
        (n.startBeat < 0.25 ? atZero : others) += n.chance == 95 ? 1 : 0;
    EXPECT_EQ(atZero, 2);
    EXPECT_EQ(others, 0);
}

TEST(DrumMachineLanesTest, ChanceKeysStepFiveOrTwentyFiveAndReadOut) {
    GrooveFixture f;
    f.addHits(36, {0, 4, 8});
    f.showLane(DrumLaneKind::Chance);
    auto& lane = f.grid.getLane(DrumLaneKind::Chance);
    lane.setFocusedHit(0);
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 5, chance 100%");
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::downKey)));
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 5, chance 95%");
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier)));
    EXPECT_EQ(modelValues(f, DrumLaneKind::Chance, 36), (std::vector<int>{100, 70, 100}));
    f.undo.undo();
    EXPECT_EQ(modelValues(f, DrumLaneKind::Chance, 36), (std::vector<int>{100, 95, 100})) << "one undo step per press";
    for (int i = 0; i < 30; ++i)
        lane.keyPressed(key(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier));
    EXPECT_EQ(modelValues(f, DrumLaneKind::Chance, 36)[1], 0) << "chance stops at 0";
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_TRUE(lane.keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_EQ(lane.getFocusedHit(), 0);
    EXPECT_FALSE(lane.keyPressed(key(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier)));
}

TEST(DrumMachineLanesTest, ARepeatLaneStepsOffFlamTwoToEight) {
    GrooveFixture f;
    f.addHits(36, {0});
    f.showLane(DrumLaneKind::Repeat);
    auto& lane = f.grid.getLane(DrumLaneKind::Repeat);
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, repeat Off");
    lane.keyPressed(key(juce::KeyPress::upKey));
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, repeat Flam");
    EXPECT_EQ(f.c().notes[0].repeat, 1);
    EXPECT_TRUE(f.c().notes[0].flam);
    lane.keyPressed(key(juce::KeyPress::upKey));
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, repeat 2");
    EXPECT_EQ(f.c().notes[0].repeat, 2);
    EXPECT_FALSE(f.c().notes[0].flam);
    for (int i = 0; i < 10; ++i)
        lane.keyPressed(key(juce::KeyPress::upKey));
    EXPECT_EQ(f.c().notes[0].repeat, 8) << "repeat stops at 8";
    lane.keyPressed(key(juce::KeyPress::downKey));
    EXPECT_EQ(f.c().notes[0].repeat, 7);
    for (int i = 0; i < 10; ++i)
        f.undo.undo();
    EXPECT_EQ(f.c().notes[0].repeat, 1);
    EXPECT_FALSE(f.c().notes[0].flam);
}

TEST(DrumMachineLanesTest, ANudgeLaneDrawsFromTheCentreLineAndStepsFivePercent) {
    GrooveFixture f;
    f.addHits(36, {0, 4});
    ASSERT_TRUE(f.doc.setNoteNudge(f.c().notes[0].id, 0.3));
    ASSERT_TRUE(f.doc.setNoteNudge(f.c().notes[1].id, -0.3));
    f.grid.refreshFromDoc();
    f.showLane(DrumLaneKind::Nudge);
    auto& lane = f.grid.getLane(DrumLaneKind::Nudge);
    const int centre = lane.getHeight() / 2;
    EXPECT_LE(lane.barBounds(0).getBottom(), centre + 1) << "a late hit rises above the centre line";
    EXPECT_GE(lane.barBounds(1).getY(), centre - 1) << "an early hit hangs below it";
    lane.setFocusedHit(0);
    lane.keyPressed(key(juce::KeyPress::upKey));
    EXPECT_DOUBLE_EQ(f.c().notes[0].nudge, 0.35);
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, nudge +35%");
    lane.setFocusedHit(1);
    lane.keyPressed(key(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier));
    EXPECT_DOUBLE_EQ(f.c().notes[1].nudge, -0.05);
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 5, nudge -5%");
}

TEST(DrumMachineLanesTest, AConditionLaneListsTheCyclesAndShowsAnOffListValueByItsText) {
    GrooveFixture f;
    f.addHits(36, {0});
    f.showLane(DrumLaneKind::Condition);
    auto& lane = f.grid.getLane(DrumLaneKind::Condition);
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, condition Always");
    lane.keyPressed(key(juce::KeyPress::upKey));
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, condition 1/2");
    lane.keyPressed(key(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier));
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, condition 3/3") << "Shift steps four options";
    EXPECT_EQ(f.c().notes[0].condHit, 3);
    EXPECT_EQ(f.c().notes[0].condOf, 3);

    ASSERT_TRUE(f.doc.setNoteCondition(f.c().notes[0].id, 3, 5)); // not one of the listed options
    f.grid.refreshFromDoc();
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, condition 3/5");
    lane.keyPressed(key(juce::KeyPress::downKey));
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, condition 4/4") << "Down moves to the option below";
    ASSERT_TRUE(f.doc.setNoteCondition(f.c().notes[0].id, 3, 5));
    f.grid.refreshFromDoc();
    lane.keyPressed(key(juce::KeyPress::upKey));
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, condition 1/8") << "Up moves to the option above";
}

TEST(DrumMachineLanesTest, ARepeatOfThreeWithAFlamKeepsItsTextAndItsFlamUntilEdited) {
    GrooveFixture f;
    f.addHits(36, {0});
    ASSERT_TRUE(f.doc.setNoteRepeat(f.c().notes[0].id, 3, true));
    f.grid.refreshFromDoc();
    f.showLane(DrumLaneKind::Repeat);
    auto& lane = f.grid.getLane(DrumLaneKind::Repeat);
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 1, repeat 3 + flam");
    EXPECT_EQ(f.c().notes[0].repeat, 3);
    EXPECT_TRUE(f.c().notes[0].flam);
}

TEST(DrumMachineLanesTest, AZeroValueStillHasABarToSeeAndFocus) {
    GrooveFixture f;
    f.addHits(36, {0});
    ASSERT_TRUE(f.doc.setNoteChance(f.c().notes[0].id, 0));
    f.grid.refreshFromDoc();
    f.showLane(DrumLaneKind::Chance);
    EXPECT_GE(f.grid.getLane(DrumLaneKind::Chance).barBounds(0).getHeight(), 1);
}

TEST(DrumMachineLanesTest, AnEmptyRowSaysSoOnEveryLane) {
    GrooveFixture f(800);
    for (const auto kind : kAllKinds) {
        f.showLane(kind);
        EXPECT_EQ(f.grid.getLane(kind).getAccessibilityValueText(), "Kick, no hits");
    }
}

TEST(DrumMachineLanesTest, EveryLaneShowsTheHitsOfThePickedRow) {
    GrooveFixture f(800);
    f.addHits(42, {0, 2, 4, 6}, 80);
    f.addHits(36, {0, 8});
    for (const auto kind : kAllKinds)
        f.showLane(kind);
    for (const auto kind : kAllKinds)
        EXPECT_EQ(f.grid.getLane(kind).getHits().size(), 2u) << "Kick is picked at first";
    selectRow(f, 42);
    for (const auto kind : kAllKinds)
        EXPECT_EQ(f.grid.getLane(kind).getHits().size(), 4u);
    EXPECT_EQ(f.grid.getLane(DrumLaneKind::Loudness).getHits()[1].value, 80);
    EXPECT_EQ(f.grid.getLane(DrumLaneKind::Chance).getHits()[1].value, 100);
}

TEST(DrumMachineLanesTest, TheFocusedCellReadsItsNonDefaultSettings) {
    GrooveFixture f;
    f.addHits(36, {0});
    ASSERT_TRUE(f.doc.setNoteChance(f.c().notes[0].id, 50));
    ASSERT_TRUE(f.doc.setNoteCondition(f.c().notes[0].id, 2, 4));
    f.grid.refreshFromDoc();
    f.grid.setFocusCell({f.grid.rowOfPitch(36), 0});
    EXPECT_EQ(f.grid.getAccessibilityValueText(), "Kick, step 1, hit, loudness 100, chance 50%, condition 2/4");
}

//==============================================================================
// Marks

TEST(DrumMachineMarksTest, EachSettingDescribesItsOwnMark) {
    using synth::ui::describeCellMarks;
    synth::ui::drumgrid::CellHits plain;
    plain.ids.push_back(synth::NoteId{});
    plain.velocity = 100;
    EXPECT_FALSE(describeCellMarks(plain, 0.5, 0).any());
    EXPECT_FALSE(describeCellMarks({}, 0.5, 0).any()) << "an empty cell has no marks";

    auto chance = plain;
    chance.chance = 40;
    EXPECT_TRUE(describeCellMarks(chance, 0.5, 0).dimmed);

    auto repeat = plain;
    repeat.repeat = 3;
    EXPECT_EQ(describeCellMarks(repeat, 0.5, 0).repeatTicks, 3);

    auto flam = plain;
    flam.flam = true;
    EXPECT_TRUE(describeCellMarks(flam, 0.5, 0).flam);

    auto cond = plain;
    cond.condHit = 2;
    cond.condOf = 4;
    EXPECT_EQ(describeCellMarks(cond, 0.5, 0).condition, "2/4");

    auto nudge = plain;
    nudge.nudge = -0.25;
    const auto early = describeCellMarks(nudge, 0.5, 0);
    EXPECT_TRUE(early.playsOffStart);
    EXPECT_DOUBLE_EQ(early.playOffset, -0.25);

    EXPECT_DOUBLE_EQ(describeCellMarks(nudge, 0.75, 1).playOffset, 0.25) << "swing and nudge add up on an odd step";
    EXPECT_FALSE(describeCellMarks(plain, 0.75, 2).playsOffStart) << "an even step does not swing";
    EXPECT_TRUE(describeCellMarks(plain, 0.75, 3).playsOffStart);
}

TEST(DrumMachineMarksTest, TheGridReportsTheMarksOfAHitAfterALaneEdit) {
    GrooveFixture f;
    f.addHits(36, {0, 1});
    f.showLane(DrumLaneKind::Repeat);
    auto& lane = f.grid.getLane(DrumLaneKind::Repeat);
    lane.setFocusedHit(1);
    for (int i = 0; i < 4; ++i)
        lane.keyPressed(key(juce::KeyPress::upKey)); // Flam, 2, 3, 4
    const int row = f.grid.rowOfPitch(36);
    EXPECT_EQ(f.grid.getCellMarks(row, 1).repeatTicks, 4);
    EXPECT_FALSE(f.grid.getCellMarks(row, 0).any());
    EXPECT_FALSE(f.grid.getCellMarks(row, 5).any()) << "an empty cell";
}

namespace {
juce::Colour pixelOf(GrooveFixture& f, int x, int y) {
    juce::Image image(juce::Image::ARGB, f.grid.getWidth(), f.grid.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    f.grid.paintEntireComponent(g, false);
    return image.getPixelAt(x, y);
}
} // namespace

TEST(DrumMachineMarksTest, AChanceBelowOneHundredDrawsTheFillDimmer) {
    GrooveFixture f;
    f.addHits(36, {0});
    const auto cell = f.grid.cellBounds(f.grid.rowOfPitch(36), 0);
    const int x = cell.getCentreX(), y = cell.getBottom() - 4;
    const auto full = pixelOf(f, x, y);
    ASSERT_TRUE(f.doc.setNoteChance(f.c().notes[0].id, 50));
    f.grid.refreshFromDoc();
    f.grid.finishMotionsForTest();
    const auto dim = pixelOf(f, x, y);
    EXPECT_LT(dim.getRed(), full.getRed());
}

TEST(DrumMachineMarksTest, ANudgedHitShowsATickWhereItPlays) {
    GrooveFixture f;
    f.addHits(36, {1});
    const auto cell = f.grid.cellBounds(f.grid.rowOfPitch(36), 1);
    const int x = cell.getX() + cell.getWidth() / 2, y = cell.getCentreY();
    const auto straight = pixelOf(f, x, y);
    ASSERT_TRUE(f.doc.setNoteNudge(f.c().notes[0].id, 0.5));
    f.grid.refreshFromDoc();
    f.grid.finishMotionsForTest();
    EXPECT_GT(pixelOf(f, x, y).getGreen(), straight.getGreen()) << "the tick at half a step is lighter than the fill";
}

TEST(DrumMachineMarksTest, AnEarlyHitsTickStaysInsideItsCell) {
    GrooveFixture f;
    f.addHits(36, {4});
    ASSERT_TRUE(f.doc.setNoteNudge(f.c().notes[0].id, -0.4));
    f.grid.refreshFromDoc();
    f.grid.finishMotionsForTest();
    const auto cell = f.grid.cellBounds(f.grid.rowOfPitch(36), 4);
    const auto tick = pixelOf(f, cell.getX() + 1, cell.getCentreY());
    f.doc.setNoteNudge(f.c().notes[0].id, 0.0);
    f.grid.refreshFromDoc();
    f.grid.finishMotionsForTest();
    EXPECT_GT(tick.getGreen(), pixelOf(f, cell.getX() + 1, cell.getCentreY()).getGreen())
        << "the early tick is clamped to the cell's left edge, not drawn over the neighbour";
}

// Writes the grid with every lane open and every mark to a PNG for a person to look at, only when DRUM_LANES_PNG names
// a file (the same convention as the other RendersToPngForVisualInspection tests).
TEST(DrumMachineMarksTest, RendersToPngForVisualInspection) {
    GrooveFixture f(560);
    f.grid.setSize(1000, 560);
    f.addHits(36, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    const auto& notes = f.c().notes;
    f.doc.setNoteChance(notes[1].id, 50);
    f.doc.setNoteRepeat(notes[2].id, 4, false);
    f.doc.setNoteRepeat(notes[3].id, 1, true);
    f.doc.setNoteCondition(notes[4].id, 2, 4);
    f.doc.setNoteNudge(notes[5].id, 0.3);
    f.doc.setNoteNudge(notes[6].id, -0.3);
    f.doc.setNoteChance(notes[7].id, 70);
    f.doc.setNoteRepeat(notes[7].id, 3, true);
    f.doc.setNoteCondition(notes[7].id, 1, 2);
    f.doc.setClipSwing(f.clip, 0.6);
    f.doc.setClipHumanise(f.clip, 0.3);
    f.grid.refreshFromDoc();
    for (const auto kind : kAllKinds)
        f.showLane(kind);
    f.grid.finishMotionsForTest();
    juce::Image image(juce::Image::ARGB, f.grid.getWidth(), f.grid.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    f.grid.paintEntireComponent(g, false);
    EXPECT_TRUE(image.isValid());
    if (const char* path = std::getenv("DRUM_LANES_PNG")) {
        juce::File file(path);
        file.deleteFile();
        juce::FileOutputStream stream(file);
        if (stream.openedOk())
            juce::PNGImageFormat().writeImageToStream(image, stream);
    }
}

TEST(DrumMachineLanesTest, ANarrowHeaderKeepsTheKnobsAndTheIdeasButton) {
    GrooveFixture f;
    f.grid.setSize(800, 360);
    for (const auto* c : {static_cast<juce::Component*>(&f.grid.getSwingKnob()),
                          static_cast<juce::Component*>(&f.grid.getHumaniseKnob()),
                          static_cast<juce::Component*>(&f.grid.getIdeasButton())})
        EXPECT_GE(c->getWidth(), 18) << c->getTitle() << " keeps room at 800 px";
    EXPECT_LE(f.grid.getIdeasButton().getRight(), 800);
}
