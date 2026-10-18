// TimelineSnapNoteValueTests.cpp -- the timeline Snap offers triplet and dotted note values: the persisted ints stay
// put, the grid maths is exact for steps that are not powers of two, the grid cycles inside a kind, and the strip's
// box is the shared note-length menu.

#include "UI/Layout/NoteLengthMenu/NoteLengthPopup.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineSnapNames.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::NoteKind;
using synth::ui::NoteLengthPopup;
using synth::ui::TimelineViewState;
using Snap = TimelineViewState::Snap;

namespace {

struct SkipDesktop {
    SkipDesktop() { NoteLengthPopup::setSkipDesktopForTest(true); }
    ~SkipDesktop() { NoteLengthPopup::setSkipDesktopForTest(false); }
};

juce::MouseEvent mouseEvent(juce::Component& comp) {
    const auto at = comp.getLocalBounds().toFloat().getCentre();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
}

void click(juce::Button& button) {
    auto& comp = static_cast<juce::Component&>(button);
    comp.mouseEnter(mouseEvent(comp));
    comp.mouseDown(mouseEvent(comp));
    comp.mouseUp(mouseEvent(comp));
}

TimelineViewState stateAt(Snap snap) {
    TimelineViewState state;
    state.snap = snap;
    return state;
}

} // namespace

// Regression test for FRO1017: the snap is saved as a raw int, so the triplet and dotted values must come after every
// older one and no older value may move.
TEST(TimelineSnapValues, EveryOldValueKeepsItsSavedInteger) {
    EXPECT_EQ((int)Snap::Off, 0);
    EXPECT_EQ((int)Snap::Bar, 1);
    EXPECT_EQ((int)Snap::Whole, 2);
    EXPECT_EQ((int)Snap::Half, 3);
    EXPECT_EQ((int)Snap::Quarter, 4);
    EXPECT_EQ((int)Snap::Eighth, 5);
    EXPECT_EQ((int)Snap::Sixteenth, 6);
    EXPECT_EQ((int)Snap::ThirtySecond, 7);
    EXPECT_EQ((int)Snap::SixtyFourth, 8);
    EXPECT_EQ((int)Snap::HundredTwentyEighth, 9);
    EXPECT_EQ((int)Snap::WholeTriplet, 10);
    EXPECT_EQ((int)Snap::HundredTwentyEighthTriplet, 17);
    EXPECT_EQ((int)Snap::WholeDotted, 18);
    EXPECT_EQ((int)Snap::HundredTwentyEighthDotted, 25);
    EXPECT_EQ(TimelineViewState::kLastSnap, Snap::HundredTwentyEighthDotted);
}

TEST(TimelineSnapValues, EveryOldValueStillMeansTheSameNumberOfBeats) {
    const std::pair<Snap, double> old[] = {{Snap::Off, 0.0},
                                           {Snap::Bar, 3.0},
                                           {Snap::Whole, 4.0},
                                           {Snap::Half, 2.0},
                                           {Snap::Quarter, 1.0},
                                           {Snap::Eighth, 0.5},
                                           {Snap::Sixteenth, 0.25},
                                           {Snap::ThirtySecond, 0.125},
                                           {Snap::SixtyFourth, 0.0625},
                                           {Snap::HundredTwentyEighth, 0.03125}};
    for (const auto& [snap, beats] : old)
        EXPECT_DOUBLE_EQ(stateAt(snap).divisionBeatsRaw(3.0), beats) << (int)snap;
}

TEST(TimelineSnapValues, TripletsAreTwoThirdsAndDottedValuesOneAndAHalfTimesTheStraightLength) {
    EXPECT_NEAR(stateAt(Snap::EighthTriplet).divisionBeatsRaw(4.0), 1.0 / 3.0, 1e-15);
    EXPECT_NEAR(stateAt(Snap::QuarterTriplet).divisionBeatsRaw(4.0), 2.0 / 3.0, 1e-15);
    EXPECT_NEAR(stateAt(Snap::WholeTriplet).divisionBeatsRaw(4.0), 8.0 / 3.0, 1e-15);
    EXPECT_DOUBLE_EQ(stateAt(Snap::EighthDotted).divisionBeatsRaw(4.0), 0.75);
    EXPECT_DOUBLE_EQ(stateAt(Snap::QuarterDotted).divisionBeatsRaw(4.0), 1.5);
    EXPECT_DOUBLE_EQ(stateAt(Snap::WholeDotted).divisionBeatsRaw(4.0), 6.0);
    EXPECT_DOUBLE_EQ(stateAt(Snap::HundredTwentyEighthDotted).divisionBeatsRaw(4.0), 0.046875);
    EXPECT_EQ(stateAt(Snap::SixteenthTriplet).divisionBeatsRaw(7.0),
              stateAt(Snap::SixteenthTriplet).divisionBeatsRaw(2.0))
        << "only Bar reads the time signature";
}

