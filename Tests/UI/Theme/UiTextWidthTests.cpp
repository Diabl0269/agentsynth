// AppLookAndFeel::uiTextWidth measures each (face, height, text) once and answers from memory after that: the answer
// is always the glyph layout's own width, however often and in whatever order it is asked.
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

using synth::theme::AppLookAndFeel;

TEST(UiTextWidthTest, EveryAnswerIsTheMeasuredWidthFirstTimeAndAfter) {
    const juce::StringArray texts{"Cutoff", "Resonance", "", "Sync", "A much longer footer label", "Cutoff"};
    for (const float height : {11.0f, 13.5f, 11.0f})
        for (const auto& text : texts) {
            const int measured = juce::GlyphArrangement::getStringWidthInt(AppLookAndFeel::uiFont(height), text);
            EXPECT_EQ(AppLookAndFeel::uiTextWidth(text, height), measured) << text << " at " << height;
            EXPECT_EQ(AppLookAndFeel::uiTextWidth(text, height), measured) << "remembered: " << text;
        }
}

TEST(UiTextWidthTest, TheSameTextAtAnotherHeightIsMeasuredAgain) {
    EXPECT_LT(AppLookAndFeel::uiTextWidth("Resonance", 9.0f), AppLookAndFeel::uiTextWidth("Resonance", 24.0f));
}
