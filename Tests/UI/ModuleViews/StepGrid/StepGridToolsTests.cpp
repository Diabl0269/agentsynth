// StepGridToolsTests.cpp -- the pattern tools row on the card: every button is named and focusable, a click is one
// undo step that glides, Double sets Steps in the same step, Fit to scale is off when it would change nothing, the
// Randomize popover keeps its settings and each Roll is one undo step, and the page strip never runs under Follow.
#include "Modules/Sequencer/SequencerPatternOps.h"
#include "StepGridCardFixture.h"
#include "UI/ModuleViews/StepGrid/StepGridModel.h"

using stepgrid_test::SeqCard;
using Tool = synth::ui::StepGridTools::Tool;

namespace {

void setSteps(SeqCard& c, int steps) {
    auto* param = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(c.module, "steps"));
    ASSERT_NE(param, nullptr);
    *param = steps;
    c.grid->poll();
}

int stepsOf(SeqCard& c) { return dynamic_cast<juce::AudioParameterInt*>(findParameterByID(c.module, "steps"))->get(); }

void click(SeqCard& c, Tool tool) { c.grid->getTools().getButton(tool).onClick(); }

} // namespace

TEST(StepGridTools, EveryButtonHasTheBoardsTooltipAsItsNameAndTakesFocus) {
    SeqCard c;
    const std::pair<Tool, const char*> expected[] = {
        {Tool::Randomize, "Randomize notes in the scale"},
        {Tool::ShiftLeft, "Shift pattern one step left"},
        {Tool::ShiftRight, "Shift pattern one step right"},
        {Tool::Reverse, "Reverse pattern"},
        {Tool::Double, "Copy the steps into a pattern twice as long"},
        {Tool::Up, "Move every note up one scale note"},
        {Tool::Down, "Move every note down one scale note"},
        {Tool::FitToScale, "Rewrite the notes into the scale"},
    };
    for (const auto& [tool, text] : expected) {
        auto& button = c.grid->getTools().getButton(tool);
        EXPECT_EQ(button.getTooltip(), text);
        EXPECT_EQ(button.getTitle(), text);
        if (button.isEnabled())
            EXPECT_TRUE(button.getWantsKeyboardFocus());
        EXPECT_EQ(button.getHeight(), synth::ui::StepGridTools::kButtonHeight);
    }
}

TEST(StepGridTools, TheRowIsItsOwn28PixelRowAtTheTopWithTheButtonsOnTheRight) {
    SeqCard c;
    const auto& tools = c.grid->getTools();
    EXPECT_EQ(tools.getY(), 0);
    EXPECT_EQ(tools.getHeight(), 28);
    EXPECT_GE(c.grid->getPageStrip().getY(), 28);
    EXPECT_GT(tools.getButton(Tool::Randomize).getX(), tools.getWidth() / 5);
}

TEST(StepGridTools, ReverseIsOneUndoStepThatUndoesCleanly) {
    SeqCard c;
    const auto before = c.module->getPattern();
    click(c, Tool::Reverse);
    EXPECT_EQ(c.module->getPattern().steps[0], before.steps[7]);
    EXPECT_TRUE(c.canvas.undo.canUndo());
    c.canvas.undo.getUndoManager().undo();
    EXPECT_FALSE(c.canvas.undo.canUndo());
    EXPECT_EQ(c.module->getPattern().steps[0], before.steps[0]);
}

TEST(StepGridTools, ShiftsRotateAndEachClickIsOneUndoStep) {
    SeqCard c;
    const auto before = c.module->getPattern();
    click(c, Tool::ShiftLeft);
    EXPECT_EQ(c.module->getPattern().steps[0], before.steps[1]);
    click(c, Tool::ShiftRight);
    EXPECT_EQ(c.module->getPattern().steps[0], before.steps[0]);
    c.canvas.undo.getUndoManager().undo();
    EXPECT_EQ(c.module->getPattern().steps[0], before.steps[1]);
    c.canvas.undo.getUndoManager().undo();
    EXPECT_FALSE(c.canvas.undo.canUndo());
}

