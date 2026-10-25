#include "AppLookAndFeel.h"
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Layout/FocusRing.h"

namespace synth::theme {

// Concern: the top bar's button (synth::ui::ToolbarButton): ground, group-coloured chip, multi-role
// glyph and its hover motion, caption. docs/layout/chrome.md#toolbar.

namespace {
constexpr float kChipTop = 4.0f;
constexpr float kIconGrid = 24.0f; // the SVGs' viewBox
constexpr float kCaptionGap = 2.0f;
constexpr float kCaptionHeight = 13.0f;
// The faint pill behind a lit inline button's chip and caption, as a share of the group colour.
constexpr float kInlinePillAlpha = 0.12f;
constexpr float kInlinePillRadius = 7.0f;
constexpr float kCaptionSideInset = 2.0f;
// The chip's group-colour alpha at rest and on hover: dark themes, then light ones (where a stronger
// tint would cost the icon its 3:1 against the chip).
constexpr float kChipRestDark = 0.16f;
constexpr float kChipHoverDark = 0.26f;
constexpr float kChipRestLight = 0.10f;
constexpr float kChipHoverLight = 0.16f;
constexpr float kGroundHoverAlpha = 0.60f;
constexpr float kGroundPressAlpha = 0.85f;
constexpr float kDisabledIconAlpha = 0.40f;
} // namespace

// Stacked, with a caption, the chip sits 4 px from the top and the caption 2 px under it; without one (narrow
// mode drops the text) the chip is centred in the button. Inline, the chip is centred vertically and sits at the
// left with a caption, or in the middle without one.
juce::Rectangle<float> toolbarChipBounds(const synth::ui::ToolbarButton& button) {
    const auto spec = synth::ui::toolbarChipSpec(button.getLayout());
    const auto full = button.getLocalBounds().toFloat();
    const float middleY = juce::jmax(0.0f, (full.getHeight() - spec.chipHeight) * 0.5f);
    if (button.isInline()) {
        const float x = button.getButtonText().isEmpty() ? full.getCentreX() - spec.chipWidth * 0.5f
                                                         : spec.inlineLeftInset + button.getInlineContentOffset();
        return {x, middleY, spec.chipWidth, spec.chipHeight};
    }
    const float top = button.getButtonText().isNotEmpty() ? kChipTop : middleY;
    return {full.getCentreX() - spec.chipWidth * 0.5f, top, spec.chipWidth, spec.chipHeight};
}

juce::AffineTransform toolbarIconTransform(const synth::ui::ToolbarButton& button) {
    const auto chip = toolbarChipBounds(button);
    const float glyph = synth::ui::toolbarChipSpec(button.getLayout()).glyphSize;
    return juce::AffineTransform::scale(glyph / kIconGrid)
        .translated(chip.getCentreX() - glyph * 0.5f, chip.getCentreY() - glyph * 0.5f);
}

namespace {
void paintToolbarGround(juce::Graphics& g, const synth::ui::ToolbarButton& button, const Theme& theme) {
    const float hover = button.getHoverAmount();
    if (!button.isEnabled() || hover <= 0.0f)
        return;
    const float alpha = kGroundHoverAlpha * hover + (kGroundPressAlpha - kGroundHoverAlpha) * button.getPressAmount();
    g.setColour(theme.colors.surfaceHi.withAlpha(alpha));
    g.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(1.0f), theme.metrics.pillRadius);
}

// The chip's alpha eases from rest to hover and then, as a toggle lights, on to solid.
float toolbarChipAlpha(const synth::ui::ToolbarButton& button, const Theme& theme) {
    const float rest = theme.isDark ? kChipRestDark : kChipRestLight;
    const float hover = theme.isDark ? kChipHoverDark : kChipHoverLight;
    const float a = rest + (hover - rest) * button.getHoverAmount();
    return a + (1.0f - a) * button.getLitAmount();
}

void paintToolbarCaption(juce::Graphics& g, const synth::ui::ToolbarButton& button, const Theme& theme,
                         juce::Rectangle<float> chip) {
    const auto text = button.getButtonText();
    if (text.isEmpty())
        return;
    const auto& c = theme.colors;
    const auto spec = synth::ui::toolbarChipSpec(button.getLayout());
    // Captions are never coloured: muted at rest, primary on hover, press and when lit.
    const float hot = juce::jmax(button.getHoverAmount(), button.getLitAmount());
    const auto colour = button.isEnabled() ? c.textMuted.interpolatedWith(c.textPrimary, hot) : c.textDisabled;
    g.setColour(colour);
    g.setFont(AppLookAndFeel::uiSemiBoldFont(spec.captionSize));
    if (button.isInline()) {
        const float left = chip.getRight() + spec.inlineGap;
        const juce::Rectangle<float> area(left, 0.0f, juce::jmax(0.0f, (float)button.getWidth() - left),
                                          (float)button.getHeight());
        g.drawFittedText(text, area.toNearestInt(), juce::Justification::centredLeft, 1);
        return;
    }
    const juce::Rectangle<float> area(kCaptionSideInset, chip.getBottom() + kCaptionGap,
                                      juce::jmax(0.0f, (float)button.getWidth() - 2.0f * kCaptionSideInset),
                                      kCaptionHeight);
    g.drawFittedText(text, area.toNearestInt(), juce::Justification::centred, 1);
}

// A lit captioned inline button sits on a faint pill of its group colour, so the name reads as lit too.
void paintInlineLitPill(juce::Graphics& g, const synth::ui::ToolbarButton& button, const Theme& theme) {
    const float lit = button.isEnabled() ? button.getLitAmount() : 0.0f;
    if (!button.isInline() || button.getButtonText().isEmpty() || lit <= 0.0f)
        return;
    g.setColour(synth::ui::toolbarGroupHue(theme, button.getGroup()).withAlpha(kInlinePillAlpha * lit));
    g.fillRoundedRectangle(button.getLocalBounds().toFloat(), kInlinePillRadius);
}
} // namespace