TEST(TimelineSnapValues, AnEighthTripletSnapsToThirdsOfABeatWithoutDrift) {
    const auto state = stateAt(Snap::EighthTriplet);
    EXPECT_EQ(state.snapBeat(0.16, 4.0), 0.0);
    EXPECT_EQ(state.snapBeat(0.17, 4.0), 1.0 / 3.0);
    EXPECT_EQ(state.snapBeat(0.49, 4.0), 1.0 / 3.0);
    EXPECT_EQ(state.snapBeat(0.51, 4.0), 2.0 / 3.0);
    EXPECT_EQ(state.snapBeat(1.0, 4.0), 1.0) << "three steps are exactly one beat";
    // Far into a long arrangement every grid line is still the exact k/3, however it is reached.
    for (int k : {7, 301, 2999, 30001})
        EXPECT_EQ(state.snapBeat((double)k / 3.0 + 0.01, 4.0), (double)k / 3.0) << k;
}

TEST(TimelineSnapValues, AnEighthDottedSnapsToThreeSixteenths) {
    const auto state = stateAt(Snap::EighthDotted);
    EXPECT_EQ(state.snapBeat(0.3, 4.0), 0.0);
    EXPECT_EQ(state.snapBeat(0.4, 4.0), 0.75);
    EXPECT_EQ(state.snapBeat(1.0, 4.0), 0.75);
    EXPECT_EQ(state.snapBeat(1.2, 4.0), 1.5);
    EXPECT_EQ(state.snapBeat(2.3, 4.0), 2.25);
}

TEST(TimelineSnapValues, FloorAndCeilLandOnTheGridLineEvenWhenTheBeatIsARoundingErrorShortOfIt) {
    const auto state = stateAt(Snap::EighthTriplet);
    EXPECT_EQ(state.floorToGrid(1.0, 4.0), 1.0) << "1.0 is a grid line, not between two";
    EXPECT_EQ(state.floorToGrid(1.0 - 1e-12, 4.0), 1.0);
    EXPECT_EQ(state.floorToGrid(0.9, 4.0), 2.0 / 3.0);
    EXPECT_EQ(state.ceilToGrid(0.9, 4.0), 1.0);
    EXPECT_EQ(state.ceilToGrid(1.0, 4.0), 1.0);
    EXPECT_EQ(stateAt(Snap::Off).floorToGrid(0.9, 4.0), 0.9);
}

TEST(TimelineSnapValues, StepsStayInTheKindAndStopAtTheEnds) {
    EXPECT_EQ(TimelineViewState::stepWithinKind(Snap::EighthTriplet, 1), Snap::SixteenthTriplet);
    EXPECT_EQ(TimelineViewState::stepWithinKind(Snap::EighthTriplet, -1), Snap::QuarterTriplet);
    EXPECT_EQ(TimelineViewState::stepWithinKind(Snap::WholeTriplet, -1), Snap::WholeTriplet) << "no leap to Bar";
    EXPECT_EQ(TimelineViewState::stepWithinKind(Snap::HundredTwentyEighthTriplet, 1), Snap::HundredTwentyEighthTriplet);
    EXPECT_EQ(TimelineViewState::stepWithinKind(Snap::WholeDotted, -1), Snap::WholeDotted);
    EXPECT_EQ(TimelineViewState::stepWithinKind(Snap::QuarterDotted, 1), Snap::EighthDotted);
    EXPECT_EQ(TimelineViewState::stepWithinKind(Snap::Whole, -1), Snap::Bar)
        << "straight keeps Bar as its coarsest stop";
    EXPECT_EQ(TimelineViewState::stepWithinKind(Snap::HundredTwentyEighth, 1), Snap::HundredTwentyEighth);
}

