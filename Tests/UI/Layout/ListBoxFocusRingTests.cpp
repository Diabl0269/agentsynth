// ListBoxFocusRingTests.cpp -- the keyboard focus ring on a stock list's selected row: which lists it
// finds (including ones the host rebuilds later), what focus does to the selection, and the pixels.
// A headless window cannot hold keyboard focus, so focus gained is delivered through the ring's real
// focus callback and "is focused" through its test seam.
#include "../Accessibility/AccessibilitySettingsFixture.h"
#include "UI/Layout/ListBoxFocusRing.h"
#include "UI/Settings/AudioSettingsTab.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <memory>

namespace {

using synth::ui::ListBoxFocusRing;

class RowModel : public juce::ListBoxModel {
public:
    explicit RowModel(int rows)
        : rows_(rows) {}
    int getNumRows() override { return rows_; }
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}

private:
    int rows_;
};

class ListBoxFocusRingTest : public ::testing::Test {
protected:
    void SetUp() override {
        lookAndFeel.applyTheme(synth::theme::makeObsidian());
        root.setLookAndFeel(&lookAndFeel);
        root.setVisible(true);
        root.setSize(300, 300);
        root.addAndMakeVisible(container);
        container.setBounds(0, 0, 300, 300);
        list.setModel(&model);
        list.setRowHeight(24);
        container.addAndMakeVisible(list);
        list.setBounds(10, 10, 200, 200);
        ring = std::make_unique<ListBoxFocusRing>(root);
    }
    void TearDown() override {
        ring.reset();
        root.setLookAndFeel(nullptr);
    }

    juce::Image paintRoot() {
        juce::Image img(juce::Image::ARGB, root.getWidth(), root.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(img);
        root.paintEntireComponent(g, false);
        return img;
    }
    juce::Colour accent() const { return lookAndFeel.getTheme().colors.accent; }
    // A point on the top edge of row `row`, in root coordinates.
    juce::Point<int> rowTopEdge(int row) {
        const auto pos = list.getRowPosition(row, true);
        return root.getLocalPoint(&list, juce::Point<int>(pos.getCentreX(), pos.getY()));
    }
    static bool isAccent(juce::Colour c, juce::Colour accent) {
        return std::abs((int)c.getRed() - (int)accent.getRed()) < 8 &&
               std::abs((int)c.getGreen() - (int)accent.getGreen()) < 8 &&
               std::abs((int)c.getBlue() - (int)accent.getBlue()) < 8 && c.getAlpha() > 240;
    }

    synth::theme::AppLookAndFeel lookAndFeel;
    juce::Component root, container;
    RowModel model{8};
    juce::ListBox list{{}, nullptr};
    std::unique_ptr<ListBoxFocusRing> ring;
};

} // namespace

TEST_F(ListBoxFocusRingTest, FindsListsUnderTheRootAndFollowsOnesAddedOrRemovedLater) {
    EXPECT_EQ(ring->getNumListsForTest(), 1);

    RowModel otherModel(3);
    juce::ListBox added({}, &otherModel);
    container.addAndMakeVisible(added); // the host rebuilding its lists: no explicit rescan
    EXPECT_EQ(ring->getNumListsForTest(), 2);

    juce::ListBox direct({}, &otherModel);
    root.addAndMakeVisible(direct);
    EXPECT_EQ(ring->getNumListsForTest(), 3);

    // A headless (not showing) component sends no children-changed event on removal, so ask for the scan.
    container.removeChildComponent(&added);
    ring->rescan();
    EXPECT_EQ(ring->getNumListsForTest(), 2);
}

TEST_F(ListBoxFocusRingTest, AListThatTakesFocusWithNothingSelectedSelectsItsFirstRow) {
    ASSERT_EQ(list.getSelectedRow(), -1);
    ring->globalFocusChanged(&list);
    EXPECT_EQ(list.getSelectedRow(), 0);
}

