// EQBandTypePill.cpp -- see EQBandTypePill.h.

#include "EQBandTypePill.h"

#include "EQBandKinds.h"
#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"

namespace synth::ui {

EQBandTypePill::EQBandTypePill(EQCurveComponent& curve)
    : juce::Button("Band type")
    , curve_(curve) {
    setWantsKeyboardFocus(true);
    onClick = [this] {
        if (curve_.getSelectedBand() >= 0)
            curve_.showTypeMenu(getScreenBounds());
    };
    curve_.addListener(this);
    eqCurveChanged();
}

EQBandTypePill::~EQBandTypePill() { curve_.removeListener(this); }

juce::String EQBandTypePill::getTooltip() {
    const auto key = EQCurveComponent::keyHint(curve_.getShortcutManager(), "eqTypeMenu");
    return (text_.isEmpty() ? juce::String() : text_ + ". ") + "Band type: press to change it" +
           (key.isEmpty() ? juce::String() : " (" + key + ")") + ", or right-click a point";
}

void EQBandTypePill::eqCurveChanged() {
    const int band = curve_.getSelectedBand();
    text_ = band < 0 ? juce::String() : eqBandTypeName(curve_.getBands()[(size_t)band].type);
    setTitle(band < 0 ? "Band type" : "Band " + juce::String(band + 1) + " type, " + text_);
    setEnabled(band >= 0);
    repaint();
}

void EQBandTypePill::paintButton(juce::Graphics& g, bool over, bool) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const int band = curve_.getSelectedBand();
    if (band < 0)
        return;
    const auto kind = eqBandKindOf(curve_.getBands()[(size_t)band].type);
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(eqBandKindColour(colors, kind).brighter(over ? 0.15f : 0.0f));
    g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
    // The type is its response-curve glyph, then a small chevron saying "opens a menu"; the name is in the tooltip
    // and the accessible title.
    constexpr float kGlyphW = 14.0f;
    constexpr float kGlyphH = 10.0f;
    constexpr float kChevronW = 7.0f;
    constexpr float kGap = 6.0f;
    const auto ink = eqBandKindInk(colors);
    const auto centre = getLocalBounds().toFloat().getCentre();
    const float left = centre.x - (kGlyphW + kGap + kChevronW) * 0.5f;
    paintEqBandTypeGlyph(g, juce::Rectangle<float>(kGlyphW, kGlyphH).withCentre({left + kGlyphW * 0.5f, centre.y}), ink,
                         curve_.getBands()[(size_t)band].type);
    const float cx = left + kGlyphW + kGap + kChevronW * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 3.0f, centre.y - 1.5f);
    chevron.lineTo(cx, centre.y + 1.5f);
    chevron.lineTo(cx + 3.0f, centre.y - 1.5f);
    g.setColour(ink);
    g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    paintFocusRing(g, getLocalBounds().toFloat(), *this, area.getHeight() * 0.5f);
}

std::unique_ptr<juce::AccessibilityHandler> EQBandTypePill::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::button,
        juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] { onClick(); }));
}

} // namespace synth::ui
