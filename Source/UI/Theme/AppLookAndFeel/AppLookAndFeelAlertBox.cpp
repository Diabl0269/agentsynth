#include "AlertIconBounds.h"
#include "AppLookAndFeel.h"
#include "UI/Timeline/TrackColour.h"

namespace synth::ui {

// JUCE sizes an alert window for an 80 px icon column left of its text (plus the 1 px border), so the text
// starts kAlertTextLeft in; drawAlertBox draws it kAlertTextTop down. The icon is about twice the title's line
// height, the size macOS, Cubase and Logic give their caution icon, and its centre sits on the title line's centre
// so the two read as one heading. A layout with no lines (no title) falls back to one line at the text top.
juce::Range<float> alertTitleLineSpan(const juce::TextLayout& text) {
    if (text.getNumLines() == 0)
        return {(float)kAlertTextTop, (float)kAlertTextTop + 18.0f};
    return text.getLine(0).getLineBoundsY() + (float)kAlertTextTop;
}

juce::Rectangle<int> alertIconBounds(const juce::Rectangle<int>& windowBounds, const juce::TextLayout& text) {
    const auto title = alertTitleLineSpan(text);
    const int titleCentre = juce::roundToInt(title.getStart() + title.getLength() / 2.0f);
    const juce::Rectangle<int> icon(windowBounds.getX() + kAlertTextLeft - kAlertIconGap - kAlertIconSize,
                                    windowBounds.getY() + titleCentre - kAlertIconSize / 2, kAlertIconSize,
                                    kAlertIconSize);
    return icon.constrainedWithin(windowBounds);
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

// Design-system glyphs in a 24-unit box, all nonzero-wound: the mark is a hole in the filled shape.
constexpr const char* kWarningGlyph = "M12 2 1 21h22L12 2Zm-1 7h2v6h-2V9Zm0 8h2v2h-2v-2Z";
constexpr const char* kInfoGlyph =
    "M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z";
constexpr const char* kQuestionGlyph =
    "M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 17h-2v-2h2v2zm2.07-7.75-.9.92C13.45 12.9 "
    "13 13.5 13 15h-2v-.5c0-1.1.45-2.1 1.17-2.83l1.24-1.26c.37-.36.59-.86.59-1.41 0-1.1-.9-2-2-2s-2 .9-2 2H8c0-2.21 "
    "1.79-4 4-4s4 1.79 4 4c0 .88-.36 1.68-.93 2.25z";

const char* glyphFor(juce::MessageBoxIconType type) {
    switch (type) {
    case juce::MessageBoxIconType::WarningIcon:
        return kWarningGlyph;
    case juce::MessageBoxIconType::InfoIcon:
        return kInfoGlyph;
    default:
        return kQuestionGlyph;
    }
}

} // namespace

// Everything LookAndFeel_V4 draws (outline, surface fill, the message) except its icon, which is a huge
// translucent shape offset past the window's top-left corner and clipped there. The icon here is a glyph about
// twice the title's height, centred on the title's line, in the warning colour for a warning and the accent otherwise.
void AppLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>&,
                                  juce::TextLayout& textLayout) {
    constexpr float kCornerSize = 4.0f;

    g.setColour(alert.findColour(juce::AlertWindow::outlineColourId));
    g.drawRoundedRectangle(alert.getLocalBounds().toFloat(), kCornerSize, 2.0f);

    const auto bounds = alert.getLocalBounds().reduced(1);
    g.reduceClipRegion(bounds);
    g.setColour(alert.findColour(juce::AlertWindow::backgroundColourId));
    g.fillRoundedRectangle(bounds.toFloat(), kCornerSize);

    lastAlertIconBounds_ = {};
    lastAlertTitleSpan_ = {};
    int iconSpaceUsed = 0;
    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon) {
        lastAlertIconBounds_ = synth::ui::alertIconBounds(alert.getLocalBounds(), textLayout);
        lastAlertTitleSpan_ = synth::ui::alertTitleLineSpan(textLayout);
        const auto glyph = juce::Drawable::parseSVGPath(glyphFor(alert.getAlertType()));
        lastAlertIconColour_ =
            synth::ui::alertIconColour(alert.getAlertType(), theme.colors.accent, theme.colors.warning,
                                       alert.findColour(juce::AlertWindow::backgroundColourId));
        g.setColour(lastAlertIconColour_);
        const auto box = lastAlertIconBounds_.toFloat();
        g.fillPath(glyph, juce::AffineTransform::scale(box.getWidth() / 24.0f).translated(box.getX(), box.getY()));
        iconSpaceUsed = synth::ui::kAlertTextLeft - bounds.getX();
    }

    g.setColour(alert.findColour(juce::AlertWindow::textColourId));
    const juce::Rectangle<int> messageBounds(bounds.getX() + iconSpaceUsed, synth::ui::kAlertTextTop, bounds.getWidth(),
                                             bounds.getHeight() - getAlertWindowButtonHeight() - 20);
    textLayout.draw(g, messageBounds.toFloat());
}

} // namespace synth::theme
