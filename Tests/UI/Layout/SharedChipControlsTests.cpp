// SharedChipControlsTests.cpp -- the small controls every surface shares: the chip, the piano-key
// toggle, the colour swatch, the text link and the side-pane toggle (an icon button). Each is painted
// from theme tokens in one place, so these tests paint into software images and read the pixels back.
// Images use SoftwareImageType(): the default native image type reads back zeros on a Windows runner.

#include "UI/Layout/ColourSwatchButton.h"
#include "UI/Layout/SidePane/SidePaneToggleButton.h"
#include "UI/Layout/TextLinkButton.h"
#include "UI/PianoRoll/ScaleAssistPanel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::theme::AppLookAndFeel;
using synth::theme::ChipState;
using synth::theme::Theme;
using synth::ui::ScaleAssistPanel;

juce::Image newImage(int w, int h) { return juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType()); }

juce::Image render(juce::Component& component, int w, int h) {
    component.setSize(w, h);
    auto image = newImage(w, h);
    juce::Graphics g(image);
    component.paintEntireComponent(g, false);
    return image;
}

bool identical(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

void expectColourNear(juce::Colour actual, juce::Colour expected, int tolerance, const char* what) {
    EXPECT_NEAR(actual.getRed(), expected.getRed(), tolerance) << what;
    EXPECT_NEAR(actual.getGreen(), expected.getGreen(), tolerance) << what;
    EXPECT_NEAR(actual.getBlue(), expected.getBlue(), tolerance) << what;
}

// The built-in themes the tests run over: a dark one and the light one.
std::vector<Theme> darkAndLightThemes() { return {synth::theme::makeObsidian(), synth::theme::makeDaylight()}; }

// Buttons the test can put into a hover or press state, standing in for the live pointer.
template <typename Base>
class Hoverable : public Base {
public:
    using Base::Base;
    void hover() { this->setState(juce::Button::buttonOver); }
    void press() { this->setState(juce::Button::buttonDown); }
};

// ---- chip ------------------------------------------------------------------------------------

juce::Image paintChipImage(const Theme& theme, const ChipState& state, juce::Colour* fillOut = nullptr) {
    auto image = newImage(40, 20);
    juce::Graphics g(image);
    const auto fill = synth::theme::paintChip(g, {0.0f, 0.0f, 40.0f, 20.0f}, theme, state);
    if (fillOut != nullptr)
        *fillOut = fill;
    return image;
}

TEST(SharedChipControlsTest, ChipFillIsTheSurfaceAndBrightensOnHoverAndPress) {
    for (const auto& theme : darkAndLightThemes()) {
        juce::Colour rest, hover, down;
        const auto restImage = paintChipImage(theme, {}, &rest);
        const auto hoverImage = paintChipImage(theme, {.hovered = true}, &hover);
        const auto downImage = paintChipImage(theme, {.hovered = true, .down = true}, &down);

        EXPECT_EQ(rest, theme.colors.surface) << theme.name;
        EXPECT_EQ(restImage.getPixelAt(20, 10), theme.colors.surface) << theme.name;
        const bool light = theme.colors.surface.getPerceivedBrightness() > 0.7f;
        EXPECT_EQ(hover, light ? theme.colors.surface.darker(0.12f) : theme.colors.surface.brighter(0.12f))
            << theme.name;
        EXPECT_EQ(hoverImage.getPixelAt(20, 10), hover) << theme.name;
        EXPECT_FALSE(identical(restImage, hoverImage)) << theme.name;
        EXPECT_FALSE(identical(hoverImage, downImage)) << theme.name;
        EXPECT_EQ(downImage.getPixelAt(20, 10), down) << theme.name;
    }
}

TEST(SharedChipControlsTest, RaisedAndActiveChipsUseTheirOwnTokens) {
    const Theme theme = synth::theme::makeObsidian();
    juce::Colour fill;
    paintChipImage(theme, {.raised = true}, &fill);
    EXPECT_EQ(fill, theme.colors.surfaceHi);
    paintChipImage(theme, {.active = true, .raised = true}, &fill);
    EXPECT_EQ(fill, theme.colors.toolActive);
}

TEST(SharedChipControlsTest, ChipBorderIsTheBorderTokenAndWarningTurnsItIntoTheWarningColour) {
    for (const auto& theme : darkAndLightThemes()) {
        juce::Colour fill;
        const auto plain = paintChipImage(theme, {}, &fill);
        const auto warned = paintChipImage(theme, {.warning = true});

        // Row 0 is the border stroke's own row; the fill sits beneath it.
        expectColourNear(plain.getPixelAt(20, 0), theme.colors.border, 3, theme.name.toRawUTF8());
        expectColourNear(warned.getPixelAt(20, 0), fill.overlaidWith(theme.colors.warning.withAlpha(0.7f)), 3,
                         theme.name.toRawUTF8());
        EXPECT_FALSE(identical(plain, warned)) << theme.name;
    }
}

TEST(SharedChipControlsTest, TheLookAndFeelDrawsTheSameChipAsTheFreeFunction) {
    AppLookAndFeel lf;
    const Theme theme = synth::theme::makeDaylight();
    lf.applyTheme(theme);
    auto image = newImage(40, 20);
    juce::Graphics g(image);
    const auto fill = lf.drawChip(g, {0.0f, 0.0f, 40.0f, 20.0f}, {.hovered = true});
    EXPECT_EQ(fill, theme.colors.surface.darker(0.12f)) << "a light chip darkens on hover";
    EXPECT_TRUE(identical(image, paintChipImage(theme, {.hovered = true})));
}

// ---- piano-key toggle ------------------------------------------------------------------------

juce::Image paintKey(ScaleAssistPanel& panel, int pitchClass, bool on, AppLookAndFeel* lf) {
    auto& toggle = panel.getCustomPitchToggle(pitchClass);
    toggle.setLookAndFeel(lf);
    toggle.setToggleState(on, juce::dontSendNotification);
    auto image = render(toggle, 32, 16);
    toggle.setToggleState(false, juce::dontSendNotification);
    toggle.setLookAndFeel(nullptr);
    return image;
}

TEST(SharedChipControlsTest, PianoKeyTogglesPaintTheThemesKeyAndAccentColoursInEveryTheme) {
    for (const auto& theme : darkAndLightThemes()) {
        AppLookAndFeel lf;
        lf.applyTheme(theme);
        ScaleAssistPanel panel;
        // C is a white key, C sharp a black one; (3, 8) is inside the fill and clear of the note name.
        const auto white = paintKey(panel, 0, false, &lf);
        const auto black = paintKey(panel, 1, false, &lf);
        const auto lit = paintKey(panel, 1, true, &lf);
        EXPECT_EQ(white.getPixelAt(3, 8), theme.colors.pianoKeyWhite) << theme.name;
        EXPECT_EQ(black.getPixelAt(3, 8), theme.colors.pianoKeyBlack) << theme.name;
        EXPECT_EQ(lit.getPixelAt(3, 8), theme.colors.accent) << theme.name;
    }
}

TEST(SharedChipControlsTest, ThemesDifferInTheirKeysSoNoColourIsHardCoded) {
    AppLookAndFeel dark, light;
    dark.applyTheme(synth::theme::makeObsidian());
    light.applyTheme(synth::theme::makeDaylight());
    ScaleAssistPanel panel;
    EXPECT_NE(paintKey(panel, 0, false, &dark).getPixelAt(3, 8), paintKey(panel, 0, false, &light).getPixelAt(3, 8));
    EXPECT_NE(paintKey(panel, 1, false, &dark).getPixelAt(3, 8), paintKey(panel, 1, false, &light).getPixelAt(3, 8));
}

TEST(SharedChipControlsTest, PianoKeyTogglesOutsideAnyLookAndFeelFollowTheDefaultTheme) {
    const Theme theme;
    ScaleAssistPanel panel;
    EXPECT_EQ(paintKey(panel, 0, false, nullptr).getPixelAt(3, 8), theme.colors.pianoKeyWhite);
    EXPECT_EQ(paintKey(panel, 1, false, nullptr).getPixelAt(3, 8), theme.colors.pianoKeyBlack);
    EXPECT_EQ(paintKey(panel, 1, true, nullptr).getPixelAt(3, 8), theme.colors.accent);
}

TEST(SharedChipControlsTest, PianoKeyHoverWashComesFromTheTextToken) {
    AppLookAndFeel lf;
    const Theme theme = synth::theme::makeObsidian();
    lf.applyTheme(theme);
    Hoverable<juce::ToggleButton> button;
    button.setLookAndFeel(&lf);
    // A black key, so the light wash shows against its dark fill.
    auto paintBlack = [&](bool hover) {
        auto image = newImage(32, 16);
        juce::Graphics g(image);
        button.setSize(32, 16);
        if (hover)
            button.hover();
        lf.drawKeyToggle(g, button, true, hover, false);
        return image;
    };
    const auto base = paintBlack(false);
    const auto washed = paintBlack(true);
    EXPECT_EQ(base.getPixelAt(3, 8), theme.colors.pianoKeyBlack);
    expectColourNear(washed.getPixelAt(3, 8),
                     theme.colors.pianoKeyBlack.overlaidWith(theme.colors.textPrimary.withAlpha(0.10f)), 3, "wash");
    button.setLookAndFeel(nullptr);
}

// ---- colour swatch ---------------------------------------------------------------------------

TEST(SharedChipControlsTest, ASwatchPaintsItsColourInEveryThemeAndOutsideAnyLookAndFeel) {
    for (const auto& theme : darkAndLightThemes()) {
        AppLookAndFeel lf;
        lf.applyTheme(theme);
        synth::ui::ColourSwatchButton swatch("s");
        swatch.colour = juce::Colour(0xffC0392B);
        swatch.setLookAndFeel(&lf);
        EXPECT_EQ(render(swatch, 24, 24).getPixelAt(12, 12), juce::Colour(0xffC0392B)) << theme.name;
        swatch.setLookAndFeel(nullptr);
    }
    synth::ui::ColourSwatchButton bare("s");
    bare.colour = juce::Colour(0xff2980B9);
    EXPECT_EQ(render(bare, 24, 24).getPixelAt(12, 12), juce::Colour(0xff2980B9));
}

TEST(SharedChipControlsTest, ASwatchBordersInTheBorderTokenAndRingsOnHover) {
    AppLookAndFeel lf;
    const Theme theme = synth::theme::makeObsidian();
    lf.applyTheme(theme);
    Hoverable<synth::ui::ColourSwatchButton> swatch("s");
    swatch.colour = juce::Colour(0xff27AE60);
    swatch.setLookAndFeel(&lf);
    const auto rest = render(swatch, 24, 24);
    // The border is stroked on the 1 px inset rectangle: half of column 1 is border over the fill.
    expectColourNear(rest.getPixelAt(1, 12), swatch.colour.interpolatedWith(theme.colors.border, 0.5f), 6,
                     "rest border");

    swatch.hover();
    const auto hovered = render(swatch, 24, 24);
    EXPECT_FALSE(identical(rest, hovered));
    EXPECT_NE(hovered.getPixelAt(12, 12), rest.getPixelAt(12, 12)) << "the fill brightens";
    swatch.setLookAndFeel(nullptr);
}

TEST(SharedChipControlsTest, ASwatchDrawsTheAccentFocusRingOnRequest) {
    AppLookAndFeel lf;
    const Theme theme = synth::theme::makeObsidian();
    lf.applyTheme(theme);
    synth::ui::ColourSwatchButton swatch("s");
    swatch.colour = juce::Colour(0xff8E44AD);
    swatch.setLookAndFeel(&lf);
    const auto without = render(swatch, 24, 24);
    swatch.forceFocusRingForTest = true;
    const auto with = render(swatch, 24, 24);
    EXPECT_FALSE(identical(without, with));
    const auto edge = with.getPixelAt(0, 12);
    EXPECT_GT(edge.getAlpha(), 200);
    expectColourNear(edge, theme.colors.accent, 4, "focus ring");
    swatch.setLookAndFeel(nullptr);
}

TEST(SharedChipControlsTest, ARightClickOnASwatchFiresOnRightClickAndNeverOnClick) {
    synth::ui::ColourSwatchButton swatch("s");
    swatch.setSize(24, 24);
    int rights = 0, clicks = 0;
    swatch.onRightClick = [&] { ++rights; };
    swatch.onClick = [&] { ++clicks; };
    const auto event = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {5.0f, 5.0f},
                                        juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 0.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, &swatch, &swatch, juce::Time::getCurrentTime(), {5.0f, 5.0f},
                                        juce::Time::getCurrentTime(), 1, false);
    swatch.mouseDown(event);
    EXPECT_EQ(rights, 1);
    EXPECT_EQ(clicks, 0);
}

