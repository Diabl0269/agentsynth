#include "AlertIconBounds.h"
#include "AppLookAndFeel.h"

namespace synth::ui {

// JUCE draws an alert's message 80 px in from the window's inner left edge and 30 px down whenever there is
// an icon, so the column left of that is the icon's: the icon ends a fixed gap before the message and
// starts level with the title.
juce::Rectangle<int> alertIconBounds(const juce::Rectangle<int>& windowBounds) {
    constexpr int kMessageLeft = 81;
    constexpr int kGapBeforeMessage = 10;
    constexpr int kTitleTop = 30;
    const juce::Rectangle<int> icon(windowBounds.getX() + kMessageLeft - kGapBeforeMessage - kAlertIconSize,
                                    windowBounds.getY() + kTitleTop, kAlertIconSize, kAlertIconSize);
    return icon.constrainedWithin(windowBounds);
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
// translucent shape offset past the window's top-left corner and clipped there. The icon here is a small
// accent glyph inside the box.
void AppLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>&,
                                  juce::TextLayout& textLayout) {
    constexpr float kCornerSize = 4.0f;
    constexpr int kIconSpace = 80;

    g.setColour(alert.findColour(juce::AlertWindow::outlineColourId));
    g.drawRoundedRectangle(alert.getLocalBounds().toFloat(), kCornerSize, 2.0f);

    const auto bounds = alert.getLocalBounds().reduced(1);
    g.reduceClipRegion(bounds);
    g.setColour(alert.findColour(juce::AlertWindow::backgroundColourId));
    g.fillRoundedRectangle(bounds.toFloat(), kCornerSize);

    lastAlertIconBounds_ = {};
    int iconSpaceUsed = 0;
    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon) {
        lastAlertIconBounds_ = synth::ui::alertIconBounds(alert.getLocalBounds());
        const auto glyph = juce::Drawable::parseSVGPath(glyphFor(alert.getAlertType()));
        g.setColour(theme.colors.accent);
        const auto box = lastAlertIconBounds_.toFloat();
        g.fillPath(glyph, juce::AffineTransform::scale(box.getWidth() / 24.0f).translated(box.getX(), box.getY()));
        iconSpaceUsed = kIconSpace;
    }

    g.setColour(alert.findColour(juce::AlertWindow::textColourId));
    const juce::Rectangle<int> messageBounds(bounds.getX() + iconSpaceUsed, 30, bounds.getWidth(),
                                             bounds.getHeight() - getAlertWindowButtonHeight() - 20);
    textLayout.draw(g, messageBounds.toFloat());
}

} // namespace synth::theme