// The press squash scales chip and glyph together about the chip's centre; the hover lift and the
// per-part motions apply to the glyph only. Rest and lit art cross-fade with the lit tween, so a
// toggle's glyph turns ink while its chip fills.
void paintToolbarButton(juce::Graphics& g, const synth::ui::ToolbarButton& button, const Theme& theme) {
    paintToolbarGround(g, button, theme);
    paintInlineLitPill(g, button, theme);

    const auto chip = toolbarChipBounds(button);
    const auto squash = button.getChipSquash();
    const auto squashTransform = juce::AffineTransform::scale(squash.x, squash.y, chip.getCentreX(), chip.getCentreY());
    const bool enabled = button.isEnabled();

    if (enabled) {
        juce::Path chipPath;
        chipPath.addRoundedRectangle(chip, synth::ui::toolbarChipSpec(button.getLayout()).chipRadius);
        chipPath.applyTransform(squashTransform);
        g.setColour(synth::ui::toolbarGroupHue(theme, button.getGroup()).withAlpha(toolbarChipAlpha(button, theme)));
        g.fillPath(chipPath);
    }

    const auto iconToScreen =
        toolbarIconTransform(button).translated(0.0f, -button.getIconLift()).followedBy(squashTransform);
    const std::array<juce::AffineTransform, 2> parts = {button.getPartTransform(0), button.getPartTransform(1)};
    const float alpha = enabled ? 1.0f : kDisabledIconAlpha;
    const float lit = enabled ? button.getLitAmount() : 0.0f;
    if (lit < 1.0f)
        button.getArt(false).draw(g, iconToScreen, parts, alpha * (1.0f - lit));
    if (lit > 0.0f)
        button.getArt(true).draw(g, iconToScreen, parts, alpha * lit);

    if (button.hasLoopingGlyph()) {
        // Pulse and glow take the amber role, the spark its own pink; a lit chip is solid group colour, so they turn
        // ink.
        const auto& c = theme.colors;
        const juce::Colour spark = theme.isDark ? juce::Colour(0xffff7ab6) : c.hueRose;
        synth::ui::paintToolbarAiParts(g, iconToScreen, button.getLoopFrame(),
                                       {c.hueAmber.interpolatedWith(c.iconInk, lit),
                                        c.hueAmber.interpolatedWith(c.iconInk, lit),
                                        spark.interpolatedWith(c.iconInk, lit)},
                                       alpha);
    }

    paintToolbarCaption(g, button, theme, chip);
    synth::ui::paintFocusRing(g, button.getLocalBounds().toFloat().reduced(1.0f), button, theme.metrics.pillRadius);
}

void AppLookAndFeel::drawToolbarButton(juce::Graphics& g, synth::ui::ToolbarButton& button,
                                       bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/) {
    paintToolbarButton(g, button, theme);
}

} // namespace synth::theme
