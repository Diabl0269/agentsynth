// ModMatrixKeyboardTests.cpp
//
// Inside the Mod Matrix region the arrow keys walk its controls (ModMatrixKeyboard.h): Left/Right in
// reading order, Up/Down by column, the amount slider keeping Up/Down for itself. Focus cannot move
// headlessly (no native window), so these pin the target each key picks, plus the controls' own key
// handling that decides whether a key reaches the matrix at all.

#include "ModMatrixCanvasHelpers.h"

#include "UI/Graph/ModMatrixKeyboard.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::modMatrixArrowTarget;
namespace ids = synth::ui::modmatrix_ids;

const juce::KeyPress kLeft(juce::KeyPress::leftKey), kRight(juce::KeyPress::rightKey), kUp(juce::KeyPress::upKey),
    kDown(juce::KeyPress::downKey);

struct TwoRows {
    MatrixCanvas c{Boxed::DestInside};
    TwoRows() {
        c.addRow();
        c.addRow();
        c.matrix().updateRowsFromGraph();
    }
    juce::Component& matrix() { return c.matrix(); }
    juce::Component* header(const char* id) { return matrix().findChildWithID(id); }
    // The control `id` on routing row `row` (0-based, on-screen order).
    juce::Component* cell(int row, const char* id) {
        auto* source = row == 0 ? c.matrix().getRowSourceComboForTest(0) : c.matrix().getRowSourceComboForTest(1);
        return source->getParentComponent()->findChildWithID(id);
    }
};

} // namespace

TEST(ModMatrixKeyboard, FromThePanelAnyArrowEntersAtTheFirstControl) {
    TwoRows t;
    ASSERT_NE(t.header(ids::kAdd), nullptr);
    for (const auto& key : {kLeft, kRight, kUp, kDown})
        EXPECT_EQ(modMatrixArrowTarget(t.matrix(), &t.matrix(), key), t.header(ids::kAdd));
}

TEST(ModMatrixKeyboard, LeftAndRightWalkEveryControlInReadingOrder) {
    TwoRows t;
    const std::vector<juce::Component*> order{
        t.header(ids::kAdd),     t.header(ids::kFlat),    t.cell(0, ids::kSource), t.cell(0, ids::kDest),
        t.cell(0, ids::kAmount), t.cell(0, ids::kBypass), t.cell(0, ids::kDelete), t.cell(1, ids::kSource),
        t.cell(1, ids::kDest),   t.cell(1, ids::kAmount), t.cell(1, ids::kBypass), t.cell(1, ids::kDelete)};
    for (auto* c : order)
        ASSERT_NE(c, nullptr);

    for (size_t i = 0; i + 1 < order.size(); ++i) {
        EXPECT_EQ(modMatrixArrowTarget(t.matrix(), order[i], kRight), order[i + 1]) << i;
        EXPECT_EQ(modMatrixArrowTarget(t.matrix(), order[i + 1], kLeft), order[i]) << i;
    }
    EXPECT_EQ(modMatrixArrowTarget(t.matrix(), order.back(), kRight), order.back()) << "stops at the end";
    EXPECT_EQ(modMatrixArrowTarget(t.matrix(), order.front(), kLeft), order.front()) << "stops at the start";
}

TEST(ModMatrixKeyboard, UpAndDownKeepTheColumn) {
    TwoRows t;
    EXPECT_EQ(modMatrixArrowTarget(t.matrix(), t.cell(0, ids::kDest), kDown), t.cell(1, ids::kDest));
    EXPECT_EQ(modMatrixArrowTarget(t.matrix(), t.cell(1, ids::kDelete), kUp), t.cell(0, ids::kDelete));
    EXPECT_EQ(modMatrixArrowTarget(t.matrix(), t.cell(0, ids::kDelete), kUp), t.header(ids::kFlat))
        << "clamped to the shorter header line";
    EXPECT_EQ(modMatrixArrowTarget(t.matrix(), t.header(ids::kFlat), kDown), t.cell(0, ids::kDest));
    EXPECT_EQ(modMatrixArrowTarget(t.matrix(), t.cell(1, ids::kSource), kDown), t.cell(1, ids::kSource));
}

TEST(ModMatrixKeyboard, OnlyArrowsFromInsideTheMatrixAreTaken) {
    TwoRows t;
    EXPECT_EQ(modMatrixArrowTarget(t.matrix(), t.cell(0, ids::kDest), juce::KeyPress(juce::KeyPress::returnKey)),
              nullptr);
    juce::Component outside;
    EXPECT_EQ(modMatrixArrowTarget(t.matrix(), &outside, kDown), nullptr);
}

TEST(ModMatrixKeyboard, TheAmountSliderNudgesWithUpDownAndPassesLeftRightOn) {
    TwoRows t;
    auto* slider = dynamic_cast<juce::Slider*>(t.cell(0, ids::kAmount));
    ASSERT_NE(slider, nullptr);
    EXPECT_TRUE(slider->getWantsKeyboardFocus()) << "juce::Slider is not focusable by default";
    EXPECT_FALSE(slider->keyPressed(kLeft));
    EXPECT_FALSE(slider->keyPressed(kRight));
    const double before = slider->getValue();
    EXPECT_TRUE(slider->keyPressed(kDown));
    EXPECT_LT(slider->getValue(), before);
}

TEST(ModMatrixKeyboard, EveryRowControlNamesItsRoutingForScreenReaders) {
    TwoRows t;
    EXPECT_EQ(t.cell(0, ids::kSource)->getTitle(), "Routing 1 source");
    EXPECT_EQ(t.cell(1, ids::kDest)->getTitle(), "Routing 2 destination");
    EXPECT_EQ(t.cell(1, ids::kAmount)->getTitle(), "Routing 2 amount");
    EXPECT_EQ(t.cell(0, ids::kBypass)->getTitle(), "Routing 1 bypass");
    EXPECT_EQ(t.cell(1, ids::kDelete)->getTitle(), "Remove routing 2");
    for (const char* id : {ids::kAdd, ids::kFlat}) {
        auto* tip = dynamic_cast<juce::TooltipClient*>(t.header(id));
        ASSERT_NE(tip, nullptr) << id;
        EXPECT_TRUE(tip->getTooltip().isNotEmpty()) << id;
    }
}

TEST(ModMatrixKeyboard, TheRowListIsNotAStrayTabStop) {
    TwoRows t;
    auto* viewport = t.cell(0, ids::kSource)->findParentComponentOfClass<juce::Viewport>();
    ASSERT_NE(viewport, nullptr);
    EXPECT_FALSE(viewport->getWantsKeyboardFocus());
}
