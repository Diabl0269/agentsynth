// StepGridCardTests.cpp -- the Sequencer and Poly Sequencer cards as the user meets them: the grid built by the card
// body, a real mouse stroke is one undo step, a Pitch drag lands on scale notes, a lane chip changes the card's height
// and the saved card view, the page strip follows the playhead, and the keyboard reaches every edit.
#include "../../Theme/ThemeSweep/ThemeSweepSupport.h"
#include "../../Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/CardViewState.h"
#include "Modules/Sequencer/SequencerModuleBase.h"
#include "StepGridCardFixture.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
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
        EXPECT_EQ(c.card->getWidth(), 560) << "the double width, which the lanes fit";
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

// holding the Steps arrow changes Steps faster than the 120 ms shade slide ends. Every column keeps heading
// one way from the level it shows, so the shade edge glides instead of flashing the columns that were mid-fade.
TEST(StepGridCard, TheShadePastStepsRetargetsFromWhereItIsWhenStepsChangesAgainMidFade) {
    SeqCard c;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    auto* steps = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(c.module, "steps"));
    ASSERT_NE(steps, nullptr);
    *steps = 16;
    c.grid->poll();
    c.grid->finishMotionForTest();
    for (const int next : {4, 12, 6, 14}) {
        std::array<float, 16> before{};
        for (int s = 0; s < 16; ++s)
            before[(size_t)s] = c.grid->dimAmountForTest(s);
        *steps = next;
        c.grid->poll();
        for (int s = 0; s < 16; ++s)
            EXPECT_NEAR(c.grid->dimAmountForTest(s), before[(size_t)s], 1.0e-5f)
                << "column " << s << " jumped when Steps changed to " << next;
        c.grid->stepMotionForTest(30.0); // the next hold repeat arrives before the 120 ms slide has ended
        for (int s = 0; s < 16; ++s) {
            const float target = s >= next ? 1.0f : 0.0f;
            const float level = c.grid->dimAmountForTest(s);
            EXPECT_LE(std::abs(level - target), std::abs(before[(size_t)s] - target) + 1.0e-5f)
                << "column " << s << " moved away from its target " << target;
            EXPECT_GE(level, 0.0f);
            EXPECT_LE(level, 1.0f);
        }
    }
    c.grid->finishMotionForTest();
    for (int s = 0; s < 16; ++s)
        EXPECT_FLOAT_EQ(c.grid->dimAmountForTest(s), s >= 14 ? 1.0f : 0.0f);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

// Regression test for FRO1672: Steps only dims columns, so changing it must not move any bar up or down.
TEST(StepGridCard, ChangingStepsNeverMovesAnyBar) {
    for (const char* type : {"Sequencer", "Poly Sequencer"}) {
        SCOPED_TRACE(type);
        SeqCard c(type);
        ASSERT_NE(c.grid, nullptr);
        auto pattern = c.module->getPattern();
        for (int i = 0; i < synth::kMaxSequencerSteps; ++i)
            pattern.steps[(size_t)i].pitch = 60 + (i % 3);
        pattern.steps[10].pitch = 36;
        pattern.steps[15].pitch = 84;
        c.module->setPattern(pattern);
        c.card->timerCallback();
        c.grid->finishMotionForTest();

        std::vector<juce::Rectangle<float>> before;
        for (int i = 0; i < synth::kMaxSequencerSteps; ++i)
            before.push_back(c.grid->pitchBarBoundsForTest(i));
        ASSERT_FALSE(before[0].isEmpty());

        auto* steps = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(c.module, "steps"));
        ASSERT_NE(steps, nullptr);
        for (const int count : {1, 2, 16}) {
            *steps = count;
            c.card->timerCallback();
            c.grid->finishMotionForTest();
            for (int i = 0; i < synth::kMaxSequencerSteps; ++i)
                EXPECT_EQ(c.grid->pitchBarBoundsForTest(i), before[(size_t)i]) << "steps " << count << " bar " << i;
        }
    }
}

