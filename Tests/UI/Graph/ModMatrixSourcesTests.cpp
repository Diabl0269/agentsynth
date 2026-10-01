// ModMatrixSourcesTests.cpp
//
// The Mod Matrix's source list offers the jacks a module's card draws, not its raw channels: an
// LFO (one CV jack, two silent pass-through channels behind it) is one entry. A routing saved from a
// channel the list no longer offers still shows its source instead of a blank box.

#include "ModMatrixCanvasHelpers.h"

#include "Modules/PolyMidiModule.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include "UI/Graph/ModMatrixPicker.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::ModMatrixPicker;

std::unique_ptr<ModMatrixPicker> openSourcePicker(MatrixCanvas& c) {
    std::unique_ptr<juce::Component> captured;
    c.matrix().setPickerLauncherForTest(
        [&captured](std::unique_ptr<juce::Component> p, juce::Rectangle<int>) { captured = std::move(p); });
    c.matrix().getRowSourceComboForTest(0)->showPopup();
    return std::unique_ptr<ModMatrixPicker>(dynamic_cast<ModMatrixPicker*>(captured.release()));
}

} // namespace

TEST(ModMatrixSources, AnLfoOffersItsOneCvJack) {
    LFOModule lfo;
    ASSERT_EQ(lfo.getTotalNumOutputChannels(), 5) << "four pass-through channels sit behind the CV jack";

    const auto outputs = synth::ui::modSourceOutputs(lfo);
    ASSERT_EQ(outputs.size(), 1u);
    EXPECT_EQ(outputs.front().channel, 0);
    EXPECT_TRUE(outputs.front().label.isEmpty()) << "a single entry reads as just the module's name";
}

TEST(ModMatrixSources, AJackFrontingTwoPolyHeadsListsEachByRole) {
    PolyMidiModule poly;
    const auto outputs = synth::ui::modSourceOutputs(poly);
    ASSERT_EQ(outputs.size(), 2u);
    EXPECT_EQ(outputs[0].label, "Poly Out Pitch");
    EXPECT_EQ(outputs[1].label, "Poly Out Gate");
    EXPECT_NE(outputs[0].channel, outputs[1].channel);
}

TEST(ModMatrixSources, EveryEntryIsADrawnJack) {
    OscillatorModule osc;
    const auto outputs = synth::ui::modSourceOutputs(osc);
    ASSERT_FALSE(outputs.empty());
    EXPECT_LE((int)outputs.size(), osc.getVisibleOutputPortCount() * 2);
    EXPECT_LT((int)outputs.size(), osc.getTotalNumOutputChannels()) << "not one per raw channel";
}

TEST(ModMatrixSources, ThePickerListsTheLfoOnce) {
    MatrixCanvas c{Boxed::DestInside};
    c.addRow();
    c.editor.setModuleDisplayName(c.lfo, "Wobble");
    c.matrix().updateRowsFromGraph();
    auto picker = openSourcePicker(c);
    ASSERT_NE(picker, nullptr);

    picker->setSearchTextForTest("wobble");
    const auto items = picker->getVisibleItemTextsForTest();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front(), "Wobble");
}

TEST(ModMatrixSources, ARoutingFromAnUnlistedChannelKeepsItsSourceVisible) {
    MatrixCanvas c{Boxed::DestInside};
    c.engine.addModRouting(c.lfo, 1, c.filterOut, kCutoff); // an LFO pass-through, as an old project may hold
    c.matrix().updateRowsFromGraph();
    c.matrix().updateRowsFromGraph();

    ASSERT_EQ(c.matrix().getNumRowsForTest(), 1);
    EXPECT_TRUE(c.matrix().getRowSourceComboTextForTest(0).endsWith("Out 2"))
        << "was: " << c.matrix().getRowSourceComboTextForTest(0);

    c.matrix().updateRowsFromGraph(); // the next tick does not add it twice
    auto picker = openSourcePicker(c);
    ASSERT_NE(picker, nullptr);
    picker->setSearchTextForTest("out 2");
    EXPECT_EQ(picker->getVisibleItemTextsForTest().size(), 1u);
}
