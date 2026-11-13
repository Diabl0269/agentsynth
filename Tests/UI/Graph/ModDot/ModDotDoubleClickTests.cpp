// A double-click on a knob's mod dot, or on a visible CV jack that drives a knob's modulation: one source is
// removed as a whole chain in one undo step, several open the dot's panel with the remove buttons highlighted.
// Real events: a press with two clicks through the knob / the card. docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotTestFixture.h"

#include "../ModMatrixCanvasHelpers.h"
#include "Modules/MacroInletModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// The second press of a double-click on the dot, as the knob receives it (then its release).
void doubleClickDot(Fixture& f) {
    const auto dot = f.dotInKnob();
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, dot, kPlain, 2));
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, dot, kPlain, 2));
}

} // namespace

TEST_F(ModuleComponentTest, DoubleClickingTheDotWithOneSourceRemovesItKeepsTheLfoAndOneUndoRestoresIt) {
    NoMotion motion;
    Fixture f;
    ASSERT_NE(f.clickDot(), nullptr) << "the first click opens the menu";
    const float knobBefore = (float)f.gainKnob->getValue();

    doubleClickDot(f);

    EXPECT_TRUE(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).empty());
    EXPECT_EQ(f.engine.getGraph().getNodeForId(f.attenId), nullptr) << "the attenuverter goes with it";
    EXPECT_NE(f.engine.getGraph().getNodeForId(f.lfoId), nullptr) << "the source module stays";
    EXPECT_EQ(f.gainKnob->getValue(), knobBefore) << "the dot's double-click does not reset the knob";
    ASSERT_TRUE(f.undo.undo());
    f.refresh();
    EXPECT_EQ(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).size(), 1u) << "one undo step";
    EXPECT_FALSE(f.undo.canUndo()) << "and only one step was recorded";
}

TEST_F(ModuleComponentTest, DoubleClickingTheDotWithSeveralSourcesRemovesNothingAndHighlightsTheRemoveButtons) {
    NoMotion motion;
    Fixture f;
    const auto second = f.addSecondLfo();
    ASSERT_NE(f.clickDot(), nullptr);

    doubleClickDot(f);

    EXPECT_EQ(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).size(), 2u);
    auto* panel = f.popover();
    ASSERT_NE(panel, nullptr);
    EXPECT_TRUE(panel->isRemoveHighlighted());
    ASSERT_EQ(panel->sourcesPage().rowCount(), 2);
    for (int i = 0; i < 2; ++i) {
        auto& button = panel->sourcesPage().rowAt(i)->removeButton();
        EXPECT_TRUE(static_cast<synth::ui::ModDotGlyphButton&>(button).isDanger());
        EXPECT_TRUE(button.getTitle().startsWith("Remove ")) << "names stay as they were";
    }

    clickNow(panel->sourcesPage().rowFor(second)->removeButton());
    const auto left = synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true);
    ASSERT_EQ(left.size(), 1u) << "a remove click there removes only that one";
    EXPECT_EQ(left[0].attenuverterId, f.attenId);
    ASSERT_TRUE(f.undo.undo());
    f.refresh();
    EXPECT_EQ(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).size(), 2u);
}

TEST_F(ModuleComponentTest, ADoubleClickOnTheDotAloneOpensThePanelHighlightedWhenThereAreSeveralSources) {
    NoMotion motion;
    Fixture f;
    f.addSecondLfo();
    ASSERT_EQ(f.popover(), nullptr);
    doubleClickDot(f);
    ASSERT_NE(f.popover(), nullptr);
    EXPECT_TRUE(f.popover()->isRemoveHighlighted());
    f.held.reset();
    f.clickDot();
    EXPECT_FALSE(f.popover()->isRemoveHighlighted()) << "a plain click opens the normal panel";
}

