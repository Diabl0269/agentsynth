// ModMatrixTitleTests.cpp
//
// The Mod Matrix lists modules under the title their card shows: the user's custom "displayName" when
// set, else the auto-numbered name. A rename refreshes the labels without the node count changing.

#include "ModMatrixCanvasHelpers.h"

#include "AudioEngine/ModuleTitle.h"
#include <gtest/gtest.h>

TEST(ModMatrixTitles, ASourceComboShowsTheRenamedCardTitle) {
    MatrixCanvas c(Boxed::DestInside);
    c.addRow();
    c.pickSource(c.lfo);
    EXPECT_TRUE(c.matrix().getRowSourceComboTextForTest(0).startsWith(c.nameOf(c.lfo)));

    c.editor.setModuleDisplayName(c.lfo, "Wobble");
    c.matrix().updateRowsFromGraph(); // the next timer tick; the node count did not change

    EXPECT_TRUE(c.matrix().getRowSourceComboTextForTest(0).startsWith("Wobble"))
        << c.matrix().getRowSourceComboTextForTest(0);
}

TEST(ModMatrixTitles, ADestinationComboShowsTheRenamedCardTitle) {
    MatrixCanvas c(Boxed::SourceInside);
    c.addRow();
    c.pickSource(c.lfo);
    c.pickDest(c.filterOut);
    c.editor.setModuleDisplayName(c.filterOut, "Bright");
    c.matrix().updateRowsFromGraph();

    EXPECT_TRUE(c.matrix().getRowDestComboTextForTest(0).startsWith("Bright"));
    EXPECT_TRUE(c.matrix().getRowDestComboTextForTest(0).contains("Cutoff"));
}

TEST(ModMatrixTitles, ClearingTheTitleFallsBackToTheNumberedName) {
    MatrixCanvas c(Boxed::DestInside);
    c.addRow();
    c.pickSource(c.lfo);
    c.editor.setModuleDisplayName(c.lfo, "Wobble");
    c.matrix().updateRowsFromGraph();
    c.editor.setModuleDisplayName(c.lfo, "");
    c.matrix().updateRowsFromGraph();

    EXPECT_TRUE(c.matrix().getRowSourceComboTextForTest(0).startsWith(c.nameOf(c.lfo)));
}

TEST(ModMatrixTitles, ModuleTitlePrefersTheCustomNameOverTheProcessorName) {
    LFOModule lfo;
    juce::NamedValueSet props;
    EXPECT_EQ(synth::moduleTitle(props, &lfo), lfo.getName());
    props.set("displayName", "Wobble");
    EXPECT_EQ(synth::moduleTitle(props, &lfo), "Wobble");
    EXPECT_EQ(synth::moduleTitle(props, nullptr), "Wobble");
    EXPECT_EQ(synth::moduleTitle(juce::NamedValueSet(), nullptr), juce::String());
}
