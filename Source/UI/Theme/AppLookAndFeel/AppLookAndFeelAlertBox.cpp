#include "AlertIconBounds.h"
#include "AppLookAndFeel.h"
#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/AlertIconPainter.h"
#include "UI/Timeline/TrackColour.h"

namespace synth::ui {

// JUCE sizes an alert window for an 80 px icon column left of its text (plus the 1 px border); createAlertWindow
// adds kAlertColumnGrowth to it, so the text starts kAlertTextLeft in and drawAlertBox draws it kAlertTextTop down.
// The icon is a 64 px lit disc (the size macOS and Logic give theirs) whose centre sits on the middle of the title
// line and the first message line together (the blank line JUCE puts between them counts as part of that block), so
// it reads as the icon of the whole heading. A layout with no lines (no title) falls back to one line at the text top;
// a title with no message centres on the gap just under the title.
juce::Range<float> alertTitleLineSpan(const juce::TextLayout& text) {
    if (text.getNumLines() == 0)
        return {(float)kAlertTextTop, (float)kAlertTextTop + 18.0f};
    return text.getLine(0).getLineBoundsY() + (float)kAlertTextTop;
}

int alertIconCentreY(const juce::TextLayout& text) {
    const auto title = alertTitleLineSpan(text);
    // JUCE builds an alert's text as the title, a blank line, then the message, so the message's first line is line 2.
    if (text.getNumLines() > 2)
        return juce::roundToInt((title.getStart() + text.getLine(2).getLineBoundsY().getEnd() + (float)kAlertTextTop) *
                                0.5f);
    return juce::roundToInt(title.getEnd()) - 1;
}

juce::Rectangle<int> alertIconBounds(const juce::Rectangle<int>& windowBounds, const juce::TextLayout& text) {
    const juce::Rectangle<int> icon(windowBounds.getX() + kAlertTextLeft - kAlertIconGap - kAlertIconSize,
                                    windowBounds.getY() + alertIconCentreY(text) - kAlertIconSize / 2, kAlertIconSize,
                                    kAlertIconSize);
    return icon.constrainedWithin(windowBounds);
}

void makeRoomForAlertIcon(juce::AlertWindow& alert, juce::MessageBoxIconType iconType) {
    if (iconType == juce::MessageBoxIconType::NoIcon)
        return;
    int buttonTop = alert.getHeight();
    for (int i = 0; i < alert.getNumButtons(); ++i)
        buttonTop = juce::jmin(buttonTop, alert.getButton(i)->getY());
    const int grow = juce::jmax(0, kAlertIconBottomEstimate + 12 - buttonTop);
    for (auto* child : alert.getChildren())
        child->setTopLeftPosition(child->getX() + kAlertColumnGrowth / 2,
                                  child->getY() + (child->getY() >= buttonTop ? grow : 0));
    alert.setSize(alert.getWidth() + kAlertColumnGrowth, alert.getHeight() + grow);
}

// The warning token is the one a warning toast uses, so an alert and a toast about the same kind of problem agree.
// JUCE has no error icon type; info and question stay in the accent. A light theme's orange warning is pushed
// darker until it reads at 3:1 on the alert surface (WCAG's ratio for non-text marks).
juce::Colour alertIconColour(juce::MessageBoxIconType type, juce::Colour accent, juce::Colour warning,
                             juce::Colour background) {
    const auto colour = type == juce::MessageBoxIconType::WarningIcon ? warning : accent;
    return readableOn(colour, background.withAlpha(1.0f));
}

} // namespace synth::ui

namespace synth::theme {

namespace {

synth::ui::SeverityGlyph glyphFor(juce::MessageBoxIconType type) {
    switch (type) {
    case juce::MessageBoxIconType::WarningIcon:
        return synth::ui::SeverityGlyph::warning;
    case juce::MessageBoxIconType::InfoIcon:
        return synth::ui::SeverityGlyph::info;
    default:
        return synth::ui::SeverityGlyph::question;
    }
}

} // namespace

// Everything LookAndFeel_V4 draws (outline, surface fill, the message) except its icon, which is a huge
// translucent shape offset past the window's top-left corner and clipped there. The icon here follows the Depth level
// (paintAlertIcon: Flat a bare glyph, Soft the lit disc a toast draws too, Rich the same disc with the shared tile
// layers), in the warning colour for a warning and the accent otherwise. `theme` is read at paint time.
void AppLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>&,
                                  juce::TextLayout& textLayout) {
    constexpr float kCornerSize = 4.0f;

    g.setColour(alert.findColour(juce::AlertWindow::outlineColourId));
    g.drawRoundedRectangle(alert.getLocalBounds().toFloat(), kCornerSize, 2.0f);

    const auto bounds = alert.getLocalBounds().reduced(1);
    g.reduceClipRegion(bounds);
    g.setColour(alert.findColour(juce::AlertWindow::backgroundColourId));
    g.fillRoundedRectangle(bounds.toFloat(), kCornerSize);

    // Frosted-glass sheen in a glass theme only (its surface is translucent): a faint light band that starts a little
    // below the top edge, peaks about a fifth of the way down and fades out by 60 percent of the height, so it reads
    // as a soft glow inside the box rather than a rim on its edge. Opaque themes paint exactly what they did.
    if (theme.colors.surface.getAlpha() < 0xff) {
        const float top = (float)bounds.getY();
        const float height = (float)bounds.getHeight();
        juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.0f), 0.0f, top + height * 0.04f,
                                   juce::Colours::white.withAlpha(0.0f), 0.0f, top + height * 0.6f, false);
        sheen.addColour(0.28, juce::Colours::white.withAlpha(0.07f));
        g.setGradientFill(sheen);
        g.fillRoundedRectangle(bounds.toFloat(), kCornerSize);
    }

    lastAlertIconBounds_ = {};
    lastAlertTitleSpan_ = {};
    int iconSpaceUsed = 0;
    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon) {
        lastAlertIconBounds_ = synth::ui::alertIconBounds(alert.getLocalBounds(), textLayout);
        lastAlertTitleSpan_ = synth::ui::alertTitleLineSpan(textLayout);
        const auto surface = alert.findColour(juce::AlertWindow::backgroundColourId);
        lastAlertIconColour_ =
            synth::ui::alertIconColour(alert.getAlertType(), theme.colors.accent, theme.colors.warning, surface);
        synth::ui::paintAlertIcon(g, lastAlertIconBounds_.toFloat(), lastAlertIconColour_,
                                  glyphFor(alert.getAlertType()), surface.withAlpha(1.0f), theme);
        iconSpaceUsed = synth::ui::kAlertTextLeft - bounds.getX();
    }

    g.setColour(alert.findColour(juce::AlertWindow::textColourId));
    const juce::Rectangle<int> messageBounds(bounds.getX() + iconSpaceUsed, synth::ui::kAlertTextTop, bounds.getWidth(),
                                             bounds.getHeight() - getAlertWindowButtonHeight() - 20);
    textLayout.draw(g, messageBounds.toFloat());
}

} // namespace synth::theme
