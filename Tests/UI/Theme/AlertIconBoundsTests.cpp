// Alert box icon placement and colour: a title-height square left of the text, centred on the title's first line,
// wholly inside the window, warning-coloured for a warning.

#include "UI/Theme/AppLookAndFeel/AlertIconBounds.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using juce::Rectangle;

// The layout JUCE's AlertWindow builds: the title in a slightly larger bold font, a blank line, the message.
juce::TextLayout alertLayout(const juce::String& title, const juce::String& message, float width) {
    const juce::Font messageFont(juce::FontOptions(15.0f));
    juce::AttributedString text;
    text.append(title, messageFont.withHeight(16.5f).boldened());
    if (message.isNotEmpty())
        text.append("\n\n" + message, messageFont);
    text.setJustification(juce::Justification::topLeft);
    juce::TextLayout layout;
    layout.createLayout(text, width);
    return layout;
}

float centreOf(juce::Range<float> span) { return span.getStart() + span.getLength() / 2.0f; }

TEST(AlertIconBoundsTest, TheIconIsCentredOnTheTitleLineLeftOfTheTextAndInsideTheWindow) {
    const juce::String longMessage = "The plug-in stopped responding and was removed from the project. Your other "
                                     "tracks keep playing; save a copy before you reopen it.";
    struct Case {
        juce::String title, message;
        float width;
        Rectangle<int> window;
    };
    const std::vector<Case> cases = {
        {"Delete preset?", "This cannot be undone.", 260.0f, {0, 0, 350, 140}},
        {"Plug-in crashed", longMessage, 300.0f, {0, 0, 420, 220}},
        {"A title long enough to wrap onto a second line of the alert", longMessage, 220.0f, {0, 0, 350, 260}},
        {"Title only", {}, 260.0f, {0, 0, 350, 110}},
        {"Offset window", "A message.", 260.0f, {40, 25, 350, 140}},
    };
    for (const auto& c : cases) {
        const auto layout = alertLayout(c.title, c.message, c.width);
        const auto icon = synth::ui::alertIconBounds(c.window, layout);
        const auto title = synth::ui::alertTitleLineSpan(layout);
        EXPECT_EQ(icon.getWidth(), synth::ui::kAlertIconSize) << c.title;
        EXPECT_EQ(icon.getHeight(), synth::ui::kAlertIconSize) << c.title;
        EXPECT_TRUE(c.window.contains(icon)) << c.title << ": " << icon.toString();
        EXPECT_EQ(c.window.getX() + synth::ui::kAlertTextLeft - icon.getRight(), synth::ui::kAlertIconGap) << c.title;
        EXPECT_NEAR(icon.toFloat().getCentreY() - (float)c.window.getY(), centreOf(title), 1.0f)
            << c.title << ": the icon is not level with its title";
        EXPECT_GT(title.getLength(), 10.0f) << c.title << ": the first line is the title, not empty";
        EXPECT_LT(title.getLength(), 30.0f) << c.title << ": only the title's first line counts";
    }
}

TEST(AlertIconBoundsTest, TheIconIsAboutTwiceTheTitleHeight) {
    const auto title = synth::ui::alertTitleLineSpan(alertLayout("Delete preset?", "Gone for good.", 260.0f));
    EXPECT_GE((float)synth::ui::kAlertIconSize, title.getLength() * 1.6f);
    EXPECT_LE((float)synth::ui::kAlertIconSize, title.getLength() * 2.4f);
}

TEST(AlertIconBoundsTest, ATinyWindowStillHoldsTheIcon) {
    const Rectangle<int> window(0, 0, 120, 60);
    EXPECT_TRUE(window.contains(synth::ui::alertIconBounds(window, alertLayout("T", "m", 40.0f))));
    EXPECT_TRUE(window.contains(synth::ui::alertIconBounds(window, juce::TextLayout{})));
}

TEST(AlertIconBoundsTest, AWarningTakesTheWarningColourAndTheOthersTheAccent) {
    const juce::Colour accent(0xff00D1FF), warning(0xffE0A33D), dark(0xff1B1F26);
    EXPECT_EQ(synth::ui::alertIconColour(juce::MessageBoxIconType::WarningIcon, accent, warning, dark), warning);
    EXPECT_EQ(synth::ui::alertIconColour(juce::MessageBoxIconType::InfoIcon, accent, warning, dark), accent);
    EXPECT_EQ(synth::ui::alertIconColour(juce::MessageBoxIconType::QuestionIcon, accent, warning, dark), accent);
}

TEST(AlertIconBoundsTest, APaleWarningIsDarkenedOnALightSurface) {
    const juce::Colour orange(0xffE67E22), white(0xffFFFFFF);
    const auto drawn = synth::ui::alertIconColour(juce::MessageBoxIconType::WarningIcon, orange, orange, white);
    EXPECT_NE(drawn, orange);
    EXPECT_LT(drawn.getPerceivedBrightness(), orange.getPerceivedBrightness());
    EXPECT_GT(drawn.getHue(), orange.getHue() - 0.05f) << "it stays orange, only darker";
}

} // namespace
