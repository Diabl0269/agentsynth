// ModMatrixPickerRowExtrasTests.cpp -- the per-row extras the command palette uses: an on/off row flips in place and
// keeps the picker open, a row's action button is reached with Tab (or Right at the end of the text) and pressed with
// Return, and a row source replaces the picker's own filter.
#include "UI/Graph/ModMatrixPicker.h"
#include <gtest/gtest.h>
#include <memory>

using synth::ui::ModMatrixPicker;

namespace {

ModMatrixPicker::Item row(int id, const juce::String& text) {
    ModMatrixPicker::Item item;
    item.id = id;
    item.category = "Things";
    item.text = text;
    return item;
}

struct Host {
    std::vector<int> chosen, actions;
    int dismissed = 0;
    juce::Component parent;
    std::unique_ptr<ModMatrixPicker> picker;

    explicit Host(std::vector<ModMatrixPicker::Item> items) {
        picker =
            std::make_unique<ModMatrixPicker>("thing", std::move(items), -1, [this](int id) { chosen.push_back(id); });
        picker->onRowAction = [this](int id) { actions.push_back(id); };
        picker->onDismissRequested = [this] { ++dismissed; };
        parent.setSize(400, 400);
        parent.addAndMakeVisible(*picker);
        picker->setBounds(0, 0, 360, 300);
    }
    bool key(int code, juce::ModifierKeys mods = {}) { return picker->sendKeyForTest(juce::KeyPress(code, mods, 0)); }
};

} // namespace

TEST(ModMatrixPickerRowExtras, AnOnOffRowFlipsInPlaceAndThePickerStaysOpen) {
    auto toggle = row(1, "Natural scrolling");
    toggle.switchOn = true;
    Host h({toggle, row(2, "Plain")});
    ASSERT_EQ(h.picker->getVisibleItemSwitchForTest(0), std::optional<bool>(true));
    EXPECT_FALSE(h.picker->getVisibleItemSwitchForTest(1).has_value());

    h.key(juce::KeyPress::returnKey);
    EXPECT_EQ(h.chosen, std::vector<int>{1});
    EXPECT_EQ(h.dismissed, 0) << "an on/off row never closes the picker";
    EXPECT_EQ(h.picker->getVisibleItemSwitchForTest(0), std::optional<bool>(false));

    h.picker->chooseVisibleItemForTest(1);
    EXPECT_EQ(h.dismissed, 1) << "a plain row closes it, through onDismissRequested outside a CallOutBox";
}

TEST(ModMatrixPickerRowExtras, TabStepsOntoTheRowsActionAndReturnPressesIt) {
    auto withAction = row(7, "Bass");
    withAction.action = ModMatrixPicker::RowAction{"Show plugin window, Bass", "Show or hide the window"};
    Host h({withAction, row(8, "Drums")});
    auto* button = h.picker->getVisibleItemActionButtonForTest(0);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->getTitle(), "Show plugin window, Bass");
    EXPECT_EQ(h.picker->getVisibleItemActionButtonForTest(1), nullptr);

    EXPECT_TRUE(h.key(juce::KeyPress::tabKey));
    EXPECT_TRUE(h.picker->isActionSelectedForTest());
    EXPECT_TRUE(h.key(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier));
    EXPECT_FALSE(h.picker->isActionSelectedForTest());
    EXPECT_TRUE(h.key(juce::KeyPress::rightKey)) << "Right at the end of the (empty) text steps on too";
    EXPECT_TRUE(h.picker->isActionSelectedForTest());

    h.key(juce::KeyPress::returnKey);
    EXPECT_EQ(h.actions, std::vector<int>{7});
    EXPECT_TRUE(h.chosen.empty()) << "the button, not the row";
    EXPECT_EQ(h.dismissed, 1);
}

TEST(ModMatrixPickerRowExtras, TabOnARowWithoutAnActionKeepsItsUsualMeaning) {
    Host h({row(1, "Plain")});
    EXPECT_FALSE(h.key(juce::KeyPress::tabKey));
    EXPECT_FALSE(h.picker->isActionSelectedForTest());
}

TEST(ModMatrixPickerRowExtras, MovingTheHighlightTakesItOffTheAction) {
    auto a = row(1, "Bass");
    a.action = ModMatrixPicker::RowAction{"Window, Bass", "tip"};
    Host h({a, row(2, "Drums")});
    h.key(juce::KeyPress::tabKey);
    ASSERT_TRUE(h.picker->isActionSelectedForTest());
    h.key(juce::KeyPress::downKey);
    EXPECT_FALSE(h.picker->isActionSelectedForTest());
    h.key(juce::KeyPress::returnKey);
    EXPECT_EQ(h.chosen, std::vector<int>{2});
    EXPECT_TRUE(h.actions.empty());
}

TEST(ModMatrixPickerRowExtras, ARowSourceListsExactlyWhatItReturnsForEachQuery) {
    Host h({row(1, "Ignored")});
    h.picker->setRowSource([](const juce::String& query) {
        ModMatrixPicker::QueryRows rows;
        rows.highlight = query.fromFirstOccurrenceOf("+", false, false).trim();
        auto top = row(10, "Oscillator");
        top.category = "Top match";
        rows.items.push_back(top);
        if (query.isEmpty())
            rows.items.push_back(row(11, "Noise"));
        return rows;
    });
    EXPECT_EQ(h.picker->getVisibleItemTextsForTest(), (std::vector<juce::String>{"Oscillator", "Noise"}));
    EXPECT_EQ(h.picker->getHighlightedItemIndexForTest(), 0);

    h.picker->setSearchTextForTest("+ osc");
    EXPECT_EQ(h.picker->getVisibleItemTextsForTest(), std::vector<juce::String>{"Oscillator"});
    EXPECT_EQ(h.picker->getVisibleRowNamesForTest().front(), "Top match");
    const auto spans = h.picker->getVisibleItemHighlightSpansForTest();
    ASSERT_FALSE(spans.empty());
    ASSERT_FALSE(spans.front().empty()) << "the highlight is the source's words, not the raw \"+ osc\"";
    EXPECT_EQ(spans.front().front().start, 0);
    EXPECT_EQ(spans.front().front().length, 3);
}

// Regression test for FRO574: rows laid out before the scrollbar appeared kept the full width, so their right edges
// stuck out past the rows below.
TEST(ModMatrixPickerRowExtras, EveryRowFollowsTheWidthLeftByTheScrollbarOnceTheListNeedsOne) {
    Host h({row(1, "Ignored")});
    auto source = [](int count) {
        return [count](const juce::String&) {
            ModMatrixPicker::QueryRows rows;
            for (int i = 0; i < count; ++i)
                rows.items.push_back(row(100 + i, "Row " + juce::String(i)));
            return rows;
        };
    };
    h.picker->setRowSource(source(2));
    h.picker->setRowSource(source(60));

    auto& column = h.picker->getRowColumnForTest();
    ASSERT_GT(column.getNumChildComponents(), 3);
    const int width = column.getChildComponent(0)->getWidth();
    EXPECT_EQ(width, column.getWidth());
    for (auto* child : column.getChildren()) {
        EXPECT_EQ(child->getWidth(), width) << "row at y=" << child->getY();
        EXPECT_EQ(child->getRight(), column.getWidth());
    }
}
