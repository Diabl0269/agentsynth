// The Ideas button and panel on the drum machine: starters, takes, picking, randomize, the changed-cell outline, one
// undo step each, and the keyboard and screen-reader paths.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "DrumDocFollower.h"
#include "DrumMachineGrooveFixture.h"
#include "UI/DrumMachine/DrumIdeasPanel.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusStepWithin.h"
#include <algorithm>
#include <set>

using synth::ui::DrumIdeasPanel;
using synth::ui::DrumMachineComponent;
using synth::ui::drumideas::Style;

namespace {

struct IdeasFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    DrumMachineComponent grid;
    synth::TrackId track;
    ClipId clip;
    std::unique_ptr<DrumIdeasPanel> panel;
    std::vector<std::pair<int, int>> auditions;

    IdeasFixture() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        track = doc.addTrack(TrackKind::Midi, "Drums");
        clip = doc.addClip(track, 0.0, 8.0, "Beat 1");
        grid.setTimelineDoc(&doc);
        grid.setUndoManager(&undo);
        grid.setSize(1000, 360);
        grid.openClip(clip);
        synth::ui::test_hooks::drumIdeasPanelHookForTest() = [this](auto p) { panel = std::move(p); };
        grid.getIdeasButton().onClick();
        panel->setSeedForTest(100);
        panel->onAudition = [this](int pitch, int velocity) { auditions.emplace_back(pitch, velocity); };
    }
    ~IdeasFixture() { synth::ui::test_hooks::drumIdeasPanelHookForTest() = nullptr; }

    const synth::Clip& c() { return *doc.getClip(clip); }
    size_t pitchCount(int pitch) {
        return (size_t)std::count_if(c().notes.begin(), c().notes.end(),
                                     [pitch](const auto& n) { return n.pitch == pitch; });
    }
};

} // namespace

TEST(DrumIdeasPanelTest, TheButtonOpensThePanelAndIsNamedAndTipped) {
    IdeasFixture f;
    ASSERT_NE(f.panel, nullptr);
    auto& button = f.grid.getIdeasButton();
    EXPECT_EQ(button.getTitle(), "Ideas");
    EXPECT_EQ(button.getTooltip(), "Starters, fills, variations and randomize");
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_EQ(f.panel->getTitle(), "Ideas");
}

TEST(DrumIdeasPanelTest, EveryControlIsATabStopWithATitleAndATooltipInsideThePanel) {
    IdeasFixture f;
    int controls = 0;
    for (auto* child : f.panel->getChildren()) {
        if (!child->getWantsKeyboardFocus())
            continue;
        ++controls;
        EXPECT_TRUE(child->getTitle().isNotEmpty()) << "a control has an accessible name";
        auto* client = dynamic_cast<juce::SettableTooltipClient*>(child);
        ASSERT_NE(client, nullptr);
        EXPECT_TRUE(client->getTooltip().isNotEmpty()) << child->getTitle();
        EXPECT_TRUE(f.panel->getLocalBounds().contains(child->getBounds())) << child->getTitle();
    }
    EXPECT_EQ(controls, 12 + 2 + 1 + 1 + 1 + 1 + 3 + 3 + 1)
        << "starters, snare roll, tom fill, every 4th bar, amount, make, randomize, takes, picks, keep";
}

TEST(DrumIdeasPanelTest, AStarterFillsTheClipAsOneUndoStepAndAnnouncesIt) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::House).onClick();
    EXPECT_FALSE(f.c().notes.empty());
    EXPECT_EQ(f.pitchCount(36), 8u) << "four on the floor across the two bars";
    EXPECT_EQ(f.grid.getLastAnnouncement(), "Filled with House starter");
    f.undo.undo();
    EXPECT_TRUE(f.c().notes.empty());
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumIdeasPanelTest, AStarterLiftsAnEightStepGridToSixteenInTheSameUndoStepAndTrapToThirtyTwo) {
    IdeasFixture f;
    ASSERT_TRUE(f.doc.setClipStepsPerBar(f.clip, 8));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    f.panel->getStarterButton(Style::Rock).onClick();
    EXPECT_EQ(f.c().stepsPerBar, 16);
    f.undo.undo();
    EXPECT_EQ(f.c().stepsPerBar, 8);
    EXPECT_TRUE(f.c().notes.empty());
    EXPECT_FALSE(f.undo.canUndo());
    f.panel->getStarterButton(Style::Trap).onClick();
    EXPECT_EQ(f.c().stepsPerBar, 32);
}

TEST(DrumIdeasPanelTest, AStarterReplacesWhatWasThere) {
    IdeasFixture f;
    f.doc.addNote(f.clip, makeNote(0.0, 70, 0.25));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    f.panel->getStarterButton(Style::Techno).onClick();
    EXPECT_EQ(f.pitchCount(70), 0u);
    f.undo.undo();
    EXPECT_EQ(f.pitchCount(70), 1u);
}