// ---- A click in the Pitch lane slides the note to the click; a drag still follows the pointer. ----
namespace {

/** Every step at C, step 10 low and 15 high so the pitch range stays put and only step 0's bar can move. */
struct ClickRig {
    SeqCard c;
    ClickRig() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true); // a headless grid gets no VBlank
        c.useOwnKey();
        c.setChoice("scale", "No scale");
        auto pattern = c.module->getPattern();
        for (int i = 0; i < synth::kMaxSequencerSteps; ++i)
            pattern.steps[(size_t)i].pitch = 60;
        pattern.steps[10].pitch = 36;
        pattern.steps[15].pitch = 84;
        c.module->setPattern(pattern);
        c.card->timerCallback();
        c.grid->finishMotionForTest();
        c.canvas.undo.getUndoManager().clearUndoHistory();
    }
    ~ClickRig() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
    }
    juce::Point<float> above(float dy) const {
        const auto bar = c.grid->pitchBarBoundsForTest(0);
        return {(float)c.grid->cellBounds(SequencerLane::Pitch, 0).getCentreX(), bar.getCentreY() - dy};
    }
};

} // namespace

TEST(StepGridCard, AClickInThePitchLaneSlidesTheNoteThereInsteadOfPopping) {
    ClickRig r;
    const float oldPitch = (float)r.c.grid->playedPitchOf(0);
    r.c.mouse("down", r.above(30.0f));
    const float newPitch = (float)r.c.grid->playedPitchOf(0);
    ASSERT_GT(newPitch, oldPitch);
    EXPECT_TRUE(r.c.grid->isMotionRunning()) << "the press starts a glide";
    r.c.grid->stepMotionForTest(40.0);
    EXPECT_GT(r.c.grid->drawnPitchForTest(0), oldPitch) << "the bar has left its old place";
    EXPECT_LT(r.c.grid->drawnPitchForTest(0), newPitch) << "and has not reached the click yet";
    r.c.mouse("up", r.above(30.0f));
    r.c.grid->finishMotionForTest();
    EXPECT_FLOAT_EQ(r.c.grid->drawnPitchForTest(0), newPitch);
    EXPECT_TRUE(r.c.canvas.undo.getUndoManager().canUndo());
    r.c.canvas.undo.undo();
    r.c.card->timerCallback();
    EXPECT_TRUE(r.c.grid->isMotionRunning()) << "undoing the click glides back, as before";
    r.c.grid->finishMotionForTest();
    EXPECT_FLOAT_EQ(r.c.grid->drawnPitchForTest(0), oldPitch);
}

TEST(StepGridCard, ADragBeyondThreePixelsCancelsThePressGlideAndFollowsThePointer) {
    ClickRig r;
    const auto start = r.above(30.0f);
    r.c.mouse("down", start);
    ASSERT_TRUE(r.c.grid->isMotionRunning());
    const int pressed = r.c.grid->playedPitchOf(0);
    r.c.mouse("drag", start + juce::Point<float>(0.0f, -2.0f));
    EXPECT_TRUE(r.c.grid->isMotionRunning()) << "within the slop the glide runs on";
    EXPECT_EQ(r.c.grid->playedPitchOf(0), pressed) << "and nothing is applied";
    r.c.mouse("drag", start + juce::Point<float>(0.0f, -20.0f));
    EXPECT_FALSE(r.c.grid->isMotionRunning()) << "past the slop the bar follows the pointer, no glide";
    EXPECT_GT(r.c.grid->playedPitchOf(0), pressed);
    EXPECT_FLOAT_EQ(r.c.grid->drawnPitchForTest(0), (float)r.c.grid->playedPitchOf(0));
    r.c.mouse("up", start + juce::Point<float>(0.0f, -20.0f));
}

TEST(StepGridCard, ReduceMotionShortensThePressGlideAndAnimationsOffLandsItAtOnce) {
    {
        ClickRig r;
        synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
        r.c.mouse("down", r.above(30.0f));
        ASSERT_TRUE(r.c.grid->isMotionRunning());
        r.c.grid->stepMotionForTest(100.0);
        EXPECT_FLOAT_EQ(r.c.grid->drawnPitchForTest(0), (float)r.c.grid->playedPitchOf(0)) << "done within 80 ms";
        r.c.mouse("up", r.above(30.0f));
    }
    {
        ClickRig r;
        synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
        r.c.mouse("down", r.above(30.0f));
        EXPECT_FALSE(r.c.grid->isMotionRunning());
        EXPECT_FLOAT_EQ(r.c.grid->drawnPitchForTest(0), (float)r.c.grid->playedPitchOf(0));
        r.c.mouse("up", r.above(30.0f));
    }
}

