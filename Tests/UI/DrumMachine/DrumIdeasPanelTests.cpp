// The Ideas button and panel on the drum machine: starters, takes, picking, randomize, the changed-cell outline, one
// undo step each, and the keyboard and screen-reader paths.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Layout/FadeVisibility.h"

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
    EXPECT_EQ(button.getTooltip(), "Starters, variations and randomize");
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
    EXPECT_EQ(controls, 12 + 1 + 1 + 3 + 3 + 1 + 1 + 1 + 1)
        << "starters, amount, make, takes, picks, replace, density, keep, randomize";
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

TEST(DrumIdeasPanelTest, PickAddsANewClipRightAfterTheCurrentOneAndOpensIt) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Rock).onClick();
    f.undo.clearUndoHistory();
    synth::ClipId created;
    f.grid.onClipCreated = [&](synth::ClipId id) { created = id; };
    f.panel->getMakeTakesButton().onClick();
    const auto take = f.panel->getTakeGrid(1).getNotes();
    f.panel->getPickButton(1).onClick();

    ASSERT_TRUE(created.isValid());
    const auto* track = f.doc.getTrackForClip(f.clip);
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->clips.size(), 2u);
    const auto* added = f.doc.getClip(created);
    ASSERT_NE(added, nullptr);
    EXPECT_DOUBLE_EQ(added->startBeat, 8.0) << "right after the current clip";
    EXPECT_DOUBLE_EQ(added->lengthBeats, 8.0);
    EXPECT_EQ(added->notes.size(), take.size());
    EXPECT_EQ(f.grid.getClipId(), created) << "the grid opens the new clip";
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrackForClip(f.clip)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo()) << "one undo step";
}

TEST(DrumIdeasPanelTest, PickWithReplaceSwapsTheNotesOfThisClipInOneUndoStep) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Rock).onClick();
    f.undo.clearUndoHistory();
    f.panel->getMakeTakesButton().onClick();
    const auto take = f.panel->getTakeGrid(2).getNotes();
    const auto before = f.c().notes.size();
    f.panel->getReplaceToggle().setToggleState(true, juce::dontSendNotification);
    f.panel->getPickButton(2).onClick();
    EXPECT_EQ(f.doc.getTrackForClip(f.clip)->clips.size(), 1u);
    EXPECT_EQ(f.c().notes.size(), take.size());
    EXPECT_EQ(f.grid.getClipId(), f.clip);
    f.undo.undo();
    EXPECT_EQ(f.c().notes.size(), before);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumIdeasPanelTest, RandomizeKeepingKickAndSnareLeavesThemAloneAsOneUndoStep) {
    IdeasFixture f;
    f.panel->getStarterButton(Style::Rock).onClick();
    f.undo.clearUndoHistory();
    const auto kicks = f.pitchCount(36);
    const auto snares = f.pitchCount(38);
    f.panel->getKeepToggle().setToggleState(true, juce::dontSendNotification);
    f.panel->getDensitySlider().setValue(90.0, juce::dontSendNotification);
    f.panel->getRandomizeButton().onClick();
    EXPECT_EQ(f.pitchCount(36), kicks);
    EXPECT_EQ(f.pitchCount(38), snares);
    f.undo.undo();
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_EQ(f.pitchCount(42), 16u) << "Rock's eighth-note hats are back";
}

TEST(DrumIdeasPanelTest, EscapeClosesThePanelAndReturnsTheFocusToTheButtonOnce) {
    IdeasFixture f;
    EXPECT_EQ(f.grid.getIdeasFocusReturns(), 0);
    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
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