// ---- text link -------------------------------------------------------------------------------

// The number of pixels with any ink in `row`.
int inkInRow(const juce::Image& image, int row) {
    int count = 0;
    for (int x = 0; x < image.getWidth(); ++x)
        if (image.getPixelAt(x, row).getAlpha() > 0)
            ++count;
    return count;
}

TEST(SharedChipControlsTest, ALinkUnderlinesOnHoverOnly) {
    AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    Hoverable<synth::ui::TextLinkButton> link("Show all");
    link.setLookAndFeel(&lf);
    const auto rest = render(link, 80, 20);
    link.hover();
    const auto hovered = render(link, 80, 20);
    EXPECT_FALSE(identical(rest, hovered));

    // The underline is a full-width 1 px row just under the text: far more ink in hover than at rest.
    int bestGain = 0;
    for (int y = 0; y < 20; ++y)
        bestGain = std::max(bestGain, inkInRow(hovered, y) - inkInRow(rest, y));
    const float textWidth = juce::Font(juce::FontOptions(11.0f)).getStringWidthFloat("Show all");
    EXPECT_GE(bestGain, (int)textWidth - 2);
    link.setLookAndFeel(nullptr);
}

TEST(SharedChipControlsTest, TheUnderlineFollowsTheJustification) {
    AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    Hoverable<synth::ui::TextLinkButton> left("Show all", juce::Justification::centredLeft);
    Hoverable<synth::ui::TextLinkButton> right("Show all", juce::Justification::centredRight);
    for (auto* link : {&left, &right}) {
        link->setLookAndFeel(&lf);
        link->hover();
    }
    const auto leftImage = render(left, 80, 20);
    const auto rightImage = render(right, 80, 20);

    auto inkInColumn = [](const juce::Image& image, int x) {
        int count = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            if (image.getPixelAt(x, y).getAlpha() > 0)
                ++count;
        return count;
    };
    EXPECT_GT(inkInColumn(leftImage, 0), 0) << "left-justified text starts at the left edge";
    EXPECT_EQ(inkInColumn(leftImage, 79), 0);
    EXPECT_GT(inkInColumn(rightImage, 79), 0) << "right-justified text ends at the right edge";
    EXPECT_EQ(inkInColumn(rightImage, 0), 0);
    for (auto* link : {&left, &right})
        link->setLookAndFeel(nullptr);
}