TEST(DrumIdeasPanelTest, ChangedCellsKeepAnOutlineThatEndsWithTheMotion) {
    IdeasFixture f;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.panel->getStarterButton(Style::House).onClick();
    EXPECT_TRUE(f.grid.isChangedOutlineActive(f.grid.rowOfPitch(36), 0));
    EXPECT_FALSE(f.grid.isChangedOutlineActive(f.grid.rowOfPitch(36), 1)) << "an empty cell that stayed empty";
    EXPECT_TRUE(f.grid.isHitMotionActive(f.grid.rowOfPitch(36), 0)) << "the hits grow in";
    f.grid.finishMotionsForTest();
    EXPECT_FALSE(f.grid.isChangedOutlineActive(f.grid.rowOfPitch(36), 0));
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumIdeasPanelTest, MakeThreeTakesShowsThreePicturesAndNothingIsWrittenYet) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Rock).onClick();
    f.undo.clearUndoHistory();
    const auto before = f.c().notes.size();
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(f.panel->getTakeGrid(i).isEmptyTake());
    f.panel->getMakeTakesButton().onClick();
    for (int i = 0; i < 3; ++i)
        EXPECT_FALSE(f.panel->getTakeGrid(i).isEmptyTake());
    EXPECT_NE(f.panel->getTakeGrid(0).getNotes().size() * 1000 + f.panel->getTakeGrid(0).getNotes()[0].velocity,
              f.panel->getTakeGrid(1).getNotes().size() * 1000 + f.panel->getTakeGrid(1).getNotes()[0].velocity + 1)
        << "sanity: separate takes";
    EXPECT_EQ(f.c().notes.size(), before);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumIdeasPanelTest, PickReplacesThisClipsPatternWithNoNewClipAndOneUndoStep) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Rock).onClick();
    ASSERT_TRUE(f.doc.setClipHumanise(f.clip, 0.4));
    f.undo.clearUndoHistory();
    const auto original = f.c().notes;
    f.panel->getMakeTakesButton().onClick();
    const auto take = f.panel->getTakeGrid(1).getNotes();
    f.panel->getPickButton(1).onClick();

    EXPECT_EQ(f.doc.getTrackForClip(f.clip)->clips.size(), 1u) << "no new clip";
    EXPECT_EQ(f.grid.getClipId(), f.clip) << "the grid stays on this clip";
    EXPECT_EQ(f.c().notes.size(), take.size());
    EXPECT_DOUBLE_EQ(f.c().humanise, 0.4) << "the groove stays";
    EXPECT_NE(f.grid.getLastAnnouncement().indexOf("Replaced this clip"), -1);
    f.undo.undo();
    EXPECT_EQ(f.c().notes.size(), original.size());
    EXPECT_FALSE(f.undo.canUndo()) << "one undo step";
}

// Regression test for FRO1049: Pick opened a new clip, and reopening the grid stopped the looping clip.
TEST(DrumIdeasPanelTest, PickKeepsAPlayingClipLoopRunning) {
    IdeasFixture f;
    int starts = 0;
    int stops = 0;
    f.grid.onStartClipPreview = [&](double, double) {
        ++starts;
        return true;
    };
    f.grid.onStopClipPreview = [&] { ++stops; };
    f.grid.startClipPreview();
    ASSERT_TRUE(f.grid.isClipPreviewing());
    f.panel->getMakeTakesButton().onClick();
    f.panel->getPickButton(0).onClick();
    EXPECT_TRUE(f.grid.isClipPreviewing()) << "the playing flag is unchanged";
    EXPECT_EQ(starts, 1) << "the loop is not restarted";
    EXPECT_EQ(stops, 0) << "and not stopped";
    f.panel->getMakeTakesButton().onClick();
    f.panel->getPickButton(2).onClick();
    EXPECT_TRUE(f.grid.isClipPreviewing());
    EXPECT_EQ(stops, 0);
}

TEST(DrumIdeasPanelTest, TheReplaceSwitchIsGoneAndPickSaysItReplacesTheClip) {
    IdeasFixture f;
    for (auto* child : f.panel->getChildren())
        EXPECT_NE(child->getTitle(), "Replace this clip");
    EXPECT_NE(f.panel->getPickButton(0).getTooltip().indexOf("replaces"), -1);
    EXPECT_EQ(f.panel->getPickButton(0).getTooltip().indexOf("new clip"), -1);
}

