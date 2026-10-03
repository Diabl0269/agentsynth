// Where the mod-dot tooltip goes (modDotTooltipRect): above-right of the knob, never over it.

#include "UI/Graph/ModDot/ModDotTooltip.h"
#include <gtest/gtest.h>

using synth::ui::modDotTooltipRect;

namespace {
const juce::Point<float> kSize{90.0f, 18.0f};
const juce::Rectangle<float> kViewport{0.0f, 0.0f, 800.0f, 600.0f};
} // namespace

TEST(ModDotTooltipGeometry, SitsAboveRightOfTheKnobWithATwoPixelGap) {
    const juce::Rectangle<float> knob{200.0f, 200.0f, 60.0f, 70.0f};
    const auto r = modDotTooltipRect(knob, kSize, kViewport);
    EXPECT_FLOAT_EQ(r.getX(), knob.getRight() + 2.0f);
    EXPECT_FLOAT_EQ(r.getBottom(), knob.getY() - 2.0f);
    EXPECT_FALSE(r.intersects(knob));
}

TEST(ModDotTooltipGeometry, StaysInsideTheViewportAtTheRightEdge) {
    const juce::Rectangle<float> knob{700.0f, 200.0f, 60.0f, 70.0f};
    const auto r = modDotTooltipRect(knob, kSize, kViewport);
    EXPECT_TRUE(kViewport.contains(r));
    EXPECT_FALSE(r.intersects(knob));
}

TEST(ModDotTooltipGeometry, DropsBelowRightWhenTheKnobIsAtTheTopEdge) {
    const juce::Rectangle<float> knob{200.0f, 4.0f, 60.0f, 70.0f};
    const auto r = modDotTooltipRect(knob, kSize, kViewport);
    EXPECT_TRUE(kViewport.contains(r));
    EXPECT_FALSE(r.intersects(knob));
}

TEST(ModDotTooltipGeometry, NeverOverlapsTheKnobInTheTopRightCorner) {
    const juce::Rectangle<float> knob{740.0f, 0.0f, 60.0f, 70.0f};
    const auto r = modDotTooltipRect(knob, kSize, kViewport);
    EXPECT_TRUE(kViewport.contains(r));
    EXPECT_FALSE(r.intersects(knob));
}
