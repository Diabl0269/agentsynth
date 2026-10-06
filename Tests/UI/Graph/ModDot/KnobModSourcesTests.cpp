// knobModSources: the sources on a knob named as the Mod Matrix names them -- the real module behind any macro
// port, with a rename showing -- and the hover chip's text built from them.

#include "../ModMatrixCanvasHelpers.h"

#include "UI/Graph/ModDot/KnobModSources.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModuleComponent/ModuleComponentModChip.h"
#include <gtest/gtest.h>

TEST(KnobModSources, LooksThroughAMacroPortToTheRealSource) {
    MatrixCanvas c(Boxed::DestInside);
    c.addRow();
    c.pickSource(c.lfo);
    c.pickDest(c.filterIn); // the LFO enters the macro through an inlet
    c.editor.timerCallback();

    const auto routings = c.editor.getCachedModRoutings();
    ASSERT_EQ(routings.size(), 1u);
    ASSERT_NE(routings.front().sourceNodeID, c.lfo) << "the raw routing starts at the port, not the LFO";

    const auto sources = synth::ui::knobModSources(c.editor, c.filterIn, kCutoff);
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_EQ(sources.front().sourceNodeId, c.lfo);
    EXPECT_EQ(sources.front().sourceName, c.nameOf(c.lfo));
    EXPECT_EQ(synth::ui::formatModHoverChipText(sources.front().sourceName, 0.42f),
              c.nameOf(c.lfo) + juce::String::fromUTF8(" \xC2\xB7 +42%"));
}

TEST(KnobModSources, ShowsARenameAndTheLiveAmount) {
    MatrixCanvas c(Boxed::DestInside);
    const auto atten = c.addRow();
    c.pickSource(c.lfo);
    c.pickDest(c.filterIn);
    c.engine.getGraph().getNodeForId(c.lfo)->properties.set("displayName", "Wobble");
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(
            findParameterByID(c.engine.getGraph().getNodeForId(atten)->getProcessor(), "amount")))
        p->setValueNotifyingHost(p->convertTo0to1(-0.3f));
    c.editor.timerCallback();

    const auto sources = synth::ui::knobModSources(c.editor, c.filterIn, kCutoff);
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_EQ(sources.front().sourceName, "Wobble");
    EXPECT_NEAR(sources.front().amount, -0.3f, 1e-4f);
}

TEST(KnobModSources, IsEmptyForAKnobNothingRoutesTo) {
    MatrixCanvas c(Boxed::DestInside);
    c.editor.timerCallback();
    EXPECT_TRUE(synth::ui::knobModSources(c.editor, c.filterOut, kCutoff).empty());
}

// The card's dot reads one count per knob, made once per routing change for the whole canvas; it must agree with the
// full per-knob list, macro ports looked through, or a dot would show the wrong number of sources.
TEST(KnobModSources, TheTickCountsAgreeWithTheListOnEveryKnob) {
    MatrixCanvas c(Boxed::DestInside);
    c.addRow();
    c.pickSource(c.lfo);
    c.pickDest(c.filterIn);
    c.editor.timerCallback();

    auto& dots = c.editor.getModDot();
    EXPECT_EQ(dots.knobSourceCount(c.filterIn, kCutoff), 1);
    EXPECT_EQ(dots.knobSourceCount(c.filterIn, kCutoff),
              (int)synth::ui::knobModSources(c.editor, c.filterIn, kCutoff).size());
    EXPECT_EQ(dots.knobSourceCount(c.filterOut, kCutoff), 0);

    const auto counts = synth::ui::countKnobModSources(c.editor, c.editor.getCachedModRoutings());
    for (const auto& [key, count] : counts)
        EXPECT_EQ(
            count,
            (int)synth::ui::knobModSources(c.editor, juce::AudioProcessorGraph::NodeID(key.first), key.second).size());
}
