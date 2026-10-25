// StepGridCardTests.cpp -- the Sequencer and Poly Sequencer cards as the user meets them: the grid built by the card
// body, a real mouse stroke is one undo step, a Pitch drag lands on scale notes, a lane chip changes the card's height
// and the saved card view, the page strip follows the playhead, and the keyboard reaches every edit.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/CardViewState.h"
#include "Modules/Sequencer/SequencerModuleBase.h"
#include "StepGridCardFixture.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/ModuleViews/StepGrid/StepGridComponent.h"

using namespace cardbody_test;
using synth::SequencerLane;
using synth::ui::StepGridComponent;

using stepgrid_test::SeqCard;

TEST(StepGridCard, BothSequencersBuildTheGridAndKeepTheirPlace) {
    for (const char* type : {"Sequencer", "Poly Sequencer"}) {
        SCOPED_TRACE(type);
        SeqCard c(type);
        ASSERT_NE(c.card, nullptr);
        ASSERT_NE(c.grid, nullptr);
        EXPECT_EQ(c.card->getPosition(), juce::Point<int>(40, 40));
        EXPECT_EQ(c.card->getWidth(), 560);
        EXPECT_TRUE(c.card->getLocalBounds().contains(c.grid->getBounds()));
        EXPECT_EQ(c.grid->getHeight(), StepGridComponent::preferredHeightFor(*c.module));
    }
}

TEST(StepGridCard, ASavedCardWithLanesOpensAtTheRightHeightAndNothingMoves) {
    SeqCard fresh;
    const int defaultHeight = fresh.card->getHeight();

    SeqCard saved("Sequencer", [](synth::SequencerModuleBase& m) {
        synth::CardViewState state;
        state.stepGridLanes = synth::kAllSequencerLanes;
        m.setCardViewState(state);
    });
    ASSERT_NE(saved.grid, nullptr);
    EXPECT_EQ(saved.card->getPosition(), juce::Point<int>(40, 40));
    EXPECT_EQ(saved.grid->getLanes(), synth::kAllSequencerLanes);
    EXPECT_EQ(saved.card->getHeight() - defaultHeight,
              synth::ui::stepgrid::viewHeightFor(synth::kAllSequencerLanes, false) -
                  synth::ui::stepgrid::viewHeightFor(synth::kDefaultSequencerLanes, false));
}

TEST(StepGridCard, ADefaultCardIsTheSizeTheStaticMeasureSays) {
    for (const char* type : {"Sequencer", "Poly Sequencer"}) {
        SCOPED_TRACE(type);
        SeqCard c(type);
        const auto estimate = synth::measureDataDrivenCardSize(type);
        ASSERT_TRUE(estimate.has_value());
        EXPECT_EQ(c.card->getWidth(), estimate->x);
        EXPECT_EQ(c.card->getHeight(), estimate->y);
    }
}

// Regression test for the card rebuild: one stroke across a bar lane is one undo step, however many steps it crosses.
TEST(StepGridCard, ADragAcrossABarLaneIsExactlyOneUndoStep) {
    SeqCard c;
    ASSERT_NE(c.grid, nullptr);
    const auto before = c.module->getPattern();
    c.mouse("down", c.pointIn(SequencerLane::Velocity, 0, 0.2f));
    for (int step = 1; step < 8; ++step)
        c.mouse("drag", c.pointIn(SequencerLane::Velocity, step, 0.2f + 0.05f * (float)step));
    c.mouse("up", c.pointIn(SequencerLane::Velocity, 7, 0.55f));

    const auto edited = c.module->getPattern();
    EXPECT_NE(edited.steps[0].velocity, before.steps[0].velocity);
    EXPECT_NE(edited.steps[7].velocity, before.steps[7].velocity);
    EXPECT_TRUE(c.canvas.undo.canUndo());
    c.canvas.undo.getUndoManager().undo();
    EXPECT_FALSE(c.canvas.undo.canUndo()) << "the stroke was more than one undo step";
    for (int step = 0; step < 8; ++step)
        EXPECT_EQ(c.module->getPattern().steps[(size_t)step].velocity, before.steps[(size_t)step].velocity);
}

TEST(StepGridCard, AStrokeAcrossThePadsPaintsTheSameStateAndIsOneUndoStep) {
    SeqCard c;
    const bool startedOn = c.module->getPattern().steps[2].on;
    const auto padPoint = [&](int step) {
        const auto pad = c.grid->padBounds(step);
        return juce::Point<float>((float)pad.getCentreX(), (float)pad.getCentreY());
    };
    c.mouse("down", padPoint(2));
    for (int step = 3; step <= 5; ++step)
        c.mouse("drag", padPoint(step));
    c.mouse("up", padPoint(5));
    for (int step = 2; step <= 5; ++step)
        EXPECT_EQ(c.module->getPattern().steps[(size_t)step].on, !startedOn) << step;
    c.canvas.undo.getUndoManager().undo();
    EXPECT_FALSE(c.canvas.undo.canUndo());
    EXPECT_EQ(c.module->getPattern().steps[2].on, startedOn);
}

