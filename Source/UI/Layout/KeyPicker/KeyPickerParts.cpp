#include "KeyPickerParts.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Layout/SearchMatch.h"

namespace synth::ui {

namespace {
constexpr float kRowFontSize = 13.0f;
constexpr float kDetailFontSize = 11.0f;
constexpr int kGutter = 16;
constexpr int kDetailWidth = 128;
} // namespace

KeyPickerRow::KeyPickerRow(juce::String header)
    : header_(true)
    , text_(std::move(header)) {
    setTitle(text_);
    setInterceptsMouseClicks(false, false);
}

KeyPickerRow::KeyPickerRow(juce::String scaleName, std::uint16_t mask, int root)
    : name_(std::move(scaleName))
    , mask_(mask)
    , root_(root) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    refreshTexts();
}

void KeyPickerRow::refreshTexts() {
    const auto k = key();
    text_ = keyDisplayName(k);
    detail_ = keyNoteNames(k);
    setTitle(text_ + ", " + detail_);
    setTooltip(text_ + ", " + detail_);
    repaint();
}

void KeyPickerRow::setRoot(int root) {
    if (root_ == root)
        return;
    root_ = root;
    refreshTexts();
}

void KeyPickerRow::setTicked(bool ticked) {
    if (ticked_ != ticked) {
        ticked_ = ticked;
        repaint();
    }
}

void KeyPickerRow::setHighlighted(bool on) {
    if (highlighted_ != on) {
        highlighted_ = on;
        repaint();
    }
}

void KeyPickerRow::setPalette(const keypicker::Palette& palette) {
    palette_ = palette;
    repaint();
}

void KeyPickerRow::setQuery(const juce::String& query) {
    if (query_ != query) {
        query_ = query;
        repaint();
    }
}

void KeyPickerRow::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    if (header_) {
        g.setColour(palette_.muted);
        g.setFont(juce::Font(juce::FontOptions(kDetailFontSize, juce::Font::bold)));
        g.drawText(text_, bounds.reduced(6, 0), juce::Justification::centredLeft, true);
        return;
    }
    if (highlighted_) {
        g.setColour(palette_.accent.withAlpha(0.25f));
        g.fillRoundedRectangle(bounds.toFloat().reduced(2.0f, 1.0f), 3.0f);
    }
    auto content = bounds.reduced(12, 0);
    const auto gutter = content.removeFromLeft(kGutter).toFloat().getCentre();
    if (ticked_) {
        juce::Path tick;
        tick.startNewSubPath(gutter.x - 4.0f, gutter.y);
        tick.lineTo(gutter.x - 1.5f, gutter.y + 3.0f);
        tick.lineTo(gutter.x + 4.0f, gutter.y - 3.5f);
        g.setColour(palette_.accent);
        g.strokePath(tick, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(palette_.muted);
    g.setFont(juce::Font(juce::FontOptions(kDetailFontSize)));
    g.drawText(detail_, content.removeFromRight(kDetailWidth), juce::Justification::centredRight, true);
    drawSearchHighlightedText(
        g, text_, query_, content,
        juce::Font(juce::FontOptions(kRowFontSize, ticked_ ? juce::Font::bold : juce::Font::plain)),
        ticked_ ? palette_.accent : palette_.text, palette_.accent.withAlpha(0.28f), palette_.accent);
}

void KeyPickerRow::mouseEnter(const juce::MouseEvent&) {
    if (!header_ && onHover)
        onHover(*this);
}

void KeyPickerRow::mouseMove(const juce::MouseEvent&) {
    if (!header_ && onHover && !highlighted_)
        onHover(*this);
}

void KeyPickerRow::mouseUp(const juce::MouseEvent& e) {
    if (!header_ && onClick && getLocalBounds().contains(e.getPosition()))
        onClick(*this);
}

// ---- KeyPickerButton ----

KeyPickerButton::KeyPickerButton(const juce::String& title, const juce::String& tooltip)
    : juce::Button(title) {
    setTitle(title);
    setTooltip(tooltip);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void KeyPickerButton::setPalette(const keypicker::Palette& palette) {
    palette_ = palette;
    repaint();
}

void KeyPickerButton::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const bool enabled = isEnabled();
    g.setColour(palette_.field);
    g.fillRoundedRectangle(bounds, 4.0f);
    if (enabled && (over || down)) {
        g.setColour(palette_.accent.withAlpha(down ? 0.3f : 0.15f));
        g.fillRoundedRectangle(bounds, 4.0f);
    }
    g.setColour(enabled && over ? palette_.accent : palette_.border);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    g.setColour(enabled ? palette_.text : palette_.muted.withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions(kDetailFontSize)));
    g.drawText(getButtonText(), getLocalBounds().reduced(6, 0), juce::Justification::centred, true);
    paintFocusRing(g, bounds, *this, 4.0f);
}

// ---- KeyPickerFollowRow ----

KeyPickerFollowRow::KeyPickerFollowRow(const juce::String& tooltip)
    : juce::Button("Follow song key") {
    setTitle("Follow song key");
    setTooltip(tooltip);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void KeyPickerFollowRow::setState(bool on, const juce::String& detail) {
    on_ = on;
    detail_ = detail;
    repaint();
}

void KeyPickerFollowRow::setPalette(const keypicker::Palette& palette) {
    palette_ = palette;
    repaint();
}

void KeyPickerFollowRow::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto bounds = getLocalBounds().toFloat();
    if (over || down || on_) {
        g.setColour(palette_.accent.withAlpha(on_ ? 0.22f : 0.12f));
        g.fillRoundedRectangle(bounds.reduced(2.0f, 1.0f), 4.0f);
    }
    auto content = getLocalBounds().reduced(12, 0);
    const auto dot = content.removeFromLeft(kGutter).toFloat().getCentre();
    g.setColour(on_ ? palette_.accent : palette_.muted);
    if (on_)
        g.fillEllipse(dot.x - 4.0f, dot.y - 4.0f, 8.0f, 8.0f);
    else
        g.drawEllipse(dot.x - 4.0f, dot.y - 4.0f, 8.0f, 8.0f, 1.2f);
    g.setColour(on_ ? palette_.accent : palette_.text);
    g.setFont(juce::Font(juce::FontOptions(kRowFontSize, on_ ? juce::Font::bold : juce::Font::plain)));
    g.drawText("Follow song key", content, juce::Justification::centredLeft, true);
    g.setColour(palette_.muted);
    g.setFont(juce::Font(juce::FontOptions(kDetailFontSize)));
    g.drawText(detail_, content, juce::Justification::centredRight, true);
    paintFocusRing(g, bounds.reduced(1.0f), *this, 4.0f);
}

} // namespace synth::ui
