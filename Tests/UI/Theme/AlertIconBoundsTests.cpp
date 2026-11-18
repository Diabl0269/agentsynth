// Alert box icon placement and colour: a 64 px disc left of the text, centred on the title and first message
// line, wholly inside the window, warning-coloured for a warning.

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

TEST(AlertIconBoundsTest, TheIconIsCentredUnderTheTitleLeftOfTheTextAndInsideTheWindow) {
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
        EXPECT_NEAR(icon.toFloat().getCentreY() - (float)c.window.getY(), (float)synth::ui::alertIconCentreY(layout),
                    1.0f)
            << c.title << ": the icon is not centred on its heading";
        EXPECT_EQ(synth::ui::kAlertTextLeft - synth::ui::kJuceAlertTextLeft, synth::ui::kAlertColumnGrowth);
        EXPECT_GT(title.getLength(), 10.0f) << c.title << ": the first line is the title, not empty";
        EXPECT_LT(title.getLength(), 30.0f) << c.title << ": only the title's first line counts";
    }
}

TEST(AlertIconBoundsTest, TheIconIsAbout64PxAndAboutThreeTimesTheTitleHeight) {
    const auto title = synth::ui::alertTitleLineSpan(alertLayout("Delete preset?", "Gone for good.", 260.0f));
    EXPECT_EQ(synth::ui::kAlertIconSize, 64);
    EXPECT_GE((float)synth::ui::kAlertIconSize, title.getLength() * 2.5f);
    EXPECT_LE((float)synth::ui::kAlertIconSize, title.getLength() * 4.0f);
    // The sizing estimate used before the layout exists never undershoots the real icon's bottom edge.
    const int bottom = synth::ui::alertIconCentreY(alertLayout("Delete preset?", "Gone for good.", 260.0f)) +
                       synth::ui::kAlertIconSize / 2;
    EXPECT_GE(synth::ui::kAlertIconBottomEstimate, bottom);
    EXPECT_LE(synth::ui::kAlertIconBottomEstimate, bottom + 3);
}

// The span from the title's top to the bottom of the message's first line (line 2, after the blank line).
juce::Range<float> headingSpan(const juce::TextLayout& layout) {
    const auto title = synth::ui::alertTitleLineSpan(layout);
    if (layout.getNumLines() > 2)
        return {title.getStart(), layout.getLine(2).getLineBoundsY().getEnd() + (float)synth::ui::kAlertTextTop};
    return title;
}

// Regression test for FRO1983: the disc sat on the gap under the title, about 8 px above the heading's middle.
TEST(AlertIconBoundsTest, TheDiscIsCentredOnTheTitleAndFirstMessageLine) {
    const auto layout = alertLayout("Delete preset?", "This cannot be undone.", 260.0f);
    const auto span = headingSpan(layout);
    EXPECT_GT(span.getLength(), synth::ui::alertTitleLineSpan(layout).getLength() * 2.0f) << "spans title and message";
    EXPECT_NEAR((float)synth::ui::alertIconCentreY(layout), (span.getStart() + span.getEnd()) * 0.5f, 1.0f);
    EXPECT_GE(synth::ui::alertIconCentreY(layout), synth::ui::alertTitleLineSpan(layout).getEnd() + 4.0f)
        << "lower than the old gap-under-the-title centre";
}

TEST(AlertIconBoundsTest, ATitleOnlyLayoutKeepsTheGapUnderTheTitleCentre) {
    const auto layout = alertLayout("Title only", {}, 260.0f);
    EXPECT_EQ(synth::ui::alertIconCentreY(layout),
              juce::roundToInt(synth::ui::alertTitleLineSpan(layout).getEnd()) - 1);
    EXPECT_EQ(synth::ui::alertIconCentreY(juce::TextLayout{}),
              juce::roundToInt(synth::ui::alertTitleLineSpan(juce::TextLayout{}).getEnd()) - 1);
}

TEST(AlertIconBoundsTest, TheLoweredDiscClearsTheButtonsForOneTwoAndFourMessageLines) {
    const juce::String words = "The plug-in stopped responding and was removed from the project. ";
    for (int messageLines : {1, 2, 4}) {
        juce::String message;
        for (int i = 0; i < messageLines; ++i)
            message += (i ? "\n" : "") + words.substring(0, 30);
        const auto layout = alertLayout("Plug-in crashed", message, 300.0f);
        for (int buttons = 1; buttons <= 3; ++buttons) {
            // JUCE puts the buttons 20 px under the text; the window grows (as makeRoomForAlertIcon does) until the
            // disc clears them by 12 px.
            int buttonTop = (int)(layout.getHeight() + (float)synth::ui::kAlertTextTop) + 20;
            const int bottom = synth::ui::alertIconCentreY(layout) + synth::ui::kAlertIconSize / 2;
            EXPECT_LE(bottom, synth::ui::kAlertIconBottomEstimate + (int)layout.getHeight()) << messageLines;
            buttonTop += juce::jmax(0, synth::ui::kAlertIconBottomEstimate + 12 - buttonTop);
            const Rectangle<int> window(0, 0, 100 * buttons + 200, buttonTop + 40);
            const auto icon = synth::ui::alertIconBounds(window, layout);
            EXPECT_TRUE(window.contains(icon)) << messageLines << " lines, " << buttons << " buttons";
            EXPECT_LE(icon.getBottom() + 12, buttonTop) << messageLines << " lines, " << buttons << " buttons";
        }
    }
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
