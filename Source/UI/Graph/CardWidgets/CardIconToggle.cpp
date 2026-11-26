// CardIconToggle.cpp -- see CardIconToggle.h.
#include "UI/Graph/CardWidgets/CardIconToggle.h"

#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/ButtonTooltip.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kCorner = 4.0f;
constexpr float kGlyphInset = 3.0f;
} // namespace

CardIconToggle::CardIconToggle(Glyph glyph, const juce::String& name, const juce::String& explanation)
    : juce::ToggleButton()
    , glyph_(glyph) {
    setTitle(name);
    setWantsKeyboardFocus(true);
    setClickingTogglesState(true);
    setButtonTooltip(*this, explanation);
}

void CardIconToggle::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const bool lit = getToggleState();
    // A lit chip is the accent to its edge, which would swallow the ring: while the ring shows, the chip steps in from
    // it (focusSteppedFill) and the glyph keeps its place.
    const auto full = getLocalBounds().toFloat().reduced(1.0f);
    const auto area =
        lit ? focusSteppedFill(*this, synth::theme::themeOf(*this), full, getLocalBounds().toFloat()) : full;
    // A chip with its own surface, so the icon reads over the picture's curve and grid behind it.
    g.setColour(lit ? colors.accent : colors.surface.withAlpha(0.92f).brighter(highlighted || down ? 0.12f : 0.0f));
    g.fillRoundedRectangle(area, kCorner);
    g.setColour(lit ? colors.accent : colors.border);
    g.drawRoundedRectangle(area, kCorner, 1.0f);
    const auto ink = lit ? colors.bg0 : (highlighted ? colors.textPrimary : colors.textMuted);
    const auto glyphArea = full.reduced(kGlyphInset);
    if (glyph_ == Glyph::response)
        paintResponseGlyph(g, glyphArea, ink);
    else if (glyph_ == Glyph::spectrum)
        paintSpectrumGlyph(g, glyphArea, ink);
    else
        paintWindowGlyph(g, glyphArea, ink);
    paintFocusRing(g, getLocalBounds().toFloat(), *this, kCorner);
}

} // namespace synth::ui