TEST(StepGridCard, APitchDragLandsOnlyOnScaleNotes) {
    SeqCard c;
    c.setChoice("scaleRoot", "C");
    c.useOwnKey();
    c.setChoice("scale", "Major");
    c.grid->poll();
    const auto major = synth::sequencerScale(0, synth::sequencerScaleChoices().indexOf("Major"), 0xFFF);
    ASSERT_FALSE(major.isChromatic());

    c.mouse("down", c.pointIn(SequencerLane::Pitch, 0, 0.5f));
    for (int i = 1; i <= 12; ++i)
        c.mouse("drag", c.pointIn(SequencerLane::Pitch, i % 8, (float)i / 13.0f));
    c.mouse("up", c.pointIn(SequencerLane::Pitch, 3, 0.9f));
    for (int step = 0; step < 8; ++step)
        EXPECT_TRUE(major.contains(c.grid->playedPitchOf(step))) << "step " << step;
}

TEST(StepGridCard, AChipShowsALaneChangesTheHeightAndIsSaved) {
    SeqCard c;
    const int heightBefore = c.card->getHeight();
    const int viewBefore = StepGridComponent::preferredHeightFor(*c.module);
    auto& chips = c.grid->getChipRow();
    int gateChip = -1;
    for (int i = 0; i < chips.getNumChips(); ++i)
        if (chips.getChip(i)->getTitle() == "Gate lane")
            gateChip = i;
    ASSERT_GE(gateChip, 0);
    chips.getChip(gateChip)->setToggleState(true, juce::sendNotificationSync); // what a click does
    c.grid->finishMotionForTest();

    EXPECT_NE(c.grid->getLanes() & synth::sequencerLaneBit(SequencerLane::Gate), 0);
    EXPECT_GT(StepGridComponent::preferredHeightFor(*c.module), viewBefore);
    EXPECT_GT(c.card->getHeight(), heightBefore);
    const auto saved = c.module->getCardViewState().toVar();
    ASSERT_TRUE(saved.isObject());
    EXPECT_TRUE(saved["lanes"].toString().contains("gate") || saved["lanes"].toString().isNotEmpty());
    const auto restored = synth::CardViewState::fromVar(saved);
    EXPECT_EQ(restored.stepGridLanes, c.grid->getLanes());

    // Turning it off again returns to the default, which saves nothing at all.
    chips.getChip(gateChip)->setToggleState(false, juce::sendNotificationSync);
    c.grid->finishMotionForTest();
    EXPECT_EQ(c.card->getHeight(), heightBefore);
    EXPECT_TRUE(c.module->getCardViewState().isDefault());
}

TEST(StepGridCard, ThePageStripAppearsPastSixteenStepsAndFollowTurnsPagesWithThePlayhead) {
    SeqCard c;
    EXPECT_EQ(c.grid->getPageStrip().getPageCount(), 1);
    auto* steps = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(c.module, "steps"));
    ASSERT_NE(steps, nullptr);
    *steps = 40;
    c.grid->poll();
    EXPECT_EQ(c.grid->getPageStrip().getPageCount(), 3);

    c.grid->setFollowing(true);
    auto* run = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(c.module, "run"));
    ASSERT_NE(run, nullptr);
    *run = true;
    c.module->currentActiveStep = 20;
    c.grid->poll();
    EXPECT_EQ(c.grid->getViewedPage(), 1);
    c.module->currentActiveStep = 35;
    c.grid->poll();
    EXPECT_EQ(c.grid->getViewedPage(), 2);

    c.grid->setFollowing(false);
    c.module->currentActiveStep = 3;
    c.grid->poll();
    EXPECT_EQ(c.grid->getViewedPage(), 2) << "without Follow the viewed page stays";
}

TEST(StepGridCard, TheKeyboardMovesFocusAndEditsWithOneUndoStepPerKey) {
    SeqCard c;
    ASSERT_EQ(c.grid->getFocusedStep(), 0);
    c.press(juce::KeyPress::rightKey);
    c.press(juce::KeyPress::rightKey);
    EXPECT_EQ(c.grid->getFocusedStep(), 2);
    c.press(juce::KeyPress::homeKey);
    EXPECT_EQ(c.grid->getFocusedStep(), 0);

    const int pitch = c.grid->playedPitchOf(0);
    c.press(juce::KeyPress::upKey);
    EXPECT_GT(c.grid->playedPitchOf(0), pitch);
    c.canvas.undo.getUndoManager().undo();
    EXPECT_FALSE(c.canvas.undo.canUndo());
    EXPECT_EQ(c.grid->playedPitchOf(0), pitch);

    const bool on = c.module->getPattern().steps[0].on;
    c.press(juce::KeyPress::spaceKey);
    EXPECT_EQ(c.module->getPattern().steps[0].on, !on);

    c.press(juce::KeyPress::pageDownKey);
    EXPECT_NE(c.grid->getFocusedLane(), SequencerLane::Pitch);
    EXPECT_FALSE(c.grid->describeFocus().isEmpty());
}

TEST(StepGridCard, AnExternalPatternChangeGlidesAndCannotBeMistakenForAnEdit) {
    SeqCard c;
    auto pattern = c.module->getPattern();
    pattern.steps[1].pitch = 72;
    c.module->setPattern(pattern);
    c.grid->poll();
    c.grid->finishMotionForTest();
    EXPECT_FALSE(c.grid->isMotionRunning());
    EXPECT_FALSE(c.canvas.undo.canUndo()) << "a change from outside is not the grid's undo step";
}

TEST(StepGridCard, ThePolyCardHasNoSlideLane) {
    SeqCard c("Poly Sequencer");
    c.grid->setLanes(synth::kAllSequencerLanes);
    c.grid->finishMotionForTest();
    EXPECT_EQ(c.grid->getLanes() & synth::sequencerLaneBit(SequencerLane::Slide), 0);
    EXPECT_TRUE(c.grid->cellBounds(SequencerLane::Slide, 0).isEmpty());
}
