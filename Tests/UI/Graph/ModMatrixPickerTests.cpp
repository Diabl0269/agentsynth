// ModMatrixPickerTests.cpp
//
// The Mod Matrix's source/destination combos open a searchable picker: categories as headers, typing
// filters, Up/Down/Return/Escape work, and a pick re-points the routing through the same path a combo
// selection takes. Every test opens it through the combo's real showPopup().

#include "ModMatrixCanvasHelpers.h"

#include "Modules/MacroInletModule.h"
#include "UI/Graph/ModMatrixPicker.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::ModMatrixPicker;

// Opens `combo`'s picker, capturing it instead of launching a CallOutBox (a headless test has no window).
std::unique_ptr<ModMatrixPicker> openPicker(MatrixCanvas& c, juce::ComboBox* combo) {
    std::unique_ptr<juce::Component> captured;
    c.matrix().setPickerLauncherForTest(
        [&captured](std::unique_ptr<juce::Component> p, juce::Rectangle<int>) { captured = std::move(p); });
    combo->showPopup();
    return std::unique_ptr<ModMatrixPicker>(dynamic_cast<ModMatrixPicker*>(captured.release()));
}

bool anyStartsWith(const std::vector<juce::String>& names, const juce::String& prefix) {
    return std::any_of(names.begin(), names.end(), [&](const auto& n) { return n.startsWith(prefix); });
}

struct PickerFixture {
    MatrixCanvas c{Boxed::DestInside};
    std::unique_ptr<ModMatrixPicker> source, dest;

    PickerFixture() {
        c.addRow();
        c.editor.setModuleDisplayName(c.lfo, "Wobble");
        c.matrix().updateRowsFromGraph();
        source = openPicker(c, c.matrix().getRowSourceComboForTest(0));
        dest = openPicker(c, c.matrix().getRowDestComboForTest(0));
    }
};

} // namespace

TEST(ModMatrixPicker, ClickingTheComboOpensAPickerGroupedUnderCategoryHeaders) {
    PickerFixture f;
    ASSERT_NE(f.source, nullptr);
    ASSERT_NE(f.dest, nullptr);

    const auto names = f.source->getVisibleRowNamesForTest();
    EXPECT_TRUE(anyStartsWith(names, "LFOs")) << "category header";
    EXPECT_TRUE(anyStartsWith(names, "Wobble")) << "the renamed module, by its card title";
    EXPECT_TRUE(anyStartsWith(f.dest->getVisibleRowNamesForTest(), "FilterIn - Cutoff"));
}

TEST(ModMatrixPicker, TypingFiltersCaseInsensitivelyAndHidesEmptyCategories) {
    PickerFixture f;
    const int height = f.source->getHeightForTest();

    f.source->setSearchTextForTest("WOB");
    const auto items = f.source->getVisibleItemTextsForTest();
    ASSERT_FALSE(items.empty());
    for (const auto& text : items)
        EXPECT_TRUE(text.startsWith("Wobble")) << text;
    // One header (LFOs) plus the matching rows; every other category header is gone.
    EXPECT_EQ(f.source->getVisibleRowNamesForTest().size(), items.size() + 1);
    EXPECT_EQ(f.source->getVisibleRowNamesForTest().front(), "LFOs");
    EXPECT_EQ(f.source->getHeightForTest(), height) << "the popup does not resize while filtering";

    f.source->setSearchTextForTest("no such module");
    EXPECT_TRUE(f.source->getVisibleRowNamesForTest().empty());

    f.source->setSearchTextForTest("");
    EXPECT_GT(f.source->getVisibleRowNamesForTest().size(), items.size() + 1);
}

TEST(ModMatrixPicker, TheSearchMatchesTheTargetLabelToo) {
    PickerFixture f;
    f.dest->setSearchTextForTest("cutoff");

    const auto items = f.dest->getVisibleItemTextsForTest();
    ASSERT_EQ(items.size(), 2u) << "both Filters' Cutoff";
    for (const auto& text : items)
        EXPECT_TRUE(text.endsWith("Cutoff")) << text;
    EXPECT_EQ(f.dest->getVisibleRowNamesForTest().front(), "Filters");
}