TEST(SharedChipControlsTest, ADisabledLinkIsDimAndNeverUnderlines) {
    AppLookAndFeel lf;
    const Theme theme = synth::theme::makeObsidian();
    lf.applyTheme(theme);
    Hoverable<synth::ui::TextLinkButton> link("Show all");
    link.setLookAndFeel(&lf);
    const auto enabled = render(link, 80, 20);
    link.setEnabled(false);
    const auto disabled = render(link, 80, 20);
    link.hover();
    const auto disabledHover = render(link, 80, 20);
    EXPECT_TRUE(identical(disabled, disabledHover)) << "no hover treatment while disabled";

    int maxAlphaEnabled = 0, maxAlphaDisabled = 0;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 80; ++x) {
            maxAlphaEnabled = std::max<int>(maxAlphaEnabled, enabled.getPixelAt(x, y).getAlpha());
            maxAlphaDisabled = std::max<int>(maxAlphaDisabled, disabled.getPixelAt(x, y).getAlpha());
        }
    EXPECT_LT(maxAlphaDisabled, maxAlphaEnabled);
    link.setLookAndFeel(nullptr);
}

TEST(SharedChipControlsTest, ALinkKeepsItsTextAsTheAccessibleTitleAndPaintsWithoutALookAndFeel) {
    synth::ui::TextLinkButton link("Show on canvas");
    EXPECT_EQ(link.getTitle(), "Show on canvas");
    EXPECT_GT(inkInRow(render(link, 120, 20), 10), 0);
}