TEST(StepGridCard, ATogglePadPressStartsNoGlide) {
    ClickRig r;
    const auto pad = r.c.grid->padBounds(3).getCentre().toFloat();
    r.c.mouse("down", pad);
    EXPECT_FALSE(r.c.grid->isMotionRunning());
    r.c.mouse("up", pad);
}

namespace {

// A Sequencer with every step on, every lane shown and `steps` steps, drawn to an image with the grid's own size.
struct ShadeRender {
    SeqCard card;
    juce::Image image;
    explicit ShadeRender(int steps) {
        auto* param = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(card.module, "steps"));
        *param = steps;
        auto pattern = card.module->getPattern();
        for (auto& step : pattern.steps)
            step.on = true;
        card.module->setPattern(pattern);
        card.grid->setLanes(synth::kAllSequencerLanes);
        card.grid->poll();
        card.grid->finishMotionForTest();
        card.grid->setSize(card.grid->getWidth(), card.grid->currentLayoutHeight());
        image = synth::test::theme_sweep::renderToImage(*card.grid);
    }
    juce::Colour at(int x, int y) const { return image.getPixelAt(x, y); }
};

bool nearColour(juce::Colour a, juce::Colour b) {
    return std::abs((int)a.getRed() - (int)b.getRed()) + std::abs((int)a.getGreen() - (int)b.getGreen()) +
               std::abs((int)a.getBlue() - (int)b.getBlue()) <=
           30;
}

} // namespace

// Theme-sweep guard for the shade past Steps (painted by the grid, outside the control sweep): in every theme the bars
// of a step past Steps stay visible against their lane (1.5:1) but quieter than a playing step's, the pattern's end
// has a 2 px accent marker on the pads and the lanes, and nothing is drawn over the lane chip row.
TEST(StepGridCard, StepsPastTheEndDrawQuietBarsAndAMarkerAndNeverShadeTheChipRow) {
    juce::MessageManager::getInstance();
    for (const auto& theme : synth::test::theme_sweep::sweepThemes()) {
        synth::test::theme_sweep::SweepEnvironment env(theme);
        ShadeRender eight(8);
        ShadeRender sixteen(16);
        ASSERT_NE(eight.card.grid, nullptr);
        auto& grid = *eight.card.grid;
        synth::test::theme_sweep::writeSweepPng("step-grid-8-of-16", theme.id, eight.image);
        synth::test::theme_sweep::writeSweepPng("step-grid-16-of-16", theme.id, sixteen.image);
        const auto& c = theme.colors;

        for (const auto lane : {SequencerLane::Velocity, SequencerLane::Chance}) {
            const auto playing = grid.cellBounds(lane, 3);
            const auto past = grid.cellBounds(lane, 12);
            ASSERT_FALSE(past.isEmpty());
            const int y = playing.getBottom() - 4;
            const double activeRatio =
                synth::test::theme_sweep::contrastRatio(eight.at(playing.getCentreX(), y), eight.at(playing.getX(), y));
            const double pastRatio =
                synth::test::theme_sweep::contrastRatio(eight.at(past.getCentreX(), y), eight.at(past.getX(), y));
            EXPECT_GE(pastRatio, 1.5) << theme.id << " lane " << (int)lane << ": the unused bar is still visible";
            EXPECT_LT(pastRatio, activeRatio) << theme.id << " lane " << (int)lane << ": the unused bar is quieter";
        }
        const auto bar = grid.pitchBarBoundsForTest(12).toNearestInt();
        const auto pitchPast = grid.cellBounds(SequencerLane::Pitch, 12);
        EXPECT_GE(synth::test::theme_sweep::contrastRatio(eight.at(bar.getCentreX(), bar.getCentreY()),
                                                          eight.at(pitchPast.getX(), bar.getCentreY())),
                  1.5)
            << theme.id << ": the unused Pitch bar is still visible";

        // The pattern's end: 2 px of accent on the pitch lane and the pads, at the first unused column's left edge.
        const int edge = grid.cellBounds(SequencerLane::Pitch, 8).getX();
        const int padY = grid.padBounds(8).getCentreY();
        const int laneY = grid.cellBounds(SequencerLane::Pitch, 8).getY() + 1;
        for (const int y : {padY, laneY})
            for (const int x : {edge - 1, edge})
                EXPECT_TRUE(nearColour(eight.at(x, y), c.accent)) << theme.id << ": marker at " << x << "," << y;
        EXPECT_FALSE(nearColour(eight.at(edge + 4, laneY), c.accent)) << theme.id << ": the marker is 2 px wide";

        // The chip row is untouched by the shade: it draws the same with 8 steps and with 16.
        const auto chips = grid.getChipRow().getBounds();
        int differing = 0;
        for (int y = chips.getY(); y < chips.getBottom(); ++y)
            for (int x = chips.getX(); x < chips.getRight(); ++x)
                if (eight.at(x, y) != sixteen.at(x, y))
                    ++differing;
        EXPECT_EQ(differing, 0) << theme.id << ": the chip row changed with Steps";
    }
}

