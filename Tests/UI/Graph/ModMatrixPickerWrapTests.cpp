// ModMatrixPickerWrapTests.cpp -- Up on the first row goes to the last, Down on the last goes to the first.
//
// One rule for every ModMatrixPicker (Cmd+K, mod matrix, Replace with, instrument picker, Add list): a search list
// wraps like a macOS menu, VS Code quick-open, Raycast and Spotlight. Disabled rows are skipped, also across the wrap,
// and headings are never stops. The Key picker follows the same rule.
#include "UI/Graph/ModMatrixPicker.h"

#include <gtest/gtest.h>
#include <set>

namespace {

using synth::ui::ModMatrixPicker;

// Rows "Even 0", "Odd 1", ... under one heading per pair; `disabled` lists the indices shown greyed.
std::unique_ptr<ModMatrixPicker> makePicker(int count, std::set<int> disabled = {}) {
    std::vector<ModMatrixPicker::Item> items;
    for (int i = 0; i < count; ++i) {
        ModMatrixPicker::Item item;
        item.id = i + 1;
        item.category = "Group " + juce::String(i / 2);
        item.text = juce::String(i % 2 == 0 ? "Even " : "Odd ") + juce::String(i);
        item.enabled = disabled.count(i) == 0;
        items.push_back(item);
    }
    return std::make_unique<ModMatrixPicker>("source", std::move(items), 0, [](int) {});
}

void press(ModMatrixPicker& p, int key) { ASSERT_TRUE(p.sendKeyForTest(juce::KeyPress(key))); }

} // namespace

TEST(ModMatrixPickerWrap, UpOnTheFirstRowGoesToTheLastAndDownOnTheLastGoesToTheFirst) {
    auto p = makePicker(5);
    ASSERT_EQ(p->getHighlightedItemIndexForTest(), 0);
    press(*p, juce::KeyPress::upKey);
    EXPECT_EQ(p->getHighlightedItemIndexForTest(), 4);
    EXPECT_TRUE(p->isHighlightInViewForTest()) << "the wrap scrolls the new row into view";
    press(*p, juce::KeyPress::downKey);
    EXPECT_EQ(p->getHighlightedItemIndexForTest(), 0);
    press(*p, juce::KeyPress::downKey);
    EXPECT_EQ(p->getHighlightedItemIndexForTest(), 1) << "a plain step still moves one row";
}

TEST(ModMatrixPickerWrap, DisabledRowsAtTheEndsAreSkippedAcrossTheWrap) {
    auto p = makePicker(5, {0, 4}); // first and last greyed: pickable rows are 1..3
    ASSERT_EQ(p->getHighlightedItemIndexForTest(), 1);
    press(*p, juce::KeyPress::upKey);
    EXPECT_EQ(p->getHighlightedItemIndexForTest(), 3) << "Up from the first pickable row lands on the last pickable";
    press(*p, juce::KeyPress::downKey);
    EXPECT_EQ(p->getHighlightedItemIndexForTest(), 1) << "Down from the last pickable row lands on the first pickable";
}

TEST(ModMatrixPickerWrap, OneItemOrOnlyOnePickableRowStaysPut) {
    auto one = makePicker(1);
    press(*one, juce::KeyPress::upKey);
    EXPECT_EQ(one->getHighlightedItemIndexForTest(), 0);
    press(*one, juce::KeyPress::downKey);
    EXPECT_EQ(one->getHighlightedItemIndexForTest(), 0);

    auto lone = makePicker(4, {0, 1, 3});
    ASSERT_EQ(lone->getHighlightedItemIndexForTest(), 2);
    press(*lone, juce::KeyPress::downKey);
    EXPECT_EQ(lone->getHighlightedItemIndexForTest(), 2);
    press(*lone, juce::KeyPress::upKey);
    EXPECT_EQ(lone->getHighlightedItemIndexForTest(), 2);
}

TEST(ModMatrixPickerWrap, AListOfOnlyDisabledRowsAndAnEmptyFilterDoNotMoveOrCrash) {
    auto none = makePicker(3, {0, 1, 2});
    press(*none, juce::KeyPress::upKey);
    press(*none, juce::KeyPress::downKey);
    EXPECT_EQ(none->getHighlightedItemIndexForTest(), 0);

    auto p = makePicker(4);
    p->setSearchTextForTest("zzz"); // matches nothing: the visible list is empty
    press(*p, juce::KeyPress::upKey);
    press(*p, juce::KeyPress::downKey);
    EXPECT_EQ(p->getHighlightedItemIndexForTest(), 0);
}

TEST(ModMatrixPickerWrap, WrapsWithinTheTypedFilterAndClearingItRestoresTheWholeList) {
    auto p = makePicker(6);
    p->setSearchTextForTest("even"); // rows 0, 2, 4
    ASSERT_EQ(p->getHighlightedItemIndexForTest(), 0);
    press(*p, juce::KeyPress::upKey);
    EXPECT_EQ(p->getHighlightedItemIndexForTest(), 2) << "the last of the three visible matches";
    press(*p, juce::KeyPress::downKey);
    EXPECT_EQ(p->getHighlightedItemIndexForTest(), 0);

    p->setSearchTextForTest("");
    press(*p, juce::KeyPress::upKey);
    EXPECT_EQ(p->getHighlightedItemIndexForTest(), 5);
}