// ---- side-pane toggle ------------------------------------------------------------------------

TEST(SharedChipControlsTest, TheSidePaneToggleIsABareIconButtonThatSwapsItsGlyphWithThePane) {
    synth::ui::SidePaneToggleButton button;
    EXPECT_EQ(button.getStyle(), synth::ui::IconButton::Style::Bare);
    EXPECT_EQ(button.currentGlyph(), synth::theme::Glyph::SidePane);

    AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    button.setLookAndFeel(&lf);
    const auto closed = render(button, 28, 28);
    button.setToggleState(true, juce::dontSendNotification);
    EXPECT_EQ(button.currentGlyph(), synth::theme::Glyph::SidePaneOpen);
    const auto open = render(button, 28, 28);
    EXPECT_FALSE(identical(closed, open));
    // Open: the left strip is filled in the accent colour; closed: that spot is empty.
    EXPECT_EQ(closed.getPixelAt(10, 14).getAlpha(), 0);
    expectColourNear(open.getPixelAt(10, 14), lf.getTheme().colors.accent, 4, "filled strip");
    button.setLookAndFeel(nullptr);
}

TEST(SharedChipControlsTest, TheSidePaneToggleKeepsItsAccessibleTextAndFocusRing) {
    synth::ui::SidePaneToggleButton button;
    EXPECT_EQ(button.getTitle(), "Side pane");
    EXPECT_FALSE(button.getDescription().isEmpty());
    EXPECT_TRUE(button.getWantsKeyboardFocus());

    AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    button.setLookAndFeel(&lf);
    const auto without = render(button, 28, 28);
    button.forceFocusRingForTest = true;
    EXPECT_FALSE(identical(without, render(button, 28, 28)));
    button.setLookAndFeel(nullptr);
}

} // namespace
