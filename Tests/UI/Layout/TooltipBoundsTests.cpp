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

// Regression test for FRO1949: a long tip is wrapped into a short, even block instead of running as one line across the
// window.
TEST_F(TooltipBoundsTest, ALongTipWrapsToTheMaxWidthAndStaysEven) {
    const juce::String longTip =
        "When on (the default), dragging a single module across an expanded macro's border adds it to or removes it "
        "from that macro without holding Cmd, and dropping a module from the library onto a macro adds it there.";
    const juce::Font font(juce::FontOptions(lookAndFeel.getTheme().type.label + 1.0f));
    const auto lines = synth::theme::wrapTooltipLines(longTip, font);
    ASSERT_GE(lines.size(), 3);
    float widest = 0.0f, narrowest = 1.0e9f;
    for (const auto& line : lines) {
        const float w = juce::GlyphArrangement::getStringWidth(font, line);
        widest = juce::jmax(widest, w);
        narrowest = juce::jmin(narrowest, w);
    }
    EXPECT_LE(widest, (float)synth::theme::kTooltipMaxTextWidth);
    EXPECT_GT(narrowest, widest * 0.5f) << "evened out: no stub last line";
    EXPECT_EQ(lines.joinIntoString(" "), longTip) << "wrapping loses no words";
    const auto box = boundsFor(longTip);
    EXPECT_LE(box.getWidth(), synth::theme::kTooltipMaxTextWidth + 14);
    EXPECT_GE(box.getHeight(), (int)std::ceil(font.getHeight()) * lines.size());
}

TEST_F(TooltipBoundsTest, AShortOrExplicitlyBrokenTipIsNotRewrapped) {
    const juce::Font font(juce::FontOptions(lookAndFeel.getTheme().type.label + 1.0f));
    EXPECT_EQ(synth::theme::wrapTooltipLines("Filter 1", font).size(), 1);
    EXPECT_EQ(synth::theme::wrapTooltipLines("A\nB\nC", font).size(), 3);
    EXPECT_EQ(synth::theme::wrapTooltipLines("Supercalifragilisticexpialidocious_with_no_spaces_at_all_so_one_word_"
                                             "that_is_wider_than_the_limit_of_the_box_and_stays_whole_anyway_ok",
                                             font)
                  .size(),
              1);
}

} // namespace
