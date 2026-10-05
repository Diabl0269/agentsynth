// The mod dot's panel across a macro boundary: adding a source from outside a macro to a knob inside it crosses
// through a port the way a dragged cable does (one undo step, ports included), the row still names the real
// source, and removing the row takes the port with it, one undo bringing both back. Mirrors
// Tests/UI/Graph/ModMatrixMacroRoutingTests.cpp. docs/modules/modulation.md#the-mod-dot-menu.

#include "../ModMatrixCanvasHelpers.h"

#include "AudioEngine/ModuleTitle.h"
#include "Modules/MacroInletModule.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModDot/ModDotPopover.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {
// Button::triggerClick() posts the click as a command message, so it takes effect after one dispatch pass.
void click(juce::Button& button) {
    button.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}
struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};
} // namespace

TEST(ModDotMacroRouting, AddingASourceIntoAMacroMintsAnInletAndTheRowNamesTheRealSource) {
    NoMotion motion;
    MatrixCanvas c(Boxed::DestInside);
    juce::Component anchor;
    std::unique_ptr<juce::Component> held;
    auto& dot = c.editor.getModDot();
    dot.popoverLauncher = [&held](std::unique_ptr<juce::Component> content, juce::Component&) {
        held = std::move(content);
    };
    dot.openPopover(c.filterIn, kCutoff, anchor);
    auto* panel = dynamic_cast<synth::ui::ModDotPopover*>(held.get());
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->sourcesPage().rowCount(), 0);

    click(panel->sourcesPage().addButton());
    const auto lfoTitle = synth::moduleTitle(*c.engine.getGraph().getNodeForId(c.lfo));
    synth::ui::ModDotChoiceRow* lfoRow = nullptr;
    for (int i = 0; auto* row = panel->addSourcePage().visibleRow(i); ++i)
        if (row->item().label() == lfoTitle)
            lfoRow = row;
    ASSERT_NE(lfoRow, nullptr);
    lfoRow->pick();

    const auto inlets = c.nodesOf<MacroInletModule>();
    ASSERT_EQ(inlets.size(), 1u) << "the crossing mints one inlet";
    ASSERT_EQ(panel->sourcesPage().rowCount(), 1);
    EXPECT_EQ(panel->sourcesPage().rowAt(0)->source().sourceName, lfoTitle) << "the real source, not the port";
    EXPECT_EQ(panel->sourcesPage().rowAt(0)->source().sourceNodeId, c.lfo);

    // The port is not a source to offer, and neither is the LFO twice.
    click(panel->sourcesPage().addButton());
    const auto portTitle = synth::moduleTitle(*c.engine.getGraph().getNodeForId(inlets[0]));
    for (const auto& label : panel->addSourcePage().visibleRowLabels())
        EXPECT_NE(label, portTitle);
    click(panel->sourcesPage().addButton()); // folds the list again

    ASSERT_TRUE(c.undo.undo());
    EXPECT_TRUE(c.nodesOf<MacroInletModule>().empty()) << "one undo takes the cable and the port";
    EXPECT_TRUE(c.macro().ports.empty());
    held.reset();
}

TEST(ModDotMacroRouting, RemovingARowTakesThePortItUsedAndOneUndoBringsBothBack) {
    NoMotion motion;
    MatrixCanvas c(Boxed::DestInside);
    juce::Component anchor;
    std::unique_ptr<juce::Component> held;
    auto& dot = c.editor.getModDot();
    dot.popoverLauncher = [&held](std::unique_ptr<juce::Component> content, juce::Component&) {
        held = std::move(content);
    };
    ASSERT_NE(c.editor.connectModulationSource(c.lfo, 0, c.filterIn, kCutoff, 0.5f).uid, 0u);
    ASSERT_EQ(c.nodesOf<MacroInletModule>().size(), 1u);
    c.editor.timerCallback();

    dot.openPopover(c.filterIn, kCutoff, anchor);
    auto* panel = dynamic_cast<synth::ui::ModDotPopover*>(held.get());
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->sourcesPage().rowCount(), 1);
    const auto atten = panel->sourcesPage().rowAt(0)->attenuverterId();

    click(panel->sourcesPage().rowAt(0)->removeButton());

    EXPECT_EQ(c.engine.getGraph().getNodeForId(atten), nullptr);
    EXPECT_TRUE(c.nodesOf<MacroInletModule>().empty());
    EXPECT_TRUE(c.macro().ports.empty());
    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.nodesOf<MacroInletModule>().size(), 1u) << "one undo brings back the routing and its port";
    held.reset();
}
