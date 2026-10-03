// The mod dot's panel, sources page, driven by real events: a click or Return opens it under the dot, its rows
// name the sources as the user patched them, a row's bar and typed amount edit the amount (one undo step each),
// the row selects what a later dot drag edits, and remove / show in timeline go through the controller's hooks.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotTestFixture.h"

#include "UI/Graph/ModDot/ModDotAmountBar.h"
#include "UI/Layout/ReducedMotion.h"

namespace {

using synth::ui::ModDotAmountBar;
using synth::ui::ModDotPopover;
using synth::ui::ModDotSourceRow;

// Under the OS's reduced-motion setting every tween lands at once, so a test reads the end state.
struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

juce::MouseEvent clickAt(juce::Component& c, juce::Point<int> p) { return makeModuleClickWithMods(c, p, kPlain); }

void dragBar(ModDotAmountBar& bar, float from, float to) {
    bar.mouseDown(clickAt(bar, {(int)bar.xForValue(from), 5}));
    bar.mouseDrag(clickAt(bar, {(int)bar.xForValue(to), 5}));
    bar.mouseUp(clickAt(bar, {(int)bar.xForValue(to), 5}));
}

} // namespace

TEST_F(ModuleComponentTest, AClickOnTheDotOpensThePanelListingTheKnobsSourcesByName) {
    NoMotion motion;
    Fixture f;
    auto* panel = f.clickDot();
    ASSERT_NE(panel, nullptr) << "a press that moves under 3 px opens the panel";

    auto& page = panel->sourcesPage();
    ASSERT_EQ(page.rowCount(), 1);
    const auto sources = synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel);
    EXPECT_EQ(page.rowAt(0)->source().sourceName, sources[0].sourceName);
    EXPECT_EQ(page.titleText(), juce::String("Gain") + juce::String::fromUTF8(" \xC2\xB7 modulation"));
    EXPECT_TRUE(page.rowAt(0)->isSelected()) << "the source a drag would edit starts selected";
    EXPECT_TRUE(f.vcaCard->getModDotButton(f.gainChannel)->isMenuOpen()) << "the dot carries the accent ring";
    EXPECT_FALSE(f.undo.canUndo()) << "opening it edits nothing";

    f.held.reset();
    EXPECT_FALSE(f.vcaCard->getModDotButton(f.gainChannel)->isMenuOpen());
}

TEST_F(ModuleComponentTest, ReturnOnTheDotButtonOpensThePanel) {
    NoMotion motion;
    Fixture f;
    auto* button = f.vcaCard->getModDotButton(f.gainChannel);
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_NE(f.popover(), nullptr);
    EXPECT_EQ(f.popover()->sourcesPage().rowCount(), 1);
}

TEST_F(ModuleComponentTest, EachRowCarriesItsAccessibleNamesAndTooltips) {
    NoMotion motion;
    Fixture f;
    auto& row = *f.clickDot()->sourcesPage().rowAt(0);
    const auto name = row.source().sourceName;
    EXPECT_EQ(row.bar().getTitle(), name + " amount");
    EXPECT_EQ(row.bar().getTooltip(), "Drag to change how strongly " + name + " moves Gain");
    EXPECT_EQ(row.bar().getValueText(), "+50%");
    EXPECT_EQ(row.timelineButton().getTitle(), "Show " + name + " in timeline");
    EXPECT_EQ(row.removeButton().getTitle(), "Remove " + name);
    EXPECT_EQ(row.removeButton().getTooltip(), "Remove " + name);
    EXPECT_EQ(f.popover()->sourcesPage().addButton().getTitle(), "Add source");
}

TEST_F(ModuleComponentTest, ClickingARowSelectsItAndALaterDotDragEditsThatSource) {
    NoMotion motion;
    Fixture f;
    const auto second = f.addSecondLfo(); // -25%
    auto* panel = f.clickDot();
    ASSERT_NE(panel, nullptr);
    auto& page = panel->sourcesPage();
    ASSERT_EQ(page.rowCount(), 2);
    EXPECT_TRUE(page.rowFor(f.attenId)->isSelected());

    page.rowFor(second)->mouseDown(clickAt(*page.rowFor(second), {4, 4}));

    EXPECT_EQ(f.editor->getModDot().chosenAttenuverter(f.vcaId, f.gainChannel), second);
    EXPECT_TRUE(page.rowFor(second)->isSelected());
    EXPECT_FALSE(page.rowFor(f.attenId)->isSelected());
    const auto dot = f.dotInKnob();
    f.pressDragRelease(dot, dot + juce::Point<int>(0, -10)); // 10 px up: +10%
    EXPECT_NEAR(f.amount(second), -0.25f + 0.10f, 0.005f);
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);
}

TEST_F(ModuleComponentTest, DraggingARowsBarChangesTheAmountLiveAsOneUndoStep) {
    NoMotion motion;
    Fixture f;
    auto& row = *f.clickDot()->sourcesPage().rowAt(0);
    auto& bar = row.bar();
    ASSERT_GT(bar.getWidth(), 20);

    bar.mouseDown(clickAt(bar, {(int)bar.xForValue(0.5f), 5}));
    bar.mouseDrag(clickAt(bar, {(int)bar.xForValue(0.8f), 5}));
    EXPECT_NEAR(f.amount(f.attenId), 0.8f, 0.011f) << "the amount follows the handle while it is held";
    EXPECT_EQ(row.amountButton().getButtonText(), ModDotAmountBar::percentText(bar.getValue()));
    bar.mouseUp(clickAt(bar, {(int)bar.xForValue(0.8f), 5}));

    ASSERT_TRUE(f.undo.undo());
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole drag is one undo step";
}

