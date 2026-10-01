// EQCurveKeyboard.cpp -- how EQCurveComponent is reached from the keyboard and a screen reader: the
// keys, the spoken value of the selected band, and the focus behaviour. EQCurveComponent is declared
// (and its mouse and paint code lives) in EQCurveComponent.h.

#include "EQCurveComponent.h"

#include "ModuleViewAccessibility.h"

namespace {
// A semitone, in octaves, and the Shift (fine) fraction every keyboard step shrinks to.
constexpr float kSemitoneOctaves = 1.0f / 12.0f;
constexpr float kFineFactor = 0.25f;
constexpr float kGainStepDb = 1.0f;
constexpr float kQStepOctaves = 0.25f;
} // namespace

void EQCurveComponent::selectBandRelative(int delta) {
    const int last = ParametricEQModule::kNumBands - 1;
    setSelectedBand(selectedBand < 0 ? 0 : juce::jlimit(0, last, selectedBand + delta));
    repaint();
}

bool EQCurveComponent::toggleSelectedBand() {
    if (selectedBand < 0)
        return false;
    const int band = selectedBand;
    if (eqModule.isBandEnabled(band)) {
        removeBand(band);
    } else {
        beginGesture();
        eqModule.setBandEnabled(band, true);
        endGesture();
    }
    setSelectedBand(band);
    refreshFromModule();
    return true;
}

juce::String EQCurveComponent::getAccessibilityValueText() const {
    if (selectedBand < 0)
        return synth::ui::describeEqCurveSummary(eqModule.getEnabledBandCount(), ParametricEQModule::kNumBands);
    const auto& band = lastBands[(size_t)selectedBand];
    return synth::ui::describeEqBand(selectedBand, band.enabled, band.freqHz, band.gainDb, band.q);
}

void EQCurveComponent::refreshAccessibilityValue() {
    const auto text = getAccessibilityValueText();
    if (text == announcedValueText)
        return;
    announcedValueText = text;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

std::unique_ptr<juce::AccessibilityHandler> EQCurveComponent::createAccessibilityHandler() {
    return synth::ui::makeValueTextHandler(*this, [this] { return getAccessibilityValueText(); });
}

void EQCurveComponent::focusGained(FocusChangeType cause) {
    // Tabbing in lands on a band, so the screen reader has something to read straight away.
    if (cause != focusChangedByMouseClick && selectedBand < 0)
        setSelectedBand(0);
    repaint();
}

bool EQCurveComponent::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (!getWantsKeyboardFocus() || mods.isCommandDown() || mods.isCtrlDown())
        return false;
    const float fine = mods.isShiftDown() ? kFineFactor : 1.0f;
    const int code = key.getKeyCode();

    if (code >= '1' && code < '1' + ParametricEQModule::kNumBands) {
        selectBand(code - '1');
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        const float direction = code == juce::KeyPress::leftKey ? -1.0f : 1.0f;
        if (mods.isAltDown())
            nudgeSelectedBand(direction * kSemitoneOctaves * fine, 0.0f);
        else
            selectBandRelative(direction < 0.0f ? -1 : 1);
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        nudgeSelectedBand(0.0f, (code == juce::KeyPress::upKey ? kGainStepDb : -kGainStepDb) * fine);
        return true;
    }
    if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey) {
        if (ensureSelectedBand())
            nudgeBandQ(selectedBand, (code == juce::KeyPress::pageUpKey ? kQStepOctaves : -kQStepOctaves) * fine);
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) {
        if (selectedBand >= 0 && eqModule.isBandEnabled(selectedBand))
            toggleSelectedBand();
        return true;
    }
    if (code == juce::KeyPress::returnKey) {
        toggleSelectedBand();
        return true;
    }
    return false;
}
