#include "AppLookAndFeel.h"

namespace synth::theme {

// Concern: the shortcut key cap -- the small physical-key bubble the Cmd-hold hints (and any
// future shortcut list) draw. One shared function so every place that shows a key looks the same.

juce::Font AppLookAndFeel::getShortcutKeyCapFont() const {
    // The mono value face at the theme's value size. JUCE fonts have no medium weight (see
    // getTypefaceForFont), so the regular cut stands in for the design's weight 500.
    return juce::Font(juce::FontOptions(theme.type.monoFamily, theme.type.value, juce::Font::plain));
}

int AppLookAndFeel::getShortcutKeyCapWidth(const juce::String& text) const {
    const int textWidth = juce::roundToInt(juce::GlyphArrangement::getStringWidth(getShortcutKeyCapFont(), text));
    return juce::jmax(kKeyCapMinWidth, textWidth + 2 * kKeyCapSidePadding);
}

void AppLookAndFeel::drawShortcutKeyCap(juce::Graphics& g, juce::Rectangle<int> bounds,
                                        const juce::String& text) const {
    if (bounds.isEmpty())
        return;

    const auto& c = theme.colors;
    const float radius = theme.metrics.cornerRadiusSmall;
    const auto cap = bounds.toFloat();

    // Soft one-pixel-offset shadow first, so the cap reads as sitting above the surface.
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(cap.translated(0.0f, 1.0f), radius);

    g.setColour(c.surfaceHi);
    g.fillRoundedRectangle(cap, radius);

    // One-pixel outline, plus a two-pixel bottom edge (the "key thickness") clipped to the cap's shape.
    juce::Path shape;
    shape.addRoundedRectangle(cap, radius);
    g.saveState();
    g.reduceClipRegion(juce::Rectangle<int>(bounds.getX(), bounds.getBottom() - kKeyCapBottomEdge, bounds.getWidth(),
                                            kKeyCapBottomEdge));
    g.setColour(c.border);
    g.fillPath(shape);
    g.restoreState();
    g.setColour(c.border);
    g.drawRoundedRectangle(cap.reduced(0.5f), radius, theme.metrics.borderWidth);

    g.setColour(c.textPrimary);
    g.setFont(getShortcutKeyCapFont());
    g.drawText(text, bounds.withTrimmedBottom(1), juce::Justification::centred, false);
}

} // namespace synth::theme
