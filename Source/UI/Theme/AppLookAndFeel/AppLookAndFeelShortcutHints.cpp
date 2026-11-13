#include "AppLookAndFeel.h"

namespace synth::theme {

// Concern: the shortcut key cap -- the small physical-key bubble the Cmd-hold hints (and any
// future shortcut list) draw. One shared function so every place that shows a key looks the same.

// The mono family at the theme's value size plus 2 px (plus 1 in a dock tab, whose strip cannot grow).
// Medium weight: JUCE has no medium flag, but the embedded mono families load their Medium cut for a
// bold request (see getTypefaceForFont), so Font::bold is what selects it.
juce::Font AppLookAndFeel::getShortcutKeyCapFont(bool compact) const {
    const float size = theme.type.value + (compact ? 1.0f : 2.0f);
    return juce::Font(juce::FontOptions(theme.type.monoFamily, size, juce::Font::bold));
}

int AppLookAndFeel::getShortcutKeyCapWidth(const juce::String& text, bool compact) const {
    const int textWidth =
        juce::roundToInt(juce::GlyphArrangement::getStringWidth(getShortcutKeyCapFont(compact), text));
    return juce::jmax(kKeyCapMinWidth, textWidth + 2 * kKeyCapSidePadding);
}

void AppLookAndFeel::drawShortcutKeyCap(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
                                        bool compact) const {
    if (bounds.isEmpty())
        return;

    const auto& c = theme.colors;
    const float radius = theme.metrics.cornerRadiusSmall;
    const auto cap = bounds.toFloat();

    juce::Path shape;
    shape.addRoundedRectangle(cap, radius);

    // Soft shadow first, so the cap reads as sitting above the surface.
    juce::DropShadow(juce::Colours::black.withAlpha(0.5f), kKeyCapShadowRadius, {0, 2}).drawForPath(g, shape);

    g.setColour(c.surfaceHi);
    g.fillRoundedRectangle(cap, radius);

    // A one-pixel outline plus a two-pixel bottom edge (the "key thickness") clipped to the cap's shape,
    // both in textDisabled: on dark themes border is close to the fill and the cap vanished.
    g.saveState();
    g.reduceClipRegion(juce::Rectangle<int>(bounds.getX(), bounds.getBottom() - kKeyCapBottomEdge, bounds.getWidth(),
                                            kKeyCapBottomEdge));
    g.setColour(c.textDisabled);
    g.fillPath(shape);
    g.restoreState();
    g.setColour(c.textDisabled);
    g.drawRoundedRectangle(cap.reduced(0.5f), radius, theme.metrics.borderWidth);

    g.setColour(c.textPrimary);
    g.setFont(getShortcutKeyCapFont(compact));
    g.drawText(text, bounds.withTrimmedBottom(1), juce::Justification::centred, false);
}

} // namespace synth::theme
