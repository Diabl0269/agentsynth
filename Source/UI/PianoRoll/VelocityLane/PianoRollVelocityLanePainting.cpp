// PianoRollVelocityLane — painting: the strip's background and scale gutter, one stick per note,
// and the value readout beside the hovered or dragged stick. Colours come from the theme (and each
// stick's own colour from the host, which resolves it exactly the way the grid colours the note);
// text goes through the theme's mono family via AppLookAndFeel, never a raw family name.

#include "PianoRollVelocityLane.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "VelocityLaneMath.h"

namespace synth::ui {

namespace {
struct LanePalette {
    juce::Colour plot, gutter, border, text, selected;
    float scaleFontPx = 8.5f;
    float readoutFontPx = 10.0f;
};

LanePalette paletteFor(juce::Component& c) {
    const auto& theme = synth::theme::themeOf(c);
    LanePalette p{theme.colors.bg1, theme.colors.surface, theme.colors.border, theme.colors.textMuted,
                  theme.colors.noteSelected};
    p.scaleFontPx = theme.type.micro;
    p.readoutFontPx = theme.type.value + 1.0f;
    return p;
}

juce::Font monoFont(float px) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), px, juce::Font::plain));
}
} // namespace

void PianoRollVelocityLane::paint(juce::Graphics& g) {
    const auto palette = paletteFor(*this);
    const int gutter = juce::jlimit(0, getWidth(), gutterWidth());
    g.fillAll(palette.plot);
    g.setColour(palette.gutter);
    g.fillRect(0, 0, gutter, getHeight());

    // Faint guides at the three scale values, then the separator from the grid above.
    g.setColour(palette.border.withAlpha(0.45f));
    for (const int v : {velocitylane::kMaxVelocity, 64, velocitylane::kMinVelocity})
        g.drawHorizontalLine((int)std::lround(yForVelocity(v)), (float)gutter, (float)getWidth());
    g.setColour(palette.border);
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());
    g.drawVerticalLine(gutter - 1, 0.0f, (float)getHeight());

    paintScale(g, gutter);
    paintSticks(g, gutter);
    paintFocusRing(g, getLocalBounds().toFloat(), *this);
}

// "127 / 64 / 1" right-aligned against the gutter's edge, i.e. under the keys column, the same way
// the keys column labels its rows.
void PianoRollVelocityLane::paintScale(juce::Graphics& g, int gutter) {
    const auto palette = paletteFor(*this);
    g.setColour(palette.text);
    g.setFont(monoFont(palette.scaleFontPx));
    const int labelH = (int)std::ceil(palette.scaleFontPx) + 2;
    for (const int v : {velocitylane::kMaxVelocity, 64, velocitylane::kMinVelocity}) {
        const int y =
            juce::jlimit(0, std::max(0, getHeight() - labelH), (int)std::lround(yForVelocity(v)) - labelH / 2);
        g.drawText(juce::String(v), juce::Rectangle<int>(0, y, std::max(0, gutter - 4), labelH),
                   juce::Justification::centredRight, false);
    }
}

// A stick is a 2 px line from the bottom up to its value with a small round head. Its colour is the
// note's own (host-resolved, so a louder note is brighter exactly as in the grid); selected sticks
// take the theme's noteSelected highlight. The gesture's preview wins over the stored value.
void PianoRollVelocityLane::paintSticks(juce::Graphics& g, int gutter) {
    const auto palette = paletteFor(*this);
    const float bottom = (float)getHeight();
    const auto sticks = currentSticks();
    if (readout_.isValid())
        shown_.reset(); // re-found below; a readout whose stick is off-screen must not linger
    for (const auto& s : sticks) {
        if (s.x < gutter || s.x > getWidth())
            continue;
        const auto it = preview_.find(s.id);
        const int velocity = it != preview_.end() ? it->second : s.velocity;
        const float headY = yForVelocity(velocity);
        g.setColour(s.selected ? palette.selected : s.colour);
        g.fillRect((float)s.x - 1.0f, headY, 2.0f, bottom - headY);
        g.fillEllipse((float)s.x - 3.0f, headY - 3.0f, 6.0f, 6.0f);
        if (s.id == readout_)
            shown_ = ShownReadout{s, velocity};
    }
    // While fading out readout_ is already clear, so the LAST painted readout keeps drawing.
    if (shown_ && readoutOpacity_ > 0.0f)
        paintReadout(g, shown_->stick, shown_->velocity);
}

// The box at fade position `t`: beside the head, flipped to the stick's left when it would run off
// the right edge, kept inside the strip vertically. Mid-fade it also sits velocitylane::
// readoutSlidePx(t) nearer the stick, so it emerges from the head as it fades.
juce::Rectangle<int> PianoRollVelocityLane::readoutBox(const Stick& stick, int velocity, float t) {
    const auto palette = paletteFor(*this);
    const auto font = monoFont(palette.readoutFontPx);
    const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(font, juce::String(velocity))) + 6;
    const int h = (int)std::ceil(palette.readoutFontPx) + 4;
    const bool flipped = stick.x + 5 + w > getWidth();
    const int rest = flipped ? stick.x - 5 - w : stick.x + 5;
    const int slide = (int)std::lround(velocitylane::readoutSlidePx(t));
    const int y = juce::jlimit(1, std::max(1, getHeight() - h), (int)std::lround(yForVelocity(velocity)) - h / 2);
    return {rest + (flipped ? slide : -slide), y, w, h};
}

void PianoRollVelocityLane::paintReadout(juce::Graphics& g, const Stick& stick, int velocity) {
    const auto palette = paletteFor(*this);
    const float t = readoutOpacity_;
    const auto box = readoutBox(stick, velocity, t);
    g.setColour(palette.plot.withAlpha(0.85f * t));
    g.fillRect(box);
    g.setColour(palette.text.brighter(0.4f).withMultipliedAlpha(t));
    g.setFont(monoFont(palette.readoutFontPx));
    g.drawText(juce::String(velocity), box, juce::Justification::centred, false);
}

} // namespace synth::ui
