#include "AppLookAndFeel.h"
#include "UI/Layout/ColourSwatchButton.h"
#include "UI/Layout/FocusRing.h"
#include <algorithm>

namespace synth::theme {

// Concern: the small shared controls that have no stock JUCE draw method: chips, piano-key toggles,
// colour swatches and text links. Each has a free paint function taking a Theme (so a control outside
// any AppLookAndFeel paints with a default-constructed one) and an AppLookAndFeel member calling it.

const Theme& themeOf(const juce::Component& comp) {
    static const Theme defaultTheme;
    if (const auto* lf = dynamic_cast<const AppLookAndFeel*>(&comp.getLookAndFeel()))
        return lf->getTheme();
    return defaultTheme;
}

namespace {
constexpr float kChipHoverShift = 0.12f;
constexpr float kChipPressShift = 0.25f;
constexpr float kChipLightFill = 0.7f;
constexpr float kChipWarningBorderAlpha = 0.7f;
constexpr float kKeyHoverWashAlpha = 0.10f;
constexpr float kKeyPressWashAlpha = 0.20f;
constexpr float kSwatchHoverBrighten = 0.2f;
constexpr float kSwatchPressDarken = 0.15f;
constexpr float kSwatchHoverRingAlpha = 0.6f;
constexpr float kSwatchHoverRingWidth = 1.5f;
constexpr float kTextLinkFontHeight = 11.0f;
constexpr float kTextLinkHoverBrighten = 0.3f;
constexpr float kTextLinkDisabledAlpha = 0.35f;
} // namespace

juce::Colour paintChip(juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme, const ChipState& state) {
    const auto& c = theme.colors;
    const float radius = theme.metrics.cornerRadiusSmall;
    juce::Colour fill = state.active ? c.toolActive : (state.raised ? c.surfaceHi : c.surface);
    // Hover and press push the fill away from its own lightness, so a white chip on a light theme still
    // shows them (brighter() would leave it white).
    const bool light = fill.getPerceivedBrightness() > kChipLightFill;
    const float amount = state.down ? kChipPressShift : (state.hovered ? kChipHoverShift : 0.0f);
    fill = light ? fill.darker(amount) : fill.brighter(amount);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(state.warning ? c.warning.withAlpha(kChipWarningBorderAlpha) : c.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, theme.metrics.borderWidth);
    return fill;
}

juce::Colour AppLookAndFeel::drawChip(juce::Graphics& g, juce::Rectangle<float> bounds, const ChipState& state) {
    return paintChip(g, bounds, theme, state);
}

void paintKeyToggle(juce::Graphics& g, juce::ToggleButton& button, const Theme& theme, bool isBlackKey,
                    bool highlighted, bool down) {
    const auto& c = theme.colors;
    const float radius = theme.metrics.cornerRadiusSmall;
    const auto keyFill = isBlackKey ? c.pianoKeyBlack : c.pianoKeyWhite;
    const bool on = button.getToggleState();
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    g.setColour(on ? c.accent : keyFill);
    g.fillRoundedRectangle(bounds, radius);
    if (down || highlighted) {
        g.setColour(c.textPrimary.withAlpha(down ? kKeyPressWashAlpha : kKeyHoverWashAlpha));
        g.fillRoundedRectangle(bounds, radius);
    }
    g.setColour(c.border);
    g.drawRoundedRectangle(bounds, radius, theme.metrics.borderWidth);

    // .contrasting so the note name holds against either key fill and against the accent once on, in
    // every theme.
    g.setColour((on ? c.accent : keyFill).contrasting(0.9f));
    g.setFont(juce::Font(juce::FontOptions(juce::jlimit(7.0f, 10.0f, bounds.getHeight() - 6.0f))));
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
    synth::ui::paintFocusRing(g, button.getLocalBounds().toFloat(), button, radius);
}

void AppLookAndFeel::drawKeyToggle(juce::Graphics& g, juce::ToggleButton& button, bool isBlackKey,
                                   bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    paintKeyToggle(g, button, theme, isBlackKey, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

void paintColourSwatch(juce::Graphics& g, const synth::ui::ColourSwatchButton& button, const Theme& theme,
                       bool highlighted, bool down) {
    const auto& c = theme.colors;
    const float radius = theme.metrics.cornerRadiusSmall;
    const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const auto colour = button.colour;
    const bool hot = highlighted || down;

    g.setColour(down ? colour.darker(kSwatchPressDarken)
                     : (highlighted ? colour.brighter(kSwatchHoverBrighten) : colour));
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(hot ? c.textPrimary.withAlpha(kSwatchHoverRingAlpha) : c.border);
    g.drawRoundedRectangle(bounds, radius, hot ? kSwatchHoverRingWidth : theme.metrics.borderWidth);
    (button.forceFocusRingForTest ? synth::ui::paintFocusRingAlways
                                  : synth::ui::paintFocusRing)(g, button.getLocalBounds().toFloat(), button, radius);
}

void AppLookAndFeel::drawColourSwatch(juce::Graphics& g, synth::ui::ColourSwatchButton& button,
                                      bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    paintColourSwatch(g, button, theme, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

void paintTextLink(juce::Graphics& g, juce::Button& button, const Theme& theme, juce::Justification justification,
                   bool highlighted) {
    const auto& accent = theme.colors.accent;
    const bool enabled = button.isEnabled();
    const bool hot = enabled && highlighted;
    const juce::Font font{juce::FontOptions(kTextLinkFontHeight)};
    const auto bounds = button.getLocalBounds();

    g.setColour(!enabled ? accent.withAlpha(kTextLinkDisabledAlpha)
                         : (hot ? accent.brighter(kTextLinkHoverBrighten) : accent));
    g.setFont(font);
    g.drawText(button.getButtonText(), bounds, justification, false);
    if (!hot)
        return;

    const float width = std::min(font.getStringWidthFloat(button.getButtonText()), (float)bounds.getWidth());
    const float x = justification.testFlags(juce::Justification::right)
                        ? (float)bounds.getRight() - width
                        : (justification.testFlags(juce::Justification::horizontallyCentred)
                               ? (float)bounds.getCentreX() - width * 0.5f
                               : (float)bounds.getX());
    g.fillRect(x, (float)bounds.getCentreY() + font.getHeight() * 0.5f, width, 1.0f);
}

void AppLookAndFeel::drawTextLink(juce::Graphics& g, juce::Button& button, juce::Justification justification,
                                  bool shouldDrawButtonAsHighlighted) {
    paintTextLink(g, button, theme, justification, shouldDrawButtonAsHighlighted);
}

} // namespace synth::theme