TEST(ModMatrixPicker, ChoosingARowRepointsTheRoutingThroughThePortSeam) {
    PickerFixture f;
    f.source->setSearchTextForTest("wobble");
    f.source->chooseVisibleItemForTest(0);
    f.c.matrix().updateRowsFromGraph();
    f.source.reset();
    f.dest->setSearchTextForTest("filterin - cutoff");
    ASSERT_EQ(f.dest->getVisibleItemTextsForTest().size(), 1u);
    f.dest->chooseVisibleItemForTest(0);
    f.dest.reset();
    f.c.matrix().updateRowsFromGraph();

    // The same outcome as selecting the combo item: an inlet minted on the macro the Filter is in.
    const auto inlets = f.c.nodesOf<MacroInletModule>();
    ASSERT_EQ(inlets.size(), 1u);
    EXPECT_TRUE(f.c.edge(inlets[0], f.c.nodesOf<AttenuverterModule>().front()));
    EXPECT_TRUE(f.c.edge(f.c.nodesOf<AttenuverterModule>().front(), f.c.filterIn, kCutoff));
    EXPECT_TRUE(f.c.matrix().getRowDestComboTextForTest(0).startsWith("FilterIn"));
}

TEST(ModMatrixPicker, ReturnPicksTheFirstMatchAndArrowsMoveTheHighlight) {
    PickerFixture f;
    f.dest->setSearchTextForTest("cutoff");
    EXPECT_EQ(f.dest->getHighlightedItemIndexForTest(), 0);
    EXPECT_TRUE(f.dest->sendKeyForTest(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.dest->getHighlightedItemIndexForTest(), 1);
    EXPECT_TRUE(f.dest->sendKeyForTest(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.dest->getHighlightedItemIndexForTest(), 1) << "stops at the last match";
    EXPECT_TRUE(f.dest->sendKeyForTest(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.dest->getHighlightedItemIndexForTest(), 0);

    f.dest->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));

    EXPECT_EQ(f.c.matrix().getRowDestComboForTest(0)->getSelectedId(),
              ModMatrixComponent::encodeComboId(f.c.filterIn, kCutoff))
        << "FilterIn sorts first among the two Cutoff rows";
}

TEST(ModMatrixPicker, EscapeClosesWithoutPicking) {
    PickerFixture f;
    f.dest->setSearchTextForTest("cutoff");
    f.dest->sendKeyForTest(juce::KeyPress(juce::KeyPress::escapeKey));

    EXPECT_EQ(f.c.matrix().getRowDestComboForTest(0)->getSelectedId(), 0);
    EXPECT_TRUE(f.c.nodesOf<MacroInletModule>().empty());
}

TEST(ModMatrixPicker, FlatSourceModeListsRowsWithoutCategoryHeaders) {
    PickerFixture f;
    f.source.reset();
    f.c.matrix().setFlatSourceMenu(true);
    f.source = openPicker(f.c, f.c.matrix().getRowSourceComboForTest(0));

    EXPECT_EQ(f.source->getVisibleRowNamesForTest().size(), f.source->getVisibleItemTextsForTest().size());
}

TEST(ModMatrixPicker, ARenameWhileAPickerIsOpenWaitsUntilItCloses) {
    PickerFixture f;
    f.c.pickSource(f.c.lfo);
    f.source.reset();
    f.dest.reset();
    f.source = openPicker(f.c, f.c.matrix().getRowSourceComboForTest(0));
    f.c.editor.setModuleDisplayName(f.c.lfo, "Renamed");

    f.c.matrix().updateRowsFromGraph();
    EXPECT_TRUE(f.c.matrix().getRowSourceComboTextForTest(0).startsWith("Wobble")) << "left alone while open";

    f.source.reset();
    f.c.matrix().updateRowsFromGraph();
    EXPECT_TRUE(f.c.matrix().getRowSourceComboTextForTest(0).startsWith("Renamed"));
}

TEST(ModMatrixPicker, EveryControlIsKeyboardReachableAndNamedForScreenReaders) {
    PickerFixture f;
    auto* sourceCombo = f.c.matrix().getRowSourceComboForTest(0);
    auto* destCombo = f.c.matrix().getRowDestComboForTest(0);
    for (auto* combo : {sourceCombo, destCombo}) {
        EXPECT_TRUE(combo->getWantsKeyboardFocus());
        EXPECT_TRUE(combo->getTitle().isNotEmpty());
        EXPECT_TRUE(combo->getTooltip().isNotEmpty());
    }

    for (auto* picker : {f.source.get(), f.dest.get()}) {
        EXPECT_TRUE(picker->getTitle().isNotEmpty());
        auto& search = picker->getSearchEditorForTest();
        EXPECT_TRUE(search.getWantsKeyboardFocus()) << "the search field takes focus when the picker opens";
        EXPECT_TRUE(search.getTitle().isNotEmpty());
        EXPECT_TRUE(search.getTooltip().isNotEmpty());
    }
}

// The bug: juce::ComboBox::showPopupIfNotActive() raises its private "menu active" flag before
// calling showPopup(), and only the stock menu lowers it again. The picker replaced that menu, so
// the flag stayed up after the first open and every later click or Return was ignored. Driven
// through the real key path (keyPressed -> showPopupIfNotActive -> async showPopup), not by calling
// showPopup() directly, which never raises the flag and so passed before the fix.
TEST(ModMatrixPicker, TheComboOpensItsPickerEveryTimeNotOnlyTheFirst) {
    PickerFixture f;
    f.source.reset();
    f.dest.reset();
    int launches = 0;
    std::unique_ptr<juce::Component> open;
    f.c.matrix().setPickerLauncherForTest([&](std::unique_ptr<juce::Component> p, juce::Rectangle<int>) {
        ++launches;
        open = std::move(p);
    });

    for (auto* combo : {f.c.matrix().getRowSourceComboForTest(0), f.c.matrix().getRowDestComboForTest(0)}) {
        for (int attempt = 1; attempt <= 3; ++attempt) {
            const int before = launches;
            EXPECT_TRUE(combo->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            EXPECT_EQ(launches, before + 1) << "attempt " << attempt;
            EXPECT_FALSE(combo->isPopupActive()) << "the stock flag is lowered while the picker is up";
            open.reset(); // the picker closes, as a pick or Escape would
        }
    }
}

TEST(ModMatrixPicker, AnOpenedAndClosedPickerLetsThePanelRefreshAgain) {
    PickerFixture f;
    f.source.reset();
    f.dest.reset();
    std::unique_ptr<juce::Component> open;
    f.c.matrix().setPickerLauncherForTest(
        [&](std::unique_ptr<juce::Component> p, juce::Rectangle<int>) { open = std::move(p); });
    f.c.pickSource(f.c.lfo);
    auto* combo = f.c.matrix().getRowSourceComboForTest(0);
    combo->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    open.reset();

    f.c.editor.setModuleDisplayName(f.c.lfo, "Renamed");
    f.c.matrix().updateRowsFromGraph();
    EXPECT_TRUE(f.c.matrix().getRowSourceComboTextForTest(0).startsWith("Renamed"))
        << "a closed picker no longer holds the list frozen";
}

TEST(ModMatrixPicker, SpaceOpensThePickerAndArrowsNeverRepointTheRouting) {
    PickerFixture f;
    f.source.reset();
    f.dest.reset();
    int launches = 0;
    f.c.matrix().setPickerLauncherForTest([&](std::unique_ptr<juce::Component>, juce::Rectangle<int>) { ++launches; });
    f.c.pickSource(f.c.lfo);
    auto* combo = f.c.matrix().getRowSourceComboForTest(0);
    const int selected = combo->getSelectedId();

    for (auto key :
         {juce::KeyPress::upKey, juce::KeyPress::downKey, juce::KeyPress::leftKey, juce::KeyPress::rightKey}) {
        EXPECT_FALSE(combo->keyPressed(juce::KeyPress(key))) << "left to the matrix's grid navigation";
        EXPECT_EQ(combo->getSelectedId(), selected);
    }
    EXPECT_TRUE(combo->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_EQ(launches, 1);
}

TEST(ModMatrixPicker, SearchMatchesWordByWordInAnyOrder) {
    EXPECT_TRUE(ModMatrixPicker::textMatchesQuery("Oscillator 8", "osc 8"));
    EXPECT_TRUE(ModMatrixPicker::textMatchesQuery("Oscillator 8 - Cutoff", "cut  OSC"));
    EXPECT_TRUE(ModMatrixPicker::textMatchesQuery("Anything", "   "));
    EXPECT_FALSE(ModMatrixPicker::textMatchesQuery("Oscillator 7", "osc 8"));
    EXPECT_FALSE(ModMatrixPicker::textMatchesQuery("Filter 1", "osc"));

    PickerFixture f;
    f.dest->setSearchTextForTest("cut filterin");
    ASSERT_EQ(f.dest->getVisibleItemTextsForTest().size(), 1u);
    EXPECT_EQ(f.dest->getVisibleItemTextsForTest().front(), "FilterIn - Cutoff");
    EXPECT_EQ(f.dest->getHighlightedItemIndexForTest(), 0) << "the one match is ready for Return";
}
