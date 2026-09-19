#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// MixerFaderSlider.h -- FRO150 (docs/mixer/fader.md): MixerFader's own juce::Slider subclass,
// giving the vertical fader the Cubase drag conventions stock juce::Slider doesn't provide:
// Shift-drag = fine adjust at 1/8 rate, anchored to wherever the drag IS when Shift toggles (never
// a jump), Shift+wheel = a finer wheel step, and Cmd-click (Ctrl-click on Windows)/double-click
// both reset to 0 dB as one undo step.
//
// Deliberately reimplements mouseDown/mouseDrag/mouseUp/mouseDoubleClick/mouseWheelMove from
// scratch rather than layering on top of stock juce::Slider's own mouse handling (all five route
// into juce::Slider::Pimpl, a private internal class) or configuring it via
// setDoubleClickReturnValue: MixerFaderTests.cpp already found that driving synthesized
// MouseEvents through a STOCK juce::Slider's mouseDown/mouseDrag hangs this suite in CI --
// juce::Slider's own internal mouse handling reaches into platform mouse-capture/cursor code the
// rest of this codebase's real-mouse tests never touch (see that file's header comment). None of
// this class's overrides call the Slider base class's versions, so headless tests can drive them
// directly and deterministically (Tests/UI/Mixer/MixerFaderDragTests.cpp), and the fine-drag
// anchor logic -- re-anchor at the CURRENT value whenever Shift toggles mid-drag, so the rate
// changes but the value never jumps -- is ours to get exactly right rather than reverse-engineering
// JUCE's own velocity-mode maths for a case it wasn't built for.
//
// Gesture bracketing reuses juce::Slider's OWN public onDragStart/onDragEnd std::function members
// (not the Slider::Listener sliderDragStarted/sliderDragEnded juce::SliderParameterAttachment
// itself listens for -- those are only ever invoked from the same private Pimpl machinery this
// class bypasses). MixerFader::bind() assigns onDragStart/onDragEnd to bracket the bound
// AudioParameterFloat's beginChangeGesture()/endChangeGesture() directly, the same pattern
// MixerFader::nudge() already uses for the keyboard path -- so every gesture this class starts
// (drag, Cmd-click reset, double-click reset, wheel step) collapses to exactly one undo step
// through the EXISTING MixerFader::parameterGestureChanged bracket, with no second mechanism.

namespace synth::ui {

class MixerFaderSlider : public juce::Slider {
public:
    MixerFaderSlider() = default;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    /** Cmd-click / double-click: reset to 0 dB as one undo step (onDragStart/onDragEnd bracket a
     *  single setValue), never starting a drag. */
    void resetToZero();

    /** Re-anchors the drag at the CURRENT mouse position and value, without changing the value --
     *  called once from mouseDown, and again from mouseDrag whenever Shift's held state changes,
     *  so 1/8-rate fine adjustment is always relative to "where the drag is right now", never the
     *  original mouseDown point (the ticket's "no jump on Shift toggle" requirement: everything
     *  already dragged under the OLD rate is baked into the new anchor, so only the RATE changes
     *  from here on). */
    void reanchor(juce::Point<float> mousePos);

    bool dragging_ = false;
    bool shiftWasDown_ = false;
    juce::Point<float> anchorMousePos_;
    double anchorValue_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFaderSlider)
};

} // namespace synth::ui