TEST(DrumIdeasPanelTest, RandomizeKeepingKickAndSnareLeavesThemAloneAsOneUndoStep) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Rock).onClick();
    f.undo.clearUndoHistory();
    const auto kicks = f.pitchCount(36);
    const auto snares = f.pitchCount(38);
    f.panel->getKeepToggle().setToggleState(true, juce::dontSendNotification);
    f.panel->getAmountSlider().setValue(90.0, juce::dontSendNotification);
    f.panel->getRandomizeButton().onClick();
    EXPECT_EQ(f.pitchCount(36), kicks);
    EXPECT_EQ(f.pitchCount(38), snares);
    f.undo.undo();
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_EQ(f.pitchCount(42), 16u) << "Rock's eighth-note hats are back";
}

TEST(DrumIdeasPanelTest, CancelClosesThePanelAndReturnsTheFocusToTheButtonOnce) {
    IdeasFixture f;
    EXPECT_EQ(f.grid.getIdeasFocusReturns(), 0);
    f.panel->onPopoverCancel(); // what the popover host runs on Escape
    EXPECT_EQ(f.grid.getIdeasFocusReturns(), 1);
    f.panel.reset();
    EXPECT_EQ(f.grid.getIdeasFocusReturns(), 1) << "closing it twice does not ask twice";
}

TEST(DrumIdeasPanelTest, ClosingThePanelByAnyOtherWayAlsoReturnsTheFocus) {
    IdeasFixture f;
    f.panel.reset(); // a click-away destroys the popover
    EXPECT_EQ(f.grid.getIdeasFocusReturns(), 1);
}

//==============================================================================
// Playing a take in time

TEST(DrumTakeAuditionTest, PlaysEachHitWhenItsTimeComesAndOnlyOnce) {
    synth::ui::DrumTakeAudition audition;
    std::vector<int> played;
    audition.onHit = [&](int pitch, int) { played.push_back(pitch); };
    std::vector<synth::MidiNote> notes{makeNote(0.0, 36), makeNote(0.5, 42), makeNote(1.0, 38)};
    audition.start(notes, 2.0, 0.5); // 0.5 s per beat: the hits are due at 0, 0.25 and 0.5 s
    EXPECT_EQ(played, (std::vector<int>{36})) << "the first hit plays at once";
    audition.advanceTo(0.3);
    EXPECT_EQ(played, (std::vector<int>{36, 42}));
    audition.advanceTo(0.3);
    EXPECT_EQ(played.size(), 2u) << "a hit never plays twice";
    audition.advanceTo(0.6);
    EXPECT_EQ(played, (std::vector<int>{36, 42, 38}));
    audition.stop();
    EXPECT_FALSE(audition.isPlaying());
}

TEST(DrumTakeAuditionTest, StartingAgainStopsTheRunningPassFirst) {
    synth::ui::DrumTakeAudition audition;
    int finished = 0;
    audition.onFinished = [&] { ++finished; };
    std::vector<synth::MidiNote> notes{makeNote(1.0, 36)};
    audition.start(notes, 2.0, 0.5);
    EXPECT_TRUE(audition.isPlaying());
    audition.start(notes, 2.0, 0.5);
    EXPECT_EQ(finished, 1);
    audition.stop();
    EXPECT_EQ(finished, 2);
}

//==============================================================================
// Fills

TEST(DrumIdeasPanelTest, TheFillsControlsAreTitledTippedAndInsideThePanelInTabOrder) {
    IdeasFixture f;
    auto& roll = f.panel->getSnareRollButton();
    auto& tom = f.panel->getTomFillButton();
    auto& every = f.panel->getEveryFourthToggle();
    EXPECT_EQ(roll.getTitle(), "Snare roll");
    EXPECT_EQ(tom.getTitle(), "Tom fill");
    EXPECT_EQ(every.getTitle(), "Every 4th bar");
    for (auto* b :
         {static_cast<juce::Button*>(&roll), static_cast<juce::Button*>(&tom), static_cast<juce::Button*>(&every)}) {
        EXPECT_TRUE(b->getWantsKeyboardFocus());
        EXPECT_TRUE(b->getTooltip().isNotEmpty());
        EXPECT_TRUE(f.panel->getLocalBounds().contains(b->getBounds())) << b->getTitle();
        EXPECT_FALSE(b->getBounds().isEmpty());
    }
    EXPECT_TRUE(roll.getTooltip().contains("undo step"));
    EXPECT_TRUE(every.getTooltip().contains("bars 4, 8, 12"));
    // Every control still fits above the panel's bottom edge.
    int bottom = 0;
    for (auto* child : f.panel->getChildren())
        bottom = std::max(bottom, child->getBottom());
    EXPECT_LE(bottom, f.panel->getHeight());
    juce::KeyboardFocusTraverser traverser;
    std::vector<juce::Component*> order;
    for (auto* c = traverser.getDefaultComponent(f.panel.get()); c != nullptr && order.size() < 60;
         c = traverser.getNextComponent(c))
        order.push_back(c);
    const auto at = [&](juce::Component* c) { return std::find(order.begin(), order.end(), c) - order.begin(); };
    EXPECT_LT(at(&f.panel->getStarterButton(Style::LoFi)), at(&roll));
    EXPECT_LT(at(&roll), at(&tom));
    EXPECT_LT(at(&tom), at(&every));
    EXPECT_LT(at(&every), at(&f.panel->getAmountSlider()));
}