// The end-of-pattern line slides with the column shade instead of jumping to the new Steps.
namespace {
juce::AudioParameterInt* stepsParam(SeqCard& c) {
    return dynamic_cast<juce::AudioParameterInt*>(findParameterByID(c.module, "steps"));
}
} // namespace

TEST(StepGridCard, TheEndLineSlidesWithTheShadeWhenStepsChanges) {
    SeqCard c;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    auto* steps = stepsParam(c);
    ASSERT_NE(steps, nullptr);
    *steps = 12;
    c.grid->poll();
    c.grid->finishMotionForTest();
    const float x12 = c.grid->stepsMarkerXForTest();
    *steps = 8;
    c.grid->poll();
    EXPECT_NEAR(c.grid->stepsMarkerXForTest(), x12, 0.5f) << "the line must not jump when Steps changes";
    c.grid->stepMotionForTest(60.0); // half of the 120 ms slide
    const float mid = c.grid->stepsMarkerXForTest();
    c.grid->finishMotionForTest();
    const float x8 = c.grid->stepsMarkerXForTest();
    EXPECT_LT(x8, x12);
    EXPECT_GT(mid, x8 + 1.0f);
    EXPECT_LT(mid, x12 - 1.0f);
    const float shade = c.grid->dimAmountForTest(9); // restart to read the shade at the same moment
    EXPECT_FLOAT_EQ(shade, 1.0f);
    *steps = 12;
    c.grid->poll();
    c.grid->stepMotionForTest(60.0);
    const float boundary = c.grid->dimAmountForTest(10);
    EXPECT_GT(boundary, 0.0f);
    EXPECT_LT(boundary, 1.0f);
    c.grid->finishMotionForTest();
    EXPECT_NEAR(c.grid->stepsMarkerXForTest(), x12, 0.5f);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(StepGridCard, TheEndLineRetargetsFromWhereItIsWhenStepsChangesAgainMidSlide) {
    SeqCard c;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    auto* steps = stepsParam(c);
    *steps = 12;
    c.grid->poll();
    c.grid->finishMotionForTest();
    *steps = 4;
    c.grid->poll();
    c.grid->stepMotionForTest(40.0);
    const float before = c.grid->stepsMarkerXForTest();
    *steps = 14;
    c.grid->poll();
    EXPECT_NEAR(c.grid->stepsMarkerXForTest(), before, 0.5f) << "a second change must start from the drawn x";
    c.grid->stepMotionForTest(30.0);
    EXPECT_GT(c.grid->stepsMarkerXForTest(), before) << "the line turns toward the new Steps";
    c.grid->finishMotionForTest();
    c.grid->stepMotionForTest(0.0);
    EXPECT_EQ(c.grid->stepsMarkerCountForTest(), 1);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(StepGridCard, TheEndLineMovesAtOnceWhenThereIsNoMotion) {
    SeqCard c; // off-screen cards do not animate in tests: the no-motion path
    auto* steps = stepsParam(c);
    *steps = 12;
    c.grid->poll();
    c.grid->finishMotionForTest();
    const float x12 = c.grid->stepsMarkerXForTest();
    *steps = 8;
    c.grid->poll();
    EXPECT_LT(c.grid->stepsMarkerXForTest(), x12 - 1.0f);
    EXPECT_FALSE(c.grid->isMotionRunning());
}
