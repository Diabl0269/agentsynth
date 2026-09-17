// Concern: FRO150 -- MixerFaderSlider's own mouse/wheel handling (Shift fine-drag, Cmd-click and
// double-click reset, Shift fine-wheel). See MixerFaderSlider.h's class comment for why every
// override below is self-contained rather than calling the juce::Slider base class.
#include "MixerFaderSlider.h"

namespace synth::ui {

namespace {

// Shift = 1/8 rate, for both a drag and a wheel step -- one constant, one feel, both places the
// ticket asks for "fine adjustment".
constexpr double kFineRateFactor = 1.0 / 8.0;

// Mirrors juce::Slider::Pimpl::getMouseWheelDelta's own proportion-space sensitivity (0.15 of the
// slider's travel per unit of wheel.deltaY), instead of stepping a FIXED dB amount per wheel
// EVENT. A fixed per-event step broke on trackpads: a two-finger scroll delivers dozens of
// small-deltaY events per gesture (plus an inertial tail), and treating every one of them as a
// full step made the fader fly 20+ dB in a single flick. Scaling by deltaY in proportion space
// (not dB space) keeps a trackpad's tiny events tiny, keeps a traditional wheel's larger notches
// larger, and stays consistent with mouseDrag() above (same taper-aware conversion).
constexpr double kWheelProportionSensitivity = 0.15;

// The param's own 0.1 dB grid (ChannelStripModule/MasterModule's gain NormalisableRange interval).
// This class has no param access (see the class header comment), so it's a local constant rather
// than read from anywhere else. Floors a wheel step to at least one grid tick in the requested
// direction so a small deltaY-scaled raw step never rounds straight back to the value it started
// from via snapToLegalValue -- juce::Slider's own wheel handling has the identical floor
// (jmax(interval, abs(delta))), for the identical reason.
constexpr double kFaderIntervalDb = 0.1;

} // namespace

void MixerFaderSlider::reanchor(juce::Point<float> mousePos) {
    anchorMousePos_ = mousePos;
    anchorValue_ = getValue();
}

void MixerFaderSlider::resetToZero() {
    // ONE undo step: onDragStart/onDragEnd (assigned by MixerFader::bind(), see this class's own
    // header comment) bracket the single setValue() the same way a whole drag's calls sit inside
    // one bracket -- never a drag itself (dragging_ stays false).
    if (onDragStart)
        onDragStart();
    setValue(0.0, juce::sendNotificationSync);
    if (onDragEnd)
        onDragEnd();
}

void MixerFaderSlider::mouseDown(const juce::MouseEvent& e) {
    if (!isEnabled())
        return;

    if (e.mods.isCommandDown()) {
        // Cmd-click (Ctrl-click on Windows -- isCommandDown() is already the platform-correct
        // check) resets to 0 dB, Cubase's convention -- no drag starts.
        resetToZero();
        return;
    }

    dragging_ = true;
    shiftWasDown_ = e.mods.isShiftDown();
    reanchor(e.position);
    if (onDragStart)
        onDragStart();
}

void MixerFaderSlider::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_)
        return;

    const bool shiftIsDown = e.mods.isShiftDown();
    if (shiftIsDown != shiftWasDown_) {
        // Re-anchor at the CURRENT position/value BEFORE the rate changes -- see this class's
        // header comment: only the rate changes from here, the value never jumps.
        reanchor(e.position);
        shiftWasDown_ = shiftIsDown;
    }

    // The real thumb-travel height, NOT getHeight() -- juce::Slider's own LookAndFeel reduces the
    // slider bounds by the thumb's own radius on each end before positioning the thumb inside them
    // (LookAndFeel_V2::getSliderLayout's `sliderBounds.reduce(0, thumbIndent)`, feeding
    // Slider::Pimpl::resized()'s `sliderRegionSize`). Dividing by the full component height instead
    // made the thumb track the cursor at ~85-90% speed and visibly lag on a long drag.
    const double trackLength = (double)juce::jmax(1, getLookAndFeel().getSliderLayout(*this).sliderBounds.getHeight());
    // LinearVertical: moving the mouse UP (smaller y) increases the value.
    const double pixelDelta = (double)(anchorMousePos_.y - e.position.y);
    const double rate = shiftIsDown ? kFineRateFactor : 1.0;
    const double proportionDelta = (pixelDelta / trackLength) * rate;

    // Drag math happens in PROPORTION space (the slider's 0..1 thumb position), not dB space --
    // that's what makes a fixed pixel delta cover more dB near the bottom of the taper than near
    // 0 dB (MixerFaderTaper.h's whole point), and what makes the Shift rate exactly 1/8 of travel
    // regardless of where on the taper the drag started.
    const auto range = getNormalisableRange();
    const double anchorProportion = range.convertTo0to1(anchorValue_);
    const double newProportion = juce::jlimit(0.0, 1.0, anchorProportion + proportionDelta);
    setValue(range.convertFrom0to1(newProportion), juce::sendNotificationSync);
}

void MixerFaderSlider::mouseUp(const juce::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    if (onDragEnd)
        onDragEnd();
}

void MixerFaderSlider::mouseDoubleClick(const juce::MouseEvent&) {
    if (!isEnabled())
        return;
    // Matches Cmd-click above (Cubase: both reset to unity).
    resetToZero();
}

void MixerFaderSlider::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (!isEnabled() || !isScrollWheelEnabled())
        return;
    const float notch = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
    if (notch == 0.0f)
        return;

    const double signedNotch = (double)(wheel.isReversed ? -notch : notch);
    const double rate = e.mods.isShiftDown() ? kFineRateFactor : 1.0;

    // Same proportion-space math as mouseDrag() above: scale by the wheel's own deltaY rather than
    // stepping a fixed dB amount, so many small trackpad events accumulate at a normal rate instead
    // of each flying a full step (see kWheelProportionSensitivity's comment).
    const auto range = getNormalisableRange();
    const double currentProportion = range.convertTo0to1(getValue());
    const double proportionDelta = signedNotch * kWheelProportionSensitivity * rate;
    const double targetProportion = juce::jlimit(0.0, 1.0, currentProportion + proportionDelta);
    double stepDb = range.convertFrom0to1(targetProportion) - getValue();
    if (stepDb == 0.0)
        return;
    // Enforce at least one 0.1 dB grid step in the requested direction -- see kFaderIntervalDb's
    // comment: below this magnitude, snapToLegalValue would otherwise round the target straight
    // back to the current value, so a real (if tiny) wheel notch would move nothing at all.
    if (std::abs(stepDb) < kFaderIntervalDb)
        stepDb = stepDb < 0.0 ? -kFaderIntervalDb : kFaderIntervalDb;

    // Each wheel tick is its own one-undo-step gesture (mirrors MixerFader::nudge()'s keyboard
    // path) -- never coalesced with a mouse drag, which is why this doesn't touch dragging_.
    if (onDragStart)
        onDragStart();
    setValue(getValue() + stepDb, juce::sendNotificationSync);
    if (onDragEnd)
        onDragEnd();
}

} // namespace synth::ui
