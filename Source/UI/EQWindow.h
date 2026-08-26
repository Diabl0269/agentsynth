#pragma once

#include "EQCurveComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

/** Dialog content for the pop-out Parametric EQ editor.
 *
 *  The module card's inline curve is necessarily small; this hosts a second EQCurveComponent on
 *  the same module so the same points can be edited at a comfortable size. Both views read the
 *  module's parameters and write through the same setters, so they stay in sync automatically —
 *  each one's 30 Hz timer picks the other's edits up on the next tick.
 *
 *  Follows the SettingsWindow pattern: this is just the content Component; the caller wraps it in
 *  a juce::DialogWindow via LaunchOptions::launchAsync().
 */
class EQWindow : public juce::Component {
public:
    explicit EQWindow(ParametricEQModule& eq) {
        curve = std::make_unique<EQCurveComponent>(eq);
        addAndMakeVisible(*curve);

        spectrumToggle.setButtonText("Show Spectrum");
        spectrumToggle.setToggleState(curve->getShowSpectrum(), juce::dontSendNotification);
        spectrumToggle.onClick = [this] { curve->setShowSpectrum(spectrumToggle.getToggleState()); };
        addAndMakeVisible(spectrumToggle);

        hint.setText("Double-click to add or remove a point  -  drag to move  -  scroll over a point for Q",
                     juce::dontSendNotification);
        hint.setJustificationType(juce::Justification::centredLeft);
        hint.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(hint);

        setSize(720, 420);
    }

    ~EQWindow() override = default;

    void resized() override {
        auto area = getLocalBounds().reduced(10);
        auto footer = area.removeFromBottom(26);
        spectrumToggle.setBounds(footer.removeFromLeft(130));
        hint.setBounds(footer);
        curve->setBounds(area);
    }

    /** Forwards the undo bracketing hooks to the hosted curve. */
    void setGestureCallbacks(std::function<void()> onStart, std::function<void()> onEnd) {
        curve->onGestureStart = std::move(onStart);
        curve->onGestureEnd = std::move(onEnd);
    }

    // Testing hooks
    EQCurveComponent& getCurve() { return *curve; }
    juce::ToggleButton& getSpectrumToggle() { return spectrumToggle; }

private:
    std::unique_ptr<EQCurveComponent> curve;
    juce::ToggleButton spectrumToggle;
    juce::Label hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQWindow)
};
