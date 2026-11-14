#include "CentredLabel.h"

namespace synth::ui {

// A label whose INK is centred on a float point (numbers in dots). drawText with an int box truncates the centre by up
// to a pixel, and Justification::centred centres the line box including the descender space, so digits ride high; the
// glyph path's own bounds have neither error.
void drawLabelCentredOn(juce::Graphics& g, const juce::String& text, const juce::Font& font,
                        juce::Point<float> centre) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    juce::Path path;
    glyphs.createPath(path);
    if (path.isEmpty())
        return;
    const auto ink = path.getBounds().getCentre();
    path.applyTransform(juce::AffineTransform::translation(centre.x - ink.x, centre.y - ink.y));
    g.fillPath(path);
}

} // namespace synth::ui
