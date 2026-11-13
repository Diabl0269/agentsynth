// EQSoloButton.cpp -- see EQSoloButton.h.

#include "EQSoloButton.h"

#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"

namespace synth::ui {

EQSoloButton::EQSoloButton(EQCurveComponent& curve)
    : juce::Button("Solo band")
    , curve_(curve) {
    setWantsKeyboardFocus(true);
    setTitle("Solo band");
    onClick = [this] { curve_.toggleSoloSelectedBand(); };
    curve_.addListener(this);
    eqCurveChanged();
}

EQSoloButton::~EQSoloButton() { curve_.removeListener(this); }

void EQSoloButton::eqCurveChanged() {
    const int band = curve_.getSelectedBand();
    const bool usable = band >= 0 && curve_.getBands()[(size_t)band].enabled;
    setEnabled(usable);
    setToggleState(usable && curve_.getSoloBand() == band, juce::dontSendNotification);
    repaint();
}

juce::String EQSoloButton::getTooltip() {
    const auto key = EQCurveComponent::keyHint(curve_.getShortcutManager(), "eqSolo");
    return key.isEmpty() ? juce::String("Solo the selected band") : "Solo the selected band (" + key + ")";
}

void EQSoloButton::paintButton(juce::Graphics& g, bool over, bool) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const bool lit = getToggleState();
    const float corner = 4.0f;
    g.setColour(lit ? colors.accent : colors.surface.brighter(over ? 0.1f : 0.0f));
    g.fillRoundedRectangle(area, corner);
    g.setColour(lit ? colors.accent : colors.border);
    g.drawRoundedRectangle(area, corner, 1.0f);
    const auto ink = lit ? colors.bg0 : colors.textPrimary;
    paintHeadphonesGlyph(g, area.reduced(4.0f), isEnabled() ? ink : ink.withAlpha(0.4f));
    paintFocusRing(g, getLocalBounds().toFloat(), *this, corner);
}

} // namespace synth::ui