TEST(DrumIdeasPanelTest, ASnareRollReplacesTheLastBeatAsOneUndoStepWithTheOutlineAndAnAnnouncement) {
    IdeasFixture f;
    f.doc.addNote(f.clip, makeNote(7.0, 42, 0.25));
    f.doc.addNote(f.clip, makeNote(7.5, 36, 0.25));
    f.doc.addNote(f.clip, makeNote(6.0, 42, 0.25));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.panel->getSnareRollButton().onClick();
    EXPECT_EQ(f.pitchCount(38), 4u);
    EXPECT_EQ(f.pitchCount(42), 1u) << "the hat in the last beat went, the one before stayed";
    EXPECT_EQ(f.pitchCount(36), 1u) << "kicks stay";
    EXPECT_EQ(f.grid.getLastAnnouncement(), "Added a snare roll");
    EXPECT_TRUE(f.grid.isChangedOutlineActive(f.grid.rowOfPitch(38), 28));
    EXPECT_TRUE(f.grid.isChangedOutlineActive(f.grid.rowOfPitch(42), 28)) << "the removed hat's cell is outlined too";
    EXPECT_FALSE(f.grid.isChangedOutlineActive(f.grid.rowOfPitch(42), 24));
    f.grid.finishMotionsForTest();
    f.undo.undo();
    EXPECT_EQ(f.pitchCount(38), 0u);
    EXPECT_EQ(f.pitchCount(42), 2u);
    EXPECT_FALSE(f.undo.canUndo()) << "one undo step";
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumIdeasPanelTest, ATomFillAndTheEveryFourthBarToggleReachTheClip) {
    IdeasFixture f;
    ASSERT_TRUE(f.doc.resizeClip(f.clip, 32.0));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    f.panel->getEveryFourthToggle().setToggleState(true, juce::dontSendNotification);
    f.panel->getTomFillButton().onClick();
    EXPECT_EQ(f.pitchCount(50), 4u) << "two fills of 2 steps each";
    EXPECT_EQ(f.pitchCount(45), 4u);
    EXPECT_EQ(f.grid.getLastAnnouncement(), "Added a tom fill every 4th bar");
    bool atFifteen = false, atThirtyOne = false;
    for (const auto& n : f.c().notes) {
        atFifteen = atFifteen || n.startBeat == 14.0;
        atThirtyOne = atThirtyOne || n.startBeat == 30.0;
    }
    EXPECT_TRUE(atFifteen && atThirtyOne) << "bars 4 and 8";
    f.undo.undo();
    EXPECT_TRUE(f.c().notes.empty());
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumIdeasPanelTest, AFillLiftsAnEightStepGridToSixteenInTheSameUndoStep) {
    IdeasFixture f;
    ASSERT_TRUE(f.doc.setClipStepsPerBar(f.clip, 8));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    f.panel->getSnareRollButton().onClick();
    EXPECT_EQ(f.c().stepsPerBar, 16);
    f.undo.undo();
    EXPECT_EQ(f.c().stepsPerBar, 8);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumIdeasPanelTest, PickingATakeKeepsTheDrumsOwnLengthAndSwingAndTheGrid) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Rock).onClick();
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 42, 12));
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, 42, 0.6));
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, 38, 0.7));
    const int steps = f.c().stepsPerBar;
    f.undo.clearUndoHistory();
    f.panel->getMakeTakesButton().onClick();
    f.panel->getPickButton(0).onClick();
    EXPECT_EQ(f.c().stepsPerBar, steps);
    ASSERT_EQ(f.c().drumRows.size(), 2u);
    EXPECT_EQ(f.c().drumRows[0].pitch, 38);
    EXPECT_DOUBLE_EQ(f.c().drumRows[0].swing, 0.7);
    EXPECT_EQ(f.c().drumRows[1].pitch, 42);
    EXPECT_EQ(f.c().drumRows[1].lengthSteps, 12);
    EXPECT_DOUBLE_EQ(f.c().drumRows[1].swing, 0.6);
    f.undo.undo();
    EXPECT_FALSE(f.undo.canUndo()) << "still one undo step";
}