TEST(TimelineSnapValues, ALineBetweenBeatsIsDrawnForTripletsAndDottedQuartersButNotForWholeBeats) {
    EXPECT_TRUE(TimelineViewState::linesFallOffBeats(1.0 / 3.0));
    EXPECT_TRUE(TimelineViewState::linesFallOffBeats(0.75));
    EXPECT_TRUE(TimelineViewState::linesFallOffBeats(1.5));
    EXPECT_FALSE(TimelineViewState::linesFallOffBeats(1.0));
    EXPECT_FALSE(TimelineViewState::linesFallOffBeats(4.0));
    EXPECT_FALSE(TimelineViewState::linesFallOffBeats(0.0));
}

TEST(TimelineSnapValues, NamesFollowTheEnumSoAnItemIdIsTheSnapIntPlusOne) {
    const auto names = synth::ui::snapValueNames();
    ASSERT_EQ(names.size(), 26);
    EXPECT_EQ(names[(int)Snap::Off], "Off");
    EXPECT_EQ(names[(int)Snap::Bar], "Bar");
    EXPECT_EQ(names[(int)Snap::Whole], "1/1");
    EXPECT_EQ(names[(int)Snap::Eighth], "1/8");
    EXPECT_EQ(names[(int)Snap::EighthTriplet], "1/8T");
    EXPECT_EQ(names[(int)Snap::HundredTwentyEighthDotted], "1/128D");
}

TEST(TimelineSnapStrip, TheBoxShowsTheFullNameOfTheSnapAndFollowsTheViewState) {
    synth::ui::TimelinePanelComponent panel;
    auto& box = panel.getSnapCombo();
    EXPECT_EQ(box.getNumItems(), 26);
    panel.setSnapValue(Snap::EighthTriplet);
    EXPECT_EQ(box.getText(), "1/8T");
    EXPECT_EQ(box.getSelectedId(), (int)Snap::EighthTriplet + 1);
    panel.setSnapValue(Snap::SixteenthDotted);
    EXPECT_EQ(box.getText(), "1/16D");
    panel.setSnapValue(Snap::Off);
    EXPECT_EQ(box.getText(), "Off");
}

TEST(TimelineSnapStrip, GridCyclingStaysInTheCurrentKind) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSnapValue(Snap::EighthTriplet);
    panel.cycleSnapValue(1);
    EXPECT_EQ(panel.getViewState().snap, Snap::SixteenthTriplet);
    panel.cycleSnapValue(-1);
    panel.cycleSnapValue(-1);
    EXPECT_EQ(panel.getViewState().snap, Snap::QuarterTriplet);
    for (int i = 0; i < 12; ++i)
        panel.cycleSnapValue(-1);
    EXPECT_EQ(panel.getViewState().snap, Snap::WholeTriplet) << "stops at the coarsest triplet";
}

TEST(TimelineSnapStrip, OffAndBarAreLeadingChipsBehindTheKindLetters) {
    SkipDesktop skip;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(900, 200);
    panel.setSnapValue(Snap::Quarter);
    auto& box = panel.getSnapCombo();
    box.showPopup();
    ASSERT_NE(box.getPopup(), nullptr);
    const auto chips = box.getPopup()->shownChips();
    ASSERT_EQ(chips.size(), 2u + 8u);
    EXPECT_EQ(chips[0]->getName(), "Off");
    EXPECT_EQ(chips[1]->getName(), "Bar");
    EXPECT_EQ(chips[2]->getName(), "1");
    EXPECT_TRUE(box.getPopup()->kindButton(NoteKind::dotted).isEnabled()) << "the timeline has dotted values";
    click(*chips[1]);
    EXPECT_EQ(panel.getViewState().snap, Snap::Bar);
}

TEST(TimelineSnapStrip, ClickingTripletThenTheEightChipSetsAnEighthTriplet) {
    SkipDesktop skip;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(900, 200);
    panel.setSnapValue(Snap::Quarter);
    auto& box = panel.getSnapCombo();
    box.showPopup();
    ASSERT_NE(box.getPopup(), nullptr);
    click(box.getPopup()->kindButton(NoteKind::triplet));
    const auto chips = box.getPopup()->shownChips();
    ASSERT_EQ(chips.size(), 2u + 8u);
    click(*chips[2 + 3]); // the "8" chip
    EXPECT_EQ(panel.getViewState().snap, Snap::EighthTriplet);
    EXPECT_EQ(box.getText(), "1/8T");
    EXPECT_TRUE(panel.getViewState().snapEnabled);
}
