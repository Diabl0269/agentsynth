#include "AppLookAndFeel.h"

namespace synth::theme {

// Concern: scrollbars, text-editor chrome, labels, tooltips, and tabs.

void AppLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar& /*scrollbar*/, int x, int y, int width,
                                   int height, bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                   bool isMouseOver, bool isMouseDown) {
    const auto& c = theme.colors;

    // Slim track underlay.
    auto trackBounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(1.0f);
    g.setColour(c.bg1.withAlpha(0.4f));
    g.fillRoundedRectangle(trackBounds, (float)juce::jmin(width, height) * 0.5f);

    // Thumb (clamped to a sensible minimum length so it stays grabbable).
    const int minLen = 20;
    const int len = juce::jmax(minLen, thumbSize);
    juce::Rectangle<int> thumb;
    if (isScrollbarVertical)
        thumb = {x + 1, thumbStartPosition, width - 2, len};
    else
        thumb = {thumbStartPosition, y + 1, len, height - 2};

    auto col = c.border.brighter(0.3f);
    if (isMouseDown)
        col = col.brighter(0.3f);
    else if (isMouseOver)
        col = col.brighter(0.15f);

    g.setColour(col);
    g.fillRoundedRectangle(thumb.toFloat().reduced(1.0f),
                           (float)juce::jmin(thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

int AppLookAndFeel::getDefaultScrollbarWidth() { return kScrollbarWidth; }

void AppLookAndFeel::drawScrollbarButton(juce::Graphics& g, juce::ScrollBar& /*scrollbar*/, int width, int height,
                                         int buttonDirection, bool /*isScrollbarVertical*/, bool isMouseOverButton,
                                         bool isButtonDown) {
    // Minimal filled triangle pointing in buttonDirection (0=up,1=right,2=down,3=left).
    // Rarely shown on macOS overlay scrollbars; needed for Win/Linux parity.
    const auto& c = theme.colors;
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height).reduced(2.0f);
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.4f;

    juce::Path tri;
    switch (buttonDirection) {
    case 0:
        tri.addTriangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r);
        break; // up
    case 1:
        tri.addTriangle(cx + r, cy, cx - r, cy - r, cx - r, cy + r);
        break; // right
    case 2:
        tri.addTriangle(cx, cy + r, cx - r, cy - r, cx + r, cy - r);
        break; // down
    default:
        tri.addTriangle(cx - r, cy, cx + r, cy - r, cx + r, cy + r);
        break; // left
    }

    g.setColour((isButtonDown || isMouseOverButton) ? c.textPrimary : c.textMuted);
    g.fillPath(tri);
}

void AppLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, theme.metrics.pillRadius);
}

void AppLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    if (editor.isEnabled()) {
        const bool focused = editor.hasKeyboardFocus(true);
        g.setColour(focused ? theme.colors.accent : editor.findColour(juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f, theme.metrics.pillRadius,
                               theme.metrics.borderWidth);
    }
}

void AppLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    if (!label.isBeingEdited()) {
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
        g.setFont(label.getFont());

        auto area = label.getBorderSize().subtractedFrom(label.getLocalBounds());
        g.drawFittedText(label.getText(), area, label.getJustificationType(),
                         juce::jmax(1, (int)((float)area.getHeight() / label.getFont().getHeight())),
                         label.getMinimumHorizontalScale());
    }
}

juce::Rectangle<int> AppLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                      juce::Rectangle<int> parentArea) {
    // The stock LookAndFeel places the tip 24 px right of (or 12 px left of) the cursor, which
    // reads as detached from small controls like the timeline transport buttons. Keep it hugging
    // the pointer instead: horizontally centred on it, 14 px below the hotspot (a typical cursor's
    // height), flipped to just above when there is no room. Measured with the SAME font
    // drawTooltip() renders with, so the fitted text never clips.
    const juce::Font font(juce::FontOptions(theme.type.label + 1.0f));
    const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(font, tipText)) + 14;
    const int h = (int)std::ceil(font.getHeight()) + 8;

    const int x = screenPos.x - w / 2;
    const bool below = screenPos.y + 14 + h <= parentArea.getBottom();
    const int y = below ? screenPos.y + 14 : screenPos.y - h - 6;
    return juce::Rectangle<int>(x, y, w, h).constrainedWithin(parentArea);
}

void AppLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    const auto& c = theme.colors;
    auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height);
    g.setColour(c.surfaceHi);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(c.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, theme.metrics.borderWidth);

    g.setColour(c.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(theme.type.label + 1.0f)));
    g.drawFittedText(text, juce::Rectangle<int>(0, 0, width, height).reduced(6, 2), juce::Justification::centred, 3);
}

void AppLookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver,
                                   bool /*isMouseDown*/) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    const bool active = button.getToggleState();
    const auto orientation = button.getTabbedButtonBar().getOrientation();

    auto area = button.getActiveArea().toFloat();
    const float r = m.cornerRadius * 0.6f;

    if (active) {
        // Filled surface with the two corners facing AWAY from the content rounded.
        juce::Path bg;
        switch (orientation) {
        case juce::TabbedButtonBar::TabsAtBottom:
            bg.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, false, false,
                                   true, true);
            break;
        case juce::TabbedButtonBar::TabsAtLeft:
            bg.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, true, false, true,
                                   false);
            break;
        case juce::TabbedButtonBar::TabsAtRight:
            bg.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, false, true,
                                   false, true);
            break;
        case juce::TabbedButtonBar::TabsAtTop:
        default:
            bg.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, true, true, false,
                                   false);
            break;
        }
        g.setColour(c.surface);
        g.fillPath(bg);

        // 2px accent indicator on the content-facing inner edge.
        g.setColour(c.accent);
        auto edge = area;
        switch (orientation) {
        case juce::TabbedButtonBar::TabsAtBottom:
            g.fillRect(edge.removeFromTop(2.0f));
            break;
        case juce::TabbedButtonBar::TabsAtLeft:
            g.fillRect(edge.removeFromRight(2.0f));
            break;
        case juce::TabbedButtonBar::TabsAtRight:
            g.fillRect(edge.removeFromLeft(2.0f));
            break;
        case juce::TabbedButtonBar::TabsAtTop:
        default:
            g.fillRect(edge.removeFromBottom(2.0f));
            break;
        }
    } else {
        if (isMouseOver) {
            g.setColour(c.surface.withAlpha(0.35f));
            g.fillRect(area);
        }
        // Right-edge hairline divider between inactive tabs.
        g.setColour(c.border);
        g.fillRect(area.removeFromRight(1.0f));
    }

    g.setColour(active ? button.findColour(juce::TabbedButtonBar::frontTextColourId)
                       : button.findColour(juce::TabbedButtonBar::tabTextColourId));
    g.setFont(juce::Font(juce::FontOptions(theme.type.label + 2.0f, active ? juce::Font::bold : juce::Font::plain)));
    g.drawFittedText(button.getButtonText(), button.getActiveArea().reduced(6, 0), juce::Justification::centred, 1);
}

void AppLookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar& bar, juce::Graphics& g) {
    const auto& c = theme.colors;
    auto bounds = bar.getLocalBounds().toFloat();

    g.setColour(c.bg0);
    g.fillRect(bounds);

    // Hairline along the content-facing edge of the tab-button strip.
    g.setColour(c.border);
    switch (bar.getOrientation()) {
    case juce::TabbedButtonBar::TabsAtBottom:
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 1.0f);
        break;
    case juce::TabbedButtonBar::TabsAtLeft:
        g.fillRect(bounds.getRight() - 1.0f, bounds.getY(), 1.0f, bounds.getHeight());
        break;
    case juce::TabbedButtonBar::TabsAtRight:
        g.fillRect(bounds.getX(), bounds.getY(), 1.0f, bounds.getHeight());
        break;
    case juce::TabbedButtonBar::TabsAtTop:
    default:
        g.fillRect(bounds.getX(), bounds.getBottom() - 1.0f, bounds.getWidth(), 1.0f);
        break;
    }
}

} // namespace synth::theme