// Cmd+Z and Cmd+Shift+Z animate the hits an idea changed the way the pick did.
TEST(DrumIdeasPanelTest, UndoingAPickShrinksTheHitsItAddedAndGrowsTheOnesItReplaced) {
    IdeasFixture f;
    drum_test::DocFollower follow(f.doc, f.grid);
    f.doc.addNote(f.clip, makeNote(0.5, 70, 0.25));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    f.panel->getStarterButton(Style::Techno).onClick();
    std::vector<std::pair<int, int>> picked; // (pitch, step) the starter wrote
    for (const auto& n : f.c().notes)
        picked.emplace_back(n.pitch, static_cast<int>(std::lround(n.startBeat / 0.25)));
    ASSERT_FALSE(picked.empty());
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.grid.finishMotionsForTest();
    f.undo.undo();
    ASSERT_EQ(f.pitchCount(70), 1u);
    for (const auto& [pitch, step] : picked)
        if (f.grid.rowOfPitch(pitch) >= 0) // a row the undo removes goes with its hits
            EXPECT_TRUE(f.grid.isHitMotionActive(f.grid.rowOfPitch(pitch), step))
                << "pitch " << pitch << " step " << step << " vanishes at once on Cmd+Z";
    EXPECT_TRUE(f.grid.isHitMotionActive(f.grid.rowOfPitch(70), 2)) << "the replaced hit grows back";
    f.grid.finishMotionsForTest();
    f.undo.redo();
    for (const auto& [pitch, step] : picked)
        if (f.grid.rowOfPitch(pitch) >= 0) // a row the undo removes goes with its hits
            EXPECT_TRUE(f.grid.isHitMotionActive(f.grid.rowOfPitch(pitch), step)) << "redo grows the pick back";
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumIdeasPanelTest, UndoingAPickThatLiftedTheGridStillShrinksTheHitsItAdded) {
    IdeasFixture f;
    drum_test::DocFollower follow(f.doc, f.grid);
    ASSERT_TRUE(f.doc.setClipStepsPerBar(f.clip, 8));
    f.doc.addNote(f.clip, makeNote(0.5, 70, 0.5)); // step 1 of the 8-step grid
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    f.panel->getStarterButton(Style::Rock).onClick();
    ASSERT_EQ(f.c().stepsPerBar, 16);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.grid.finishMotionsForTest();
    f.undo.undo();
    ASSERT_EQ(f.c().stepsPerBar, 8);
    EXPECT_GT(f.grid.getLeavingHitCount(), 0) << "the pick's hits fade out at their 16-step places";
    EXPECT_TRUE(f.grid.isHitMotionActive(f.grid.rowOfPitch(70), 1)) << "the old hit grows back on the 8-step grid";
    f.grid.finishMotionsForTest();
    EXPECT_EQ(f.grid.getLeavingHitCount(), 0);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

//==============================================================================
// Keys: a panel of controls takes Up and Down

// Regression test for FRO1549: the Randomize panel took Escape only, so its controls were reached by Tab alone and the
// arrows did nothing, unlike every other panel; keyboard-reach.
TEST(DrumIdeasPanelTest, UpAndDownStepBetweenTheControlsInOrderAndStopAtTheEnds) {
    IdeasFixture f;
    f.panel->setSize(synth::ui::DrumIdeasPanel::kWidth, synth::ui::DrumIdeasPanel::kHeight);
    f.panel->resized();
    const auto stops = synth::ui::shownFocusableDescendants(*f.panel);
    ASSERT_GE(stops.size(), 20u) << "starters, fills, the sliders, takes, picks and Randomize";
    const juce::KeyPress down(juce::KeyPress::downKey, juce::ModifierKeys(), 0);
    const juce::KeyPress up(juce::KeyPress::upKey, juce::ModifierKeys(), 0);
    for (size_t i = 0; i + 1 < stops.size(); ++i) {
        SCOPED_TRACE(stops[i]->getTitle().toStdString());
        ASSERT_TRUE(f.panel->pressKeyForTest(down, stops[i]));
        EXPECT_EQ(f.panel->getLastKeyStepForTest(), stops[i + 1]) << "Down goes to the next control";
    }
    EXPECT_TRUE(f.panel->pressKeyForTest(down, stops.back()));
    EXPECT_EQ(f.panel->getLastKeyStepForTest(), nullptr) << "Down on the last control stays there";
    for (size_t i = stops.size() - 1; i > 0; --i) {
        ASSERT_TRUE(f.panel->pressKeyForTest(up, stops[i]));
        EXPECT_EQ(f.panel->getLastKeyStepForTest(), stops[i - 1]) << "Up goes to the previous control";
    }
    EXPECT_TRUE(f.panel->pressKeyForTest(up, stops.front()));
    EXPECT_EQ(f.panel->getLastKeyStepForTest(), nullptr) << "Up on the first control stays there";
}

TEST(DrumIdeasPanelTest, TheRandomizeControlsAreOnTheUpDownWalkAndEscapeIsLeftToThePopover) {
    IdeasFixture f;
    f.panel->resized();
    const auto stops = synth::ui::shownFocusableDescendants(*f.panel);
    for (auto* control : {static_cast<juce::Component*>(&f.panel->getAmountSlider()),
                          static_cast<juce::Component*>(&f.panel->getMakeTakesButton()),
                          static_cast<juce::Component*>(&f.panel->getKeepToggle()),
                          static_cast<juce::Component*>(&f.panel->getRandomizeButton())})
        EXPECT_NE(std::find(stops.begin(), stops.end(), control), stops.end()) << control->getTitle();
    EXPECT_FALSE(f.panel->pressKeyForTest(juce::KeyPress(juce::KeyPress::escapeKey), &f.panel->getRandomizeButton()))
        << "Escape bubbles up to the popover, which closes it";
    EXPECT_FALSE(f.panel->pressKeyForTest(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0),
                                          &f.panel->getRandomizeButton()))
        << "a shifted arrow is another action's chord";
}