TEST_F(ListBoxFocusRingTest, AnExistingSelectionIsKeptAndOtherFocusChangesDoNotSelect) {
    list.selectRow(3);
    ring->globalFocusChanged(&list);
    EXPECT_EQ(list.getSelectedRow(), 3);

    list.deselectAllRows();
    juce::TextButton other;
    ring->globalFocusChanged(&other);
    ring->globalFocusChanged(nullptr);
    EXPECT_EQ(list.getSelectedRow(), -1) << "focus elsewhere never selects a row";
}

TEST_F(ListBoxFocusRingTest, AnEmptyListStaysWithoutASelection) {
    RowModel empty(0);
    juce::ListBox none({}, &empty);
    root.addAndMakeVisible(none);
    none.setBounds(0, 220, 100, 60);
    ring->globalFocusChanged(&none);
    EXPECT_EQ(none.getSelectedRow(), -1);
}

TEST_F(ListBoxFocusRingTest, TheRingIsPaintedAroundTheSelectedRowOnlyWhileTheListIsFocused) {
    list.selectRow(2);
    EXPECT_FALSE(isAccent(paintRoot().getPixelAt(rowTopEdge(2).x, rowTopEdge(2).y), accent())) << "not focused";

    ring->setFocusedListForTest(&list);
    const auto focused = paintRoot();
    const auto edge = rowTopEdge(2);
    EXPECT_TRUE(isAccent(focused.getPixelAt(edge.x, edge.y), accent())) << "top edge of the selected row";
    EXPECT_FALSE(isAccent(focused.getPixelAt(rowTopEdge(4).x, rowTopEdge(4).y), accent())) << "other rows have none";

    list.selectRow(5);
    const auto moved = paintRoot();
    EXPECT_TRUE(isAccent(moved.getPixelAt(rowTopEdge(5).x, rowTopEdge(5).y), accent())) << "the ring follows the row";
    EXPECT_FALSE(isAccent(moved.getPixelAt(edge.x, edge.y), accent()));

    ring->setFocusedListForTest(nullptr);
    EXPECT_FALSE(isAccent(paintRoot().getPixelAt(rowTopEdge(5).x, rowTopEdge(5).y), accent())) << "focus left the list";
}

TEST_F(ListBoxFocusRingTest, TheRingNeverTakesAClickOrFocus) {
    const auto inRow = root.getLocalPoint(&list, list.getRowPosition(1, true).getCentre());
    auto* hit = root.getComponentAt(inRow.x, inRow.y);
    ASSERT_NE(hit, nullptr);
    EXPECT_TRUE(hit == list.getViewport() || list.getViewport()->isParentOf(hit)) << "the click reaches the rows";

    for (auto* child : list.getChildren())
        if (child != list.getViewport())
            EXPECT_FALSE(child->getWantsKeyboardFocus());
}

TEST_F(ListBoxFocusRingTest, AMouseClickSelectsTheClickedRowAsBefore) {
    ring->globalFocusChanged(&list); // takes focus: row 0 selected
    ASSERT_EQ(list.getSelectedRow(), 0);
    list.selectRow(4); // what ListBox's own mouse handling does for a click on row 4
    EXPECT_EQ(list.getSelectedRow(), 4);
}

TEST_F(AccessibilitySettingsTest, TheAudioTabsStockListsAllGetARing) {
    AudioSettingsTab tab(deviceManager, {});
    tab.setSize(500, 450);
    int lists = 0;
    std::function<void(juce::Component&)> count = [&](juce::Component& c) {
        for (auto* child : c.getChildren()) {
            if (dynamic_cast<juce::ListBox*>(child) != nullptr)
                ++lists;
            count(*child);
        }
    };
    count(tab.getDeviceSelector());
    EXPECT_GE(lists, 1) << "the MIDI input list is always there";
    EXPECT_EQ(tab.getListFocusRingForTest().getNumListsForTest(), lists);
}
