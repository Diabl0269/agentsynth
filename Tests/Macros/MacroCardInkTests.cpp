// Guard for the preview-vs-commit kind on a collapsed macro card: the card body is the picked colour at full strength,
// and every ink drawn on it reads on that colour.

#include "UI/Macros/MacroCardComponent/MacroCardInk.h"
#include "UI/Macros/MacroCardComponent/MacroPreviewLayout.h"
#include "UI/Theme/ReadableText.h"
#include <gtest/gtest.h>

namespace {

const juce::Colour kPicks[] = {juce::Colour(0xff00ff3d), juce::Colour(0xffff3b30), juce::Colour(0xff0a1a40),
                               juce::Colour(0xff808080), juce::Colour(0xffffffff), juce::Colour(0xff000000),
                               juce::Colour(0xffffd60a), juce::Colour(0xff7a3cff)};

} // namespace

TEST(MacroCardInk, BodyIsThePickedColourOpaque) {
    for (const auto pick : kPicks) {
        EXPECT_EQ(macro_card::bodyFill(pick), pick.withAlpha(1.0f));
        EXPECT_FLOAT_EQ(macro_card::bodyFill(pick.withAlpha(0.22f)).getFloatAlpha(), 1.0f);
    }
}

TEST(MacroCardInk, InkIsBlackOrWhiteAndReadsOnEveryPick) {
    for (const auto pick : kPicks) {
        const auto ink = macro_card::inkOn(pick);
        EXPECT_TRUE(ink == juce::Colours::black || ink == juce::Colours::white);
        EXPECT_GE(synth::theme::contrastBetween(ink, pick), synth::theme::kReadableTextContrast)
            << pick.toDisplayString(false);
    }
    EXPECT_EQ(macro_card::inkOn(juce::Colour(0xff00ff3d)), juce::Colours::black); // bright green, the reported pick
    EXPECT_EQ(macro_card::inkOn(juce::Colour(0xff0a1a40)), juce::Colours::white);
}

TEST(MacroCardInk, StripTintIsTheOppositeOfTheInk) {
    synth::theme::Colors base{};
    for (const auto pick : kPicks) {
        const auto ink = macro_card::inkOn(pick);
        const auto inked = macro_card::inkedColours(base, ink);
        EXPECT_EQ(inked.textPrimary, ink);
        EXPECT_NE(inked.surfaceHi, ink);
    }
}

TEST(MacroCardInk, PreviewBoxesUseTheInk) {
    EXPECT_EQ(macro_preview::memberBoxColour(juce::Colours::black).withAlpha(1.0f), juce::Colours::black);
    EXPECT_LT(macro_preview::memberBoxColour(juce::Colours::white, 0.5f).getFloatAlpha(),
              macro_preview::memberBoxColour(juce::Colours::white).getFloatAlpha());
}