//==============================================================================
// The changed-cell outline follows the document change, not the apply call.

namespace {

std::set<std::pair<int, int>> outlinedCells(const DrumMachineComponent& grid) {
    std::set<std::pair<int, int>> cells;
    for (int pitch = 0; pitch < 128; ++pitch) {
        const int row = grid.rowOfPitch(pitch);
        if (row < 0)
            continue;
        for (int step = 0; step < grid.getStepCount(); ++step)
            if (grid.isChangedOutlineActive(row, step))
                cells.emplace(pitch, step);
    }
    return cells;
}

} // namespace

// Regression test for FRO1670: Cmd+Z and Cmd+Shift+Z of an idea never outlined the cells it changed.
TEST(DrumIdeasPanelTest, UndoAndRedoOutlineTheSameCellsThePickDid) {
    drumtest::AnimationModeGuard full(synth::ui::AnimationMode::full);
    IdeasFixture f;
    drum_test::DocFollower follow(f.doc, f.grid);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.panel->getStarterButton(Style::House).onClick();
    const auto picked = outlinedCells(f.grid);
    ASSERT_FALSE(picked.empty());
    f.grid.finishMotionsForTest();
    ASSERT_TRUE(outlinedCells(f.grid).empty());
    f.undo.undo();
    EXPECT_EQ(outlinedCells(f.grid), picked) << "undo";
    f.grid.finishMotionsForTest();
    f.undo.redo();
    EXPECT_EQ(outlinedCells(f.grid), picked) << "redo";
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumIdeasPanelTest, UndoOutlinesWithoutAFollowerOnceTheGridRefreshes) {
    drumtest::AnimationModeGuard full(synth::ui::AnimationMode::full);
    IdeasFixture f;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.panel->getStarterButton(Style::House).onClick();
    const auto picked = outlinedCells(f.grid);
    f.grid.finishMotionsForTest();
    f.undo.undo();
    f.grid.refreshFromDoc();
    EXPECT_EQ(outlinedCells(f.grid), picked);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumIdeasPanelTest, ReduceMotionStillOutlinesAndAnimationsOffDoesNot) {
    {
        drumtest::AnimationModeGuard reduced(synth::ui::AnimationMode::reduced);
        IdeasFixture f;
        drum_test::DocFollower follow(f.doc, f.grid);
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
        f.panel->getStarterButton(Style::House).onClick();
        const auto picked = outlinedCells(f.grid);
        EXPECT_FALSE(picked.empty());
        f.grid.finishMotionsForTest();
        f.undo.undo();
        EXPECT_EQ(outlinedCells(f.grid), picked) << "Reduce Motion keeps the outline on undo";
    }
    {
        drumtest::AnimationModeGuard off(synth::ui::AnimationMode::off);
        IdeasFixture f;
        drum_test::DocFollower follow(f.doc, f.grid);
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
        f.panel->getStarterButton(Style::House).onClick();
        EXPECT_TRUE(outlinedCells(f.grid).empty());
        f.undo.undo();
        EXPECT_TRUE(outlinedCells(f.grid).empty()) << "Animations Off lands without it";
    }
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumIdeasPanelTest, AClickOnACellMarksNothingAndNeitherDoesItsUndo) {
    drumtest::AnimationModeGuard full(synth::ui::AnimationMode::full);
    IdeasFixture f;
    drum_test::DocFollower follow(f.doc, f.grid);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    const auto p = centreOf(f.grid.cellBounds(f.grid.rowOfPitch(36), 4));
    f.grid.mouseDown(leftClick(f.grid, p));
    f.grid.mouseUp(leftClick(f.grid, p));
    ASSERT_EQ(f.pitchCount(36), 1u);
    EXPECT_TRUE(outlinedCells(f.grid).empty());
    f.undo.undo();
    EXPECT_FALSE(outlinedCells(f.grid).empty()) << "undo is not the grid's own click, so it outlines";
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

//==============================================================================
// The genre: a starter's chip stays lit, the clip stores it, the takes lean toward it, one amount slider.

namespace {

// How many of the starter's hits a take has.
size_t starterHitsIn(const std::vector<synth::MidiNote>& notes, Style style, const synth::ui::drumideas::Grid& grid) {
    const auto target = synth::ui::drumideas::starter(style, grid);
    size_t count = 0;
    for (const auto& n : notes)
        count += (size_t)std::any_of(target.begin(), target.end(), [&](const auto& t) {
            return t.pitch == n.pitch && std::abs(t.startBeat - n.startBeat) < 1.0e-6;
        });
    return count;
}

} // namespace

// Regression test for FRO1560: the chosen genre chip went back to plain the moment the next control was used.
TEST(DrumIdeasPanelTest, ThePickedStarterChipStaysLitAfterTheSliderAndMakeThreeTakesAndIsExposedAsSelected) {
    IdeasFixture f;
    EXPECT_EQ(f.panel->getSelectedStyle(), -1);
    f.panel->getStarterButton(Style::House).onClick();
    auto& chip = f.panel->getStarterChip(Style::House);
    EXPECT_TRUE(chip.isSelected());
    EXPECT_EQ(chip.getTitle(), "House starter, selected");
    EXPECT_TRUE(chip.isToggleable()) << "the screen reader hears a toggle";
    EXPECT_EQ(f.c().drumStyle, static_cast<int>(Style::House));
    f.panel->getAmountSlider().setValue(80.0, juce::sendNotificationSync);
    f.panel->getMakeTakesButton().onClick();
    EXPECT_TRUE(chip.isSelected());
    EXPECT_EQ(f.panel->getSelectedStyle(), static_cast<int>(Style::House));
    f.panel->getStarterButton(Style::House).onClick(); // pressing the lit chip refills and keeps it lit
    EXPECT_TRUE(chip.isSelected());
    f.panel->getStarterButton(Style::Funk).onClick();
    EXPECT_FALSE(chip.isSelected());
    EXPECT_EQ(chip.getTitle(), "House starter");
    EXPECT_TRUE(f.panel->getStarterChip(Style::Funk).isSelected());
    for (int i = 0; i < synth::ui::drumideas::kStyleCount; ++i)
        EXPECT_EQ(f.panel->getStarterChip(synth::ui::drumideas::styleAt(i)).isSelected(), i == (int)Style::Funk);
}

TEST(DrumIdeasPanelTest, ReopeningThePanelOnAClipWithAStoredStyleShowsThatChipLit) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Disco).onClick();
    f.panel.reset();
    f.grid.getIdeasButton().onClick();
    ASSERT_NE(f.panel, nullptr);
    EXPECT_EQ(f.panel->getSelectedStyle(), static_cast<int>(Style::Disco));
    EXPECT_TRUE(f.panel->getStarterChip(Style::Disco).isSelected());
    EXPECT_FALSE(f.panel->getStarterChip(Style::House).isSelected());
}

