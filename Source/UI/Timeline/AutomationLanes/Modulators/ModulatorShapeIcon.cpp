// Concern: the modulator row's shape picture -- which glyph each LFO shape index maps to, its names, and the cross-fade
// when the shape changes.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorShapeIcon.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr double kFadeMs = 160.0;
} // namespace

ModulatorShapeIcon::ModulatorShapeIcon() {
    setComponentID("modulatorShapeIcon");
    setInterceptsMouseClicks(true, false); // for the tooltip; the row listens, so a right-click still opens its menu
    setWantsKeyboardFocus(false);
    setShape(0);
}

synth::theme::Glyph ModulatorShapeIcon::glyphForShape(int shapeIndex) {
    using synth::theme::Glyph;
    switch (shapeIndex) {
    case 0:
        return Glyph::ShapeSine;
    case 1:
        return Glyph::ShapeTriangle;
    case 2:
        return Glyph::ShapeSaw;
    case 3:
        return Glyph::ShapeSquare;
    case 4:
        return Glyph::ShapeRandom;
    default:
        return Glyph::ShapeCustom;
    }
}

juce::String ModulatorShapeIcon::nameForShape(int shapeIndex) {
    switch (shapeIndex) {
    case 0:
        return "Sine";
    case 1:
        return "Triangle";
    case 2:
        return "Sawtooth";
    case 3:
        return "Square";
    case 4:
        return "Sample and hold";
    default:
        return "Custom";
    }
}

// A repeated call with the same shape is free. While the row is on screen the old picture fades out as the new one
// fades in; headless (nothing shows) the new one lands at once.
void ModulatorShapeIcon::setShape(int shapeIndex) {
    const auto name = nameForShape(shapeIndex) + " shape";
    setTitle(name);
    setTooltip(name);
    if (shapeIndex == shape_ && getTitle().isNotEmpty() && fade_ >= 1.0f)
        return;
    const bool changed = glyphForShape(shapeIndex) != glyphForShape(shape_);
    previousShape_ = shape_;
    shape_ = shapeIndex;
    if (!changed || !isShowing()) {
        driver_.stop(updater_);
        fade_ = 1.0f;
        repaint();
        return;
    }
    fade_ = 0.0f;
    driver_.start(
        updater_, kFadeMs, easeOutCubic,
        [this](float t) {
            fade_ = t;
            repaint();
        },
        [this] {
            fade_ = 1.0f;
            repaint();
        });
}

void ModulatorShapeIcon::paint(juce::Graphics& g) {
    const auto colour = synth::theme::themeOf(*this).colors.textPrimary;
    const auto area = getLocalBounds().toFloat();
    if (fade_ < 1.0f)
        synth::theme::paintGlyph(g, glyphForShape(previousShape_), area, colour.withMultipliedAlpha(1.0f - fade_));
    synth::theme::paintGlyph(g, glyphForShape(shape_), area, colour.withMultipliedAlpha(fade_));
}

} // namespace synth::ui
