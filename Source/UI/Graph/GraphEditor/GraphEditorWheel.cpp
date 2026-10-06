// GraphEditorWheel.cpp
//
// Wheel and trackpad gestures on the patch canvas: a wheel or two-finger swipe PANS, a pinch (or
// Cmd/Ctrl+wheel for a mouse) ZOOMS. GraphEditor is declared in GraphEditor.h.

#include "GraphEditor.h"
#include "UI/Timeline/ScrollPolicy.h"

namespace {
// Screen pixels the canvas pans per unit of wheel delta (same 200-per-unit the timeline uses).
constexpr float kPanPixelsPerWheelUnit = 200.0f;
// applyZoomAt multiplies zoom by (1 + delta * 0.1); a pinch scale factor s is delta = (s - 1) / 0.1.
constexpr float kZoomStep = 0.1f;
} // namespace

// A pan is "content follows the fingers" (juce::Viewport convention): the OS has already folded its
// natural-scrolling choice into the deltas (isReversed only reports it), and an inertial tail is just
// more of the same deltas, so both pan with no extra flip. Cmd/Ctrl+wheel is the mouse user's zoom;
// no platform branch is needed (isCommandDown() resolves to Cmd on macOS, Ctrl elsewhere).
void GraphEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.mods.isCommandDown()) {
        applyZoomAt(synth::ui::dominantWheelDelta(wheel), e.position);
        return;
    }

    float dx = wheel.deltaX;
    float dy = wheel.deltaY;
    // macOS already moves Shift+wheel onto deltaX; elsewhere it stays on deltaY, so move it here.
    if (e.mods.isShiftDown() && dx == 0.0f)
        std::swap(dx, dy);

    // A plain mouse notch is one big jump, so it eases in; a trackpad (smooth) or its inertia applies as-is.
    const bool eased = !wheel.isSmooth && !wheel.isInertial;
    panByWheel(0, dx * kPanPixelsPerWheelUnit, eased);
    panByWheel(1, dy * kPanPixelsPerWheelUnit, eased);
}

void GraphEditor::mouseMagnify(const juce::MouseEvent& e, float scaleFactor) {
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
        return;
    applyZoomAt((scaleFactor - 1.0f) / kZoomStep, e.position);
}

void GraphEditor::panByWheel(int axis, float amountPx, bool eased) {
    if (amountPx == 0.0f)
        return;
    const auto read = [this](int a) { return (double)(a == 0 ? panOffset.x : panOffset.y); };
    const auto pan = [this](int a, double d) {
        (a == 0 ? panOffset.x : panOffset.y) += (float)d;
        updateTransform();
    };
    if (eased && wheelPanTween_.push(*this, axis, (double)amountPx, read, pan))
        return;
    wheelPanTween_.stop();
    pan(axis, (double)amountPx);
}
