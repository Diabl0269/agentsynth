// SoundBrowserIcons.cpp -- the row icons of the Sounds view: path strings on a 16 x 16 grid and the one stroke that
// draws them.
#include "UI/Library/SoundBrowser/SoundBrowserIcons.h"

namespace synth::ui {

namespace {

constexpr float kGrid = 16.0f;
constexpr float kStroke = 1.6f;

// Each icon is one or more sub-paths; they are the design board's artwork.
const char* svgOf(SoundRowIcon icon) {
    switch (icon) {
    case SoundRowIcon::Folder:
        return "M2 4.5h4l1.5 1.5H14v6.5H2z";
    case SoundRowIcon::OneShot:
        return "M1.5 8h2l1.5-5 2 10 1.5-7 1.2 4H14.5";
    case SoundRowIcon::Loop:
        return "M3 7a4 4 0 0 1 7-2.6M13 9a4 4 0 0 1-7 2.6M10.5 2.5v2.2H8.3M5.5 13.5v-2.2h2.2";
    case SoundRowIcon::Instrument:
        return "M2 3.5h12v9H2zM5 3.5v5.5M8 3.5v5.5M11 3.5v5.5";
    case SoundRowIcon::Pack:
        return "M2.5 5 8 2.5 13.5 5v6L8 13.5 2.5 11zM2.5 5 8 7.5 13.5 5M8 7.5v6";
    case SoundRowIcon::Favourites:
        return "M8 2 9.8 5.8l4 .5-3 2.8.8 4L8 11.2 4.4 13l.8-4-3-2.8 4-.5z";
    case SoundRowIcon::Recent:
        return "M8 2.5a5.5 5.5 0 1 0 0 11a5.5 5.5 0 1 0 0-11zM8 5v3l2 1.5";
    case SoundRowIcon::Shapes:
        return "M1.5 12c3 0 3-8 6.5-8s3.5 8 6.5 8";
    case SoundRowIcon::Plus:
        return "M8 3v10M3 8h10";
    }
    return "";
}

} // namespace

juce::Path soundRowIconPath(SoundRowIcon icon) { return juce::Drawable::parseSVGPath(svgOf(icon)); }

void paintSoundRowIcon(juce::Graphics& g, SoundRowIcon icon, juce::Rectangle<float> cell, juce::Colour colour) {
    auto path = soundRowIconPath(icon);
    path.applyTransform(juce::AffineTransform::scale(cell.getWidth() / kGrid, cell.getHeight() / kGrid)
                            .translated(cell.getX(), cell.getY()));
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(kStroke * cell.getWidth() / kGrid, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

} // namespace synth::ui
