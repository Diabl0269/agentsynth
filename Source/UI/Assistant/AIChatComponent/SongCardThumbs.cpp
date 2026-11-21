#include "SongCardThumbs.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth {

ThumbButton::ThumbButton(bool up)
    : juce::Button(up ? "Like" : "Dislike")
    , up_(up) {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ThumbButton::setSelected(bool selected) {
    if (selected == selected_)
        return;
    selected_ = selected;
    setToggleState(selected, juce::dontSendNotification);
    repaint();
}

// Drawn in a 24 x 24 box: a cuff on the left and the hand with its thumb up, corners rounded. The dislike is the same
// path turned over vertically, so the two always match.
juce::Path ThumbButton::thumbPath(bool up, juce::Rectangle<float> area) {
    juce::Path p;
    p.addRoundedRectangle(2.0f, 10.0f, 4.0f, 11.0f, 1.2f);
    juce::Path hand;
    hand.startNewSubPath(8.5f, 10.5f);
    hand.lineTo(12.0f, 3.5f);
    hand.lineTo(13.6f, 3.5f);
    hand.lineTo(14.2f, 4.6f);
    hand.lineTo(13.2f, 9.0f);
    hand.lineTo(19.8f, 9.0f);
    hand.lineTo(21.4f, 10.6f);
    hand.lineTo(19.6f, 19.4f);
    hand.lineTo(18.2f, 21.0f);
    hand.lineTo(8.5f, 21.0f);
    hand.closeSubPath();
    p.addPath(hand.createPathWithRoundedCorners(1.6f));
    if (!up)
        p.applyTransform(juce::AffineTransform::verticalFlip(24.0f));
    const auto scale = juce::AffineTransform::scale(area.getWidth() / 24.0f, area.getHeight() / 24.0f)
                           .translated(area.getX(), area.getY());
    p.applyTransform(scale);
    return p;
}

void ThumbButton::paintButton(juce::Graphics& g, bool hot, bool down) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto r = getLocalBounds().toFloat();
    const auto hue = up_ ? c.success : c.error;
    if (hot || down) {
        g.setColour(c.surfaceHi);
        g.fillRoundedRectangle(r.reduced(1.0f), 6.0f);
    }
    const auto path = thumbPath(up_, r.reduced(4.0f));
    if (selected_) {
        g.setColour(hue);
        g.fillPath(path);
    } else {
        g.setColour(hot || down ? c.textPrimary : c.textMuted);
        g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    synth::ui::paintFocusRing(g, r, *this, 6.0f);
}

} // namespace synth
