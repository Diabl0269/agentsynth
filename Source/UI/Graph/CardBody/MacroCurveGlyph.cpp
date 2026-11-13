#include "MacroCurveGlyph.h"
#include "Modules/ModCurve.h"

#include "UI/ModuleViews/ModCurve/ModCurveChip.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth {

namespace {
constexpr const char* kGlyphId = "macroCurveGlyph";
}

MacroCurveGlyph::MacroCurveGlyph(juce::Component& knob)
    : knob_(knob) {
    setComponentID(kGlyphId);
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setAccessible(false);
    knob_.addAndMakeVisible(*this);
    knob_.addComponentListener(this);
    place();
}

MacroCurveGlyph::~MacroCurveGlyph() { knob_.removeComponentListener(this); }

MacroCurveGlyph* MacroCurveGlyph::of(juce::Component& knob) {
    for (auto* child : knob.getChildren())
        if (child->getComponentID() == kGlyphId)
            return static_cast<MacroCurveGlyph*>(child);
    return nullptr;
}

void MacroCurveGlyph::setCurve(const synth::ModCurve& curve) {
    if (curve_ == curve)
        return;
    curve_ = curve;
    repaint();
}

void MacroCurveGlyph::componentMovedOrResized(juce::Component&, bool, bool wasResized) {
    if (wasResized)
        place();
}

// The knob's bottom-right corner, beside the value's text box.
void MacroCurveGlyph::place() {
    // Clear of the corner, where Edit Layout draws its resize handle.
    setBounds(knob_.getWidth() - kWidth - 14, knob_.getHeight() - kHeight - 4, kWidth, kHeight);
}

void MacroCurveGlyph::paint(juce::Graphics& g) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    synth::ui::paintModCurveGlyph(g, getLocalBounds().toFloat().reduced(1.5f), curve_,
                                  isBent() ? colours.accent : colours.textMuted, 1.2f);
}

} // namespace synth