TEST_F(ModuleComponentTest, ABarPressThatChangesNothingLeavesNoUndoStep) {
    NoMotion motion;
    Fixture f;
    auto& bar = f.clickDot()->sourcesPage().rowAt(0)->bar();
    dragBar(bar, 0.5f, 0.5f);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST_F(ModuleComponentTest, LeftAndRightStepTheBarByOnePercentAndShiftByTenEachAnUndoStep) {
    NoMotion motion;
    Fixture f;
    auto& bar = f.clickDot()->sourcesPage().rowAt(0)->bar();

    EXPECT_TRUE(bar.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_NEAR(f.amount(f.attenId), 0.51f, 1e-3f);
    EXPECT_TRUE(bar.keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(f.amount(f.attenId), 0.41f, 1e-3f);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_NEAR(f.amount(f.attenId), 0.51f, 1e-3f);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-3f);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST_F(ModuleComponentTest, TheBarIsASliderThatSpeaksPercent) {
    ModDotAmountBar bar;
    bar.setValue(-0.18f);
    EXPECT_EQ(bar.getValueText(), "-18%");
    bar.setValue(0.0f);
    EXPECT_EQ(bar.getValueText(), "0%");
    EXPECT_TRUE(bar.getWantsKeyboardFocus());
}

TEST_F(ModuleComponentTest, TypingAnAmountAppliesOnReturnAsOneUndoStepAndEscapeCancels) {
    NoMotion motion;
    Fixture f;
    auto& row = *f.clickDot()->sourcesPage().rowAt(0);

    clickNow(row.amountButton());
    ASSERT_TRUE(row.isEditingAmount());
    row.amountEditor()->setText("-18", false);
    EXPECT_TRUE(row.amountEditor()->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_FALSE(row.isEditingAmount());
    EXPECT_NEAR(f.amount(f.attenId), -0.18f, 1e-3f);
    EXPECT_EQ(row.amountButton().getButtonText(), "-18%");
    ASSERT_TRUE(f.undo.undo());
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);
    EXPECT_FALSE(f.undo.canUndo());

    clickNow(row.amountButton());
    row.amountEditor()->setText("90", false);
    EXPECT_TRUE(row.amountEditor()->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(row.isEditingAmount());
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST_F(ModuleComponentTest, RemoveWithNoAppHookCutsTheRoutingAndOneUndoBringsItBack) {
    NoMotion motion;
    Fixture f;
    auto* panel = f.clickDot();
    ASSERT_NE(panel, nullptr);

    clickNow(panel->sourcesPage().rowAt(0)->removeButton());

    EXPECT_EQ(f.engine.getGraph().getNodeForId(f.attenId), nullptr);
    EXPECT_EQ(panel->sourcesPage().rowCount(), 0);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.engine.getModulationRoutings().size(), 1u) << "one undo brings the routing back";
}

TEST_F(ModuleComponentTest, RemoveAndShowInTimelineGoThroughTheHostHooksWithTheRoutingNamed) {
    NoMotion motion;
    Fixture f;
    std::vector<synth::ui::ModulatorInfo> removed, revealed;
    f.editor->getModDot().host.removeModulator = [&](const synth::ui::ModulatorInfo& m) { removed.push_back(m); };
    f.editor->getModDot().host.revealModulator = [&](const synth::ui::ModulatorInfo& m) { revealed.push_back(m); };
    auto& row = *f.clickDot()->sourcesPage().rowAt(0);

    clickNow(row.timelineButton());
    clickNow(row.removeButton());

    ASSERT_EQ(revealed.size(), 1u);
    ASSERT_EQ(removed.size(), 1u);
    for (const auto& info : {revealed[0], removed[0]}) {
        EXPECT_EQ(info.paramId, "gain");
        EXPECT_EQ(info.targetChannel, f.gainChannel);
        EXPECT_TRUE(info.isLfo);
        EXPECT_EQ(info.sourceTitle, row.source().sourceName);
        EXPECT_TRUE(info.attenuverterUuid.isNotEmpty());
        EXPECT_TRUE(info.targetUuid.isNotEmpty());
    }
    EXPECT_NE(f.engine.getGraph().getNodeForId(f.attenId), nullptr) << "the host decides what removal does";
}

TEST_F(ModuleComponentTest, TheOpenPanelFollowsTheGraphOnTheEditorsTick) {
    NoMotion motion;
    Fixture f;
    auto* panel = f.clickDot();
    ASSERT_EQ(panel->sourcesPage().rowCount(), 1);

    f.addSecondLfo(); // patched by hand while the panel is open; refresh() ran the tick
    EXPECT_EQ(panel->sourcesPage().rowCount(), 2);
}

TEST_F(ModuleComponentTest, EscapeOnTheSourcesPageClosesThePanel) {
    NoMotion motion;
    Fixture f;
    auto* panel = f.clickDot();
    int closed = 0;
    panel->onDismiss = [&] { ++closed; };
    EXPECT_TRUE(panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(closed, 1);
}

TEST_F(ModuleComponentTest, UpAndDownWalkTheRowsAndEndOnAddSource) {
    NoMotion motion;
    Fixture f;
    f.addSecondLfo();
    auto& page = f.clickDot()->sourcesPage();
    // The page tells its rows' bars apart from "+ Add source"; the walk itself needs a native window to move
    // focus, so only the key claim is checked headlessly.
    EXPECT_TRUE(page.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_TRUE(page.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_TRUE(page.rowAt(0)->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
}
