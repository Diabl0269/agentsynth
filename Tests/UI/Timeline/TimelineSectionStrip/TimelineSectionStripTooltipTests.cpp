// TimelineSectionStripTooltipTests.cpp -- the Sections strip's tip lands below the whole row, so it never covers the
// neighbouring block's name: for a hover over every block and over a variation chip's end of a block.
#include "TimelineSectionStripThreeBlocksFixture.h"
#include "UI/Layout/AppTooltipWindow.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {
struct PointerReset {
    ~PointerReset() { synth::ui::AppTooltipWindow::setSyntheticPointer(std::nullopt); }
};
} // namespace

TEST(TimelineSectionStripTooltipTest, TheStripAsksForItsWholeRowToBeKeptClear) {
    ThreeBlocks f;
    EXPECT_EQ(f.strip.tooltipAvoidRect(), f.strip.getLocalBounds());
}

TEST(TimelineSectionStripTooltipTest, TheTipIsClearOfEveryBlockWhereverThePointerRests) {
    PointerReset reset;
    ThreeBlocks f;
    juce::Component parent;
    parent.setSize(1300, 200);
    parent.addAndMakeVisible(f.strip);
    f.strip.setBounds(0, 40, 1200, 24);
    synth::ui::AppTooltipWindow window(&parent);
    window.setTipComponentForTest(&f.strip);
    // Over the middle and over the right end (where the chip sits) of each block, and over the empty strip.
    for (const double beat : {1.0, 4.0, 7.5, 8.5, 10.0, 11.8, 12.5, 14.0, 15.8, 20.0}) {
        const juce::Point<int> pointer(f.strip.getX() + (int)f.x(beat), f.strip.getY() + 12);
        const auto tip = f.strip.tooltipAt({f.x(beat), 12.0f});
        ASSERT_FALSE(tip.isEmpty());
        window.setLastTipForTest(tip);
        synth::ui::AppTooltipWindow::setSyntheticPointer(parent.localPointToGlobal(pointer));
        window.displayTip(parent.localPointToGlobal(pointer), tip);
        EXPECT_FALSE(window.getBounds().intersects(f.strip.getBounds())) << "beat " << beat;
        window.hideTip();
    }
    parent.removeChildComponent(&f.strip);
}