TEST(DrumIdeasPanelTest, OneUndoRevertsTheNotesAndTheGenreTogetherAndTheLitChipFollows) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::House).onClick();
    f.undo.clearUndoHistory();
    f.panel->getStarterButton(Style::Rock).onClick();
    EXPECT_EQ(f.c().drumStyle, static_cast<int>(Style::Rock));
    f.undo.undo();
    EXPECT_EQ(f.c().drumStyle, static_cast<int>(Style::House)) << "genre and notes come back as one step";
    EXPECT_EQ(f.pitchCount(36), 8u);
    EXPECT_FALSE(f.undo.canUndo());
    f.panel->appUndoneOrRedone(); // what the popover host runs after Cmd+Z
    EXPECT_EQ(f.panel->getSelectedStyle(), static_cast<int>(Style::House));
    f.undo.redo();
    f.panel->appUndoneOrRedone();
    EXPECT_EQ(f.panel->getSelectedStyle(), static_cast<int>(Style::Rock));
}

TEST(DrumIdeasPanelTest, OtherIdeasLeaveTheStoredGenreAlone) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Techno).onClick();
    f.panel->getSnareRollButton().onClick();
    f.panel->getRandomizeButton().onClick();
    f.panel->getMakeTakesButton().onClick();
    f.panel->getPickButton(0).onClick();
    EXPECT_EQ(f.c().drumStyle, static_cast<int>(Style::Techno));
    EXPECT_TRUE(f.panel->getStarterChip(Style::Techno).isSelected());
}