TEST_F(ModuleComponentTest, WithTheDoubleClickPreferenceOffTheDotDoubleClickRemovesNothing) {
    NoMotion motion;
    Fixture f;
    f.editor->setDoubleClickPortDisconnectEnabled(false);
    f.clickDot();
    doubleClickDot(f);
    EXPECT_EQ(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
    if (auto* panel = f.popover())
        EXPECT_FALSE(panel->isRemoveHighlighted());
}

TEST_F(ModuleComponentTest, TheDotTooltipMentionsTheDoubleClick) {
    Fixture f;
    EXPECT_TRUE(f.vcaCard->getModDotButton(f.gainChannel)->getTooltip().endsWith("Double-click to remove"));
}

// A VCA whose gain is not shown as a knob keeps a visible CV jack for it, the jack the double-click lands on.
TEST_F(ModuleComponentTest, DoubleClickingAVisibleCvJackRemovesJustTheOneChainAndAnAudioJackStillDisconnects) {
    NoMotion motion;
    Fixture f(/*modulated*/ true, /*gainAsKnob*/ false);
    auto* mb = dynamic_cast<ModuleBase*>(f.vcaCard->getModule());
    ASSERT_NE(mb, nullptr);
    const int cvJack = mb->mapInputChannel(f.gainChannel).visibleJackIndex;
    ASSERT_GE(cvJack, 0);
    ASSERT_EQ(f.vcaCard->getModDotButton(f.gainChannel), nullptr) << "no dot: the jack is the visible landing";

    // An audio cable into the VCA's first audio jack, to prove the old path is untouched.
    auto osc = f.engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    int audioJack = -1;
    for (int i = 0; i < mb->getVisibleInputPortCount(); ++i)
        if (i != cvJack && audioJack < 0)
            audioJack = i;
    ASSERT_GE(audioJack, 0);
    ASSERT_TRUE(f.engine.getGraph().addConnection({{osc->nodeID, 0}, {f.vcaId, 0}}));
    f.refresh();

    const auto at = f.vcaCard->getPortCenter(cvJack, true).roundToInt();
    f.vcaCard->mouseDown(makeModuleClickWithMods(*f.vcaCard, at, kPlain, 2));
    EXPECT_TRUE(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).empty());
    EXPECT_NE(f.engine.getGraph().getNodeForId(f.lfoId), nullptr);
    EXPECT_TRUE(f.engine.getGraph().isConnected({{osc->nodeID, 0}, {f.vcaId, 0}})) << "only that chain went";
    ASSERT_TRUE(f.undo.undo());
    f.refresh();
    EXPECT_EQ(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).size(), 1u);

    const auto audioAt = f.vcaCard->getPortCenter(audioJack, true).roundToInt();
    f.vcaCard->mouseDown(makeModuleClickWithMods(*f.vcaCard, audioAt, kPlain, 2));
    EXPECT_FALSE(f.engine.getGraph().isConnected({{osc->nodeID, 0}, {f.vcaId, 0}})) << "an audio jack disconnects";
}

TEST_F(ModuleComponentTest, DoubleClickingAVisibleCvJackWithSeveralSourcesOpensTheHighlightedPanel) {
    NoMotion motion;
    Fixture f(true, false);
    f.addSecondLfo();
    auto* mb = dynamic_cast<ModuleBase*>(f.vcaCard->getModule());
    const int cvJack = mb->mapInputChannel(f.gainChannel).visibleJackIndex;
    const auto at = f.vcaCard->getPortCenter(cvJack, true).roundToInt();
    f.vcaCard->mouseDown(makeModuleClickWithMods(*f.vcaCard, at, kPlain, 2));
    EXPECT_EQ(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).size(), 2u) << "nothing disconnected";
    ASSERT_NE(f.popover(), nullptr);
    EXPECT_TRUE(f.popover()->isRemoveHighlighted());
}

TEST(ModDotDoubleClick, ThroughAMacroPortTheWholeChainGoesInOneUndoStep) {
    NoMotion motion;
    MatrixCanvas c(Boxed::DestInside);
    juce::Component anchor;
    c.editor.getModDot().popoverLauncher = [](std::unique_ptr<juce::Component>, juce::Component&) {};
    ASSERT_NE(c.editor.connectModulationSource(c.lfo, 0, c.filterIn, kCutoff, 0.5f).uid, 0u);
    ASSERT_EQ(c.nodesOf<MacroInletModule>().size(), 1u);
    c.editor.timerCallback();

    c.editor.getModDot().dotDoubleClicked(c.filterIn, kCutoff, anchor);

    EXPECT_TRUE(c.nodesOf<MacroInletModule>().empty()) << "the port hop goes with the chain";
    EXPECT_NE(c.engine.getGraph().getNodeForId(c.lfo), nullptr);
    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.nodesOf<MacroInletModule>().size(), 1u);
}