TEST(StepGridTools, DoubleCopiesTheStepsAndSetsStepsInTheSameUndoStep) {
    SeqCard c;
    c.module->setPattern([&] {
        auto p = c.module->getPattern();
        for (int i = 0; i < 8; ++i)
            p.steps[(size_t)(8 + i)].pitch = 90; // not a copy yet
        return p;
    }());
    c.canvas.undo.getUndoManager().clearUndoHistory();
    const auto before = c.module->getPattern();
    ASSERT_EQ(stepsOf(c), 8);

    click(c, Tool::Double);
    EXPECT_EQ(stepsOf(c), 16);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(c.module->getPattern().steps[(size_t)(8 + i)], before.steps[(size_t)i]);

    c.canvas.undo.getUndoManager().undo();
    EXPECT_EQ(stepsOf(c), 8) << "one undo gives Steps back";
    EXPECT_EQ(c.module->getPattern().steps[8].pitch, 90) << "and the pattern";
    EXPECT_FALSE(c.canvas.undo.canUndo()) << "and that was the whole step";
}

TEST(StepGridTools, DoubleOfAPatternThatAlreadyRepeatsStillUndoesStepsInOneStep) {
    SeqCard c; // the default pattern repeats every 8 steps, so only Steps changes
    ASSERT_EQ(stepsOf(c), 8);
    click(c, Tool::Double);
    EXPECT_EQ(stepsOf(c), 16);
    c.canvas.undo.getUndoManager().undo();
    EXPECT_EQ(stepsOf(c), 8);
    EXPECT_FALSE(c.canvas.undo.canUndo());
}

TEST(StepGridTools, DoubleIsOffPastSixtyFourSteps) {
    SeqCard c;
    auto& button = c.grid->getTools().getButton(Tool::Double);
    EXPECT_TRUE(button.isEnabled());
    setSteps(c, 65);
    EXPECT_FALSE(button.isEnabled());
    setSteps(c, 64);
    EXPECT_TRUE(button.isEnabled());
}

TEST(StepGridTools, FitToScaleIsOffWithoutAScaleAndWhenNothingWouldChange) {
    SeqCard c;
    auto& fit = c.grid->getTools().getButton(Tool::FitToScale);
    EXPECT_FALSE(fit.isEnabled()) << "No scale";
    c.setChoice("scaleRoot", "C");
    c.setChoice("scale", "Major");
    c.grid->poll();
    EXPECT_TRUE(fit.isEnabled()) << "the default notes include out-of-scale ones";

    const auto before = c.module->getPattern();
    click(c, Tool::FitToScale);
    for (int i = 0; i < 8; ++i)
        EXPECT_TRUE(synth::makeScale(0, 0).contains(c.module->getPattern().steps[(size_t)i].pitch)) << i;
    EXPECT_FALSE(fit.isEnabled()) << "nothing left to rewrite";
    c.canvas.undo.getUndoManager().undo();
    EXPECT_EQ(c.module->getPattern().steps[1].pitch, before.steps[1].pitch);
    EXPECT_FALSE(c.canvas.undo.canUndo());
}

TEST(StepGridTools, UpAndDownMoveEveryNoteOneScaleNote) {
    SeqCard c;
    c.setChoice("scaleRoot", "C");
    c.setChoice("scale", "Major");
    c.grid->poll();
    click(c, Tool::Up);
    for (int i = 0; i < 8; ++i)
        EXPECT_TRUE(synth::makeScale(0, 0).contains(c.module->getPattern().steps[(size_t)i].pitch)) << i;
    EXPECT_TRUE(c.canvas.undo.canUndo());
    click(c, Tool::Down);
    c.canvas.undo.getUndoManager().undo();
    c.canvas.undo.getUndoManager().undo();
    EXPECT_FALSE(c.canvas.undo.canUndo());
}