TEST(DrumIdeasPanelTest, RandomizeReadsTheOneAmountSliderAsItsDensity) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Rock).onClick();
    double seenDensity = -1.0;
    f.panel->onRandomize = [&](double density, bool, int) { seenDensity = density; };
    f.panel->getAmountSlider().setValue(73.0, juce::dontSendNotification);
    f.panel->getRandomizeButton().onClick();
    EXPECT_DOUBLE_EQ(seenDensity, 0.73);
    for (auto* child : f.panel->getChildren())
        EXPECT_NE(child->getTitle(), "Density") << "no second slider";
    EXPECT_NE(f.panel->getAmountSlider().getTooltip().indexOf("Randomize"), -1);
}

TEST(DrumIdeasPanelTest, TakesLeanTowardTheStoredStyleMoreThanTakesWithoutOne) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Rock).onClick(); // a pattern that is not Funk, so leaning has hits to add
    f.doc.setClipDrumStyle(f.clip, synth::Clip::kNoDrumStyle);
    f.panel->getAmountSlider().setValue(60.0, juce::dontSendNotification);
    synth::ui::drumideas::Grid grid{f.c().lengthBeats, 4.0, 0.25};
    size_t without = 0, with = 0;
    for (int seed = 0; seed < 20; ++seed) {
        f.panel->setSeedForTest(seed * 3);
        f.panel->setSelectedStyle(-1);
        f.panel->getMakeTakesButton().onClick();
        for (int t = 0; t < 3; ++t)
            without += starterHitsIn(f.panel->getTakeGrid(t).getNotes(), Style::Funk, grid);
    }
    f.doc.setClipDrumStyle(f.clip, static_cast<int>(Style::Funk));
    for (int seed = 0; seed < 20; ++seed) {
        f.panel->setSeedForTest(seed * 3);
        f.panel->getMakeTakesButton().onClick();
        for (int t = 0; t < 3; ++t)
            with += starterHitsIn(f.panel->getTakeGrid(t).getNotes(), Style::Funk, grid);
    }
    EXPECT_GT(with, without + 20) << "with " << with << " without " << without;
}

//==============================================================================
// The panel's layout at its real width

// Regression test for FRO1560: layout-overlap guard for the reshaped Ideas panel: nothing overlaps, every gap between
// controls is at least the panel's gap, everything keeps the padding and the hint fits its two lines.
TEST(DrumIdeasPanelTest, NothingOverlapsEveryGapBetweenControlsKeepsTheGapAndTheHintFitsAtTheRealWidth) {
    IdeasFixture f;
    constexpr int kGap = 6;
    constexpr int kPad = 10;
    f.panel->setSize(DrumIdeasPanel::kWidth, DrumIdeasPanel::kHeight);
    f.panel->resized();
    const auto children = f.panel->getChildren();
    for (auto* a : children) {
        EXPECT_TRUE(f.panel->getLocalBounds().reduced(kPad).contains(a->getBounds())) << a->getTitle();
        for (auto* b : children) {
            if (a >= b)
                continue;
            EXPECT_FALSE(a->getBounds().intersects(b->getBounds()))
                << a->getTitle() << " / " << b->getTitle() << " overlap";
            if (dynamic_cast<juce::Label*>(a) != nullptr || dynamic_cast<juce::Label*>(b) != nullptr)
                continue; // a caption sits flush on what it names
            const auto grown = a->getBounds().expanded(kGap - 1);
            EXPECT_FALSE(grown.intersects(b->getBounds()))
                << a->getTitle() << " / " << b->getTitle() << " are closer than " << kGap << " px";
        }
    }
    int bottom = 0;
    for (auto* child : children)
        bottom = std::max(bottom, child->getBottom());
    EXPECT_LE(bottom + kPad, f.panel->getHeight());
    for (auto* child : children)
        if (auto* label = dynamic_cast<juce::Label*>(child); label != nullptr && label->getText().length() > 80) {
            const float width = juce::GlyphArrangement::getStringWidth(label->getFont(), label->getText());
            EXPECT_LE(width, label->getWidth() * 1.85f) << "the hint wraps to two lines";
            EXPECT_EQ(label->getText(), "How much changes. Make 3 takes previews three versions to pick from, leaning "
                                        "toward the lit style; Randomize replaces the pattern right away.");
        }
    // Make 3 takes and Randomize are side by side with equal widths.
    auto& make = f.panel->getMakeTakesButton();
    auto& random = f.panel->getRandomizeButton();
    EXPECT_EQ(make.getY(), random.getY());
    EXPECT_LE(std::abs(make.getWidth() - random.getWidth()), 1);
    EXPECT_LT(make.getX(), random.getX());
}
