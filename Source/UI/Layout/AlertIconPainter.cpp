// Concern: the alert box's severity icon at each Depth level (see AlertIconPainter.h).
#include "UI/Layout/AlertIconPainter.h"

#include <algorithm>

namespace synth::ui {

void paintAlertIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph glyph,
                    juce::Colour surface, const synth::theme::Theme& theme) {
    using synth::theme::DepthLevel;
    const float side = std::min(area.getWidth(), area.getHeight());
    const auto disc = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
    const auto centre = disc.getCentre();
    const float softGlyph = side * kAlertGlyphShare;

    switch (theme.treatment.depth) {
    case DepthLevel::Soft:
        break;
    case DepthLevel::Flat:
        paintSeverityShape(g, centre, softGlyph * kFlatGlyphScale, ink, glyph, surface, false, true);
        return;
    case DepthLevel::Rich:
        break;
    }

    // The one disc painter, for Soft and Rich alike: Rich passes the theme and so adds the shared tile layers (shadow,
    // rim, highlight) around Soft's disc and glyph; Soft passes none and is today's disc byte for byte. Rich never adds
    // a shape: a warning keeps Soft's silhouette and glyph position.
    paintSeverityDisc(g, area, ink, glyph, surface, theme.treatment.depth == DepthLevel::Rich ? &theme : nullptr);
}

} // namespace synth::ui