TEST(StepGridTools, RandomizeOpensAPopoverWhoseRollsAreEachOneUndoStep) {
    SeqCard c;
    c.setChoice("scaleRoot", "C");
    c.setChoice("scale", "Major");
    c.grid->poll();
    std::unique_ptr<synth::ui::StepGridRandomizePanel> panel;
    synth::ui::test_hooks::randomizePanelHookForTest() = [&](auto p) { panel = std::move(p); };
    click(c, Tool::Randomize);
    synth::ui::test_hooks::randomizePanelHookForTest() = nullptr;
    ASSERT_NE(panel, nullptr);

    EXPECT_EQ(panel->getWidth(), 250);
    EXPECT_EQ(panel->getDensitySlider().getValue(), 75.0);
    EXPECT_TRUE(panel->getKeepRhythm().getToggleState());
    EXPECT_EQ(panel->getRangePicker().getLow(), 48) << "C2 in the grid's numbering";
    EXPECT_EQ(panel->getRangePicker().getHigh(), 72);

    const auto before = c.module->getPattern();
    juce::Random::getSystemRandom().setSeed(7);
    panel->getRollButton().onClick();
    const auto first = c.module->getPattern();
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(synth::makeScale(0, 0).contains(first.steps[(size_t)i].pitch)) << i;
        EXPECT_EQ(first.steps[(size_t)i].on, before.steps[(size_t)i].on) << "Keep my rhythm";
    }
    panel->getRollButton().onClick();
    EXPECT_TRUE(c.canvas.undo.canUndo());
    c.canvas.undo.getUndoManager().undo();
    EXPECT_EQ(c.module->getPattern().steps, first.steps) << "one roll undone";
    c.canvas.undo.getUndoManager().undo();
    EXPECT_EQ(c.module->getPattern().steps, before.steps);
    EXPECT_FALSE(c.canvas.undo.canUndo());
}

TEST(StepGridTools, TheRandomizeSettingsStayForTheSessionAndEscapeIsHandled) {
    SeqCard c;
    std::unique_ptr<synth::ui::StepGridRandomizePanel> panel;
    synth::ui::test_hooks::randomizePanelHookForTest() = [&](auto p) { panel = std::move(p); };
    click(c, Tool::Randomize);
    ASSERT_NE(panel, nullptr);
    panel->getKeepRhythm().setToggleState(false, juce::dontSendNotification);
    panel->getKeepRhythm().onClick();
    panel->getDensitySlider().setValue(30.0, juce::sendNotificationSync);
    panel->getRangePicker()
        .getStop(synth::ui::NoteRangePicker::Part::HighEdge)
        .keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    const int high = panel->getRangePicker().getHigh();
    panel.reset();

    click(c, Tool::Randomize);
    synth::ui::test_hooks::randomizePanelHookForTest() = nullptr;
    ASSERT_NE(panel, nullptr);
    EXPECT_FALSE(panel->getKeepRhythm().getToggleState());
    EXPECT_NEAR(panel->getDensitySlider().getValue(), 30.0, 0.5);
    EXPECT_EQ(panel->getRangePicker().getHigh(), high);
    EXPECT_TRUE(panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) == false)
        << "headless: no callout to close, so the key is not claimed";
}

TEST(StepGridTools, TheTallerCardKeepsItsMeasureAndThePageStripSitsBelowTheRow) {
    for (const char* type : {"Sequencer", "Poly Sequencer"}) {
        SCOPED_TRACE(type);
        SeqCard c(type);
        EXPECT_EQ(c.grid->getHeight(), StepGridComponent::preferredHeightFor(*c.module));
        EXPECT_EQ(synth::ui::stepgrid::fixedHeight(false), 28 + 20 + 4 + 18 + 4 + 22);
    }
}

TEST(StepGridPageStrip, AtOneHundredTwentyEightStepsTheLastPageBarEndsBeforeTheFollowPill) {
    SeqCard c;
    setSteps(c, 128);
    auto& strip = c.grid->getPageStrip();
    ASSERT_EQ(strip.getPageCount(), 8);
    const auto last = strip.pageBounds(7);
    EXPECT_LT(last.getRight(), strip.getFollowPill().getX());
    EXPECT_GE(strip.pageBounds(0).getX(), synth::ui::stepgrid::kGutterWidth);
    for (int page = 1; page < 8; ++page)
        EXPECT_GT(strip.pageBounds(page).getX(), strip.pageBounds(page - 1).getRight());
}
