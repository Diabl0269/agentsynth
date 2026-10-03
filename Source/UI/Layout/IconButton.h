#pragma once

#include "UI/Theme/IconGlyphs.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

// The one icon button of the app: a juce::Button that shows a synth::theme::Glyph and nothing else.
// Hover, press, focus ring and colours are defined once, in AppLookAndFeel::drawIconButton; this
// class only carries the state they read. Call sites keep their own title, tooltip, description,
// component ID and focus policy: the button never sets any of them.
class IconButton : public juce::Button {
public:
    // Framed: surface fill and border (a transport button). Bare: no background until hovered (a
    // toolbar or row icon). Round: circular hover wash (a popover header icon). Danger: red hover
    // wash and glyph (a destructive icon).
    enum class Style { Framed, Bare, Round, Danger };

    IconButton(const juce::String& name, synth::theme::Glyph glyph, Style style = Style::Bare);

    void setGlyph(synth::theme::Glyph glyph);
    // The glyph drawn while getToggleState() is true (play to stop, record idle to on, eye hidden to open).
    void setGlyphWhenOn(synth::theme::Glyph glyph);
    // Replaces the accent as the lit colour (the record button's red) and tints a Framed background.
    void setOnColour(std::optional<juce::Colour> colour);
    // Accent: toggled on reads as "active" in the lit colour. Plain: on is the normal state of the
    // control (an eye that is open), so it reads like hover (textPrimary) instead of lighting up.
    enum class OnTone { Accent, Plain };
    void setOnTone(OnTone tone);
    OnTone getOnTone() const noexcept { return onTone_; }

    synth::theme::Glyph getGlyph() const noexcept { return glyph_; }
    // The glyph painted right now: the "when on" one while toggled on, else getGlyph().
    synth::theme::Glyph currentGlyph() const noexcept;
    Style getStyle() const noexcept { return style_; }
    std::optional<juce::Colour> getOnColour() const noexcept { return onColour_; }

    // THE colour the glyph is painted in for the given hover/press state: the single source for the
    // look-and-feel and for tests.
    juce::Colour glyphColour(bool highlighted, bool down) const;

    // Test seam: draws the focus ring without real keyboard focus.
    bool forceFocusRingForTest = false;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    synth::theme::Glyph glyph_;
    std::optional<synth::theme::Glyph> glyphWhenOn_;
    std::optional<juce::Colour> onColour_;
    OnTone onTone_ = OnTone::Accent;
    Style style_;
};

} // namespace synth::ui
