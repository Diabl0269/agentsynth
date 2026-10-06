// TooltipBoundsTests.cpp -- the tooltip box fits its whole text: a one-line tip is one line high, a
// list (a macro's modules) is one line per entry, and the width follows the widest line.
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>

namespace {

class TooltipBoundsTest : public ::testing::Test {
protected:
    void SetUp() override { lookAndFeel.applyTheme(synth::theme::makeObsidian()); }

    juce::Rectangle<int> boundsFor(const juce::String& tip) {
        return lookAndFeel.getTooltipBounds(tip, {400, 300}, {0, 0, 1000, 800});
    }

    synth::theme::AppLookAndFeel lookAndFeel;
};

TEST_F(TooltipBoundsTest, ALongerListIsTallerByOneLinePerEntry) {
    const auto one = boundsFor("Filter 1");
    const auto three = boundsFor("Filter 1\nGate 2\nParametric EQ 3");
    const auto five = boundsFor("A\nB\nC\nD\nE");
    const int line = three.getHeight() - one.getHeight();
    EXPECT_GT(line, 0) << "three lines are taller than one";
    EXPECT_EQ(three.getHeight(), one.getHeight() + 2 * (line / 2));
    EXPECT_GT(five.getHeight(), three.getHeight());
}

TEST_F(TooltipBoundsTest, TheWidthFollowsTheWidestLineNotTheWholeText) {
    const auto widest = boundsFor("Parametric EQ 3");
    const auto list = boundsFor("Filter 1\nGate 2\nParametric EQ 3");
    EXPECT_EQ(list.getWidth(), widest.getWidth());
}

TEST_F(TooltipBoundsTest, ALongNameWidensTheBoxInsteadOfClipping) {
    const auto shortTip = boundsFor("Gate 2");
    const auto longTip = boundsFor("Gate 2\nA very long module name that goes on and on");
    EXPECT_GT(longTip.getWidth(), shortTip.getWidth());
}

} // namespace
