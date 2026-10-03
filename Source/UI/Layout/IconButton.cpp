#include "IconButton.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// The theme a button outside any AppLookAndFeel (a bare test fixture) paints with.
const synth::theme::Theme& fallbackTheme() {
    static const synth::theme::Theme theme;
    return theme;
}

const synth::theme::Theme& themeOf(const juce::Component& comp) {
    if (const auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&comp.getLookAndFeel()))
        return lf->getTheme();
    return fallbackTheme();
}
} // namespace

IconButton::IconButton(const juce::String& name, synth::theme::Glyph glyph, Style style)
    : juce::Button(name)
    , glyph_(glyph)
    , style_(style) {}

void IconButton::setGlyph(synth::theme::Glyph glyph) {
    glyph_ = glyph;
    repaint();
}

void IconButton::setGlyphWhenOn(synth::theme::Glyph glyph) {
    glyphWhenOn_ = glyph;
    repaint();
}

void IconButton::setOnColour(std::optional<juce::Colour> colour) {
    onColour_ = colour;
    repaint();
}

void IconButton::setOnTone(OnTone tone) {
    onTone_ = tone;
    repaint();
}

synth::theme::Glyph IconButton::currentGlyph() const noexcept {
    return (getToggleState() && glyphWhenOn_) ? *glyphWhenOn_ : glyph_;
}

juce::Colour IconButton::glyphColour(bool highlighted, bool down) const {
    return synth::theme::iconButtonGlyphColour(themeOf(*this), *this, highlighted, down);
}

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        lf->drawIconButton(g, *this, highlighted, down);
    else
        synth::theme::paintIconButton(g, *this, fallbackTheme(), highlighted, down);
}

} // namespace synth::ui
