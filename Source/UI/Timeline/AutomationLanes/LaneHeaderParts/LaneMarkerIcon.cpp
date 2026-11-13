// Concern: the lane header's marker icon -- which glyph and colour each lane kind draws.
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneMarkerIcon.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/IconGlyphs.h"

namespace synth::ui {

LaneMarkerIcon::LaneMarkerIcon(Kind kind)
    : kind_(kind) {
    setComponentID("laneMarkerIcon");
    setAccessible(false);
    setSize(kSize, kSize);
}

void LaneMarkerIcon::setKind(Kind kind) {
    if (kind_ == kind)
        return;
    kind_ = kind;
    repaint();
}

void LaneMarkerIcon::paint(juce::Graphics& g) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    const bool mod = kind_ == Kind::Mod;
    synth::theme::paintGlyph(g, mod ? synth::theme::Glyph::LaneMod : synth::theme::Glyph::LaneAutomation,
                             getLocalBounds().toFloat(), mod ? colours.hueViolet : colours.accent);
}

} // namespace synth::ui
