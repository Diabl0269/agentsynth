// GraphEditorViewFly.cpp
//
// Showing something on the canvas (a mixer insert, a module the keyboard stepped to, a dropped card) flies the view
// there instead of jumping: the wheel pan's ScrollTweenRunner, run once over the whole distance
// (docs/layout/animation.md#motion-rules, "View jumps animate"). GraphEditor is declared in GraphEditor.h.

#include "GraphEditor.h"
#include "UI/Layout/ReducedMotion.h"

namespace {
constexpr double kViewFlyMs = 200.0; // a view change across the canvas (Motion rules: 160-220 ms)
} // namespace

// The runner ends the fly when anything else moves the view (a canvas drag, an edge scroll, a wheel pan), and every
// zoom or applied view stops it through wheelPanTween_.stop(). A second fly starts from where the view is drawn now.
void GraphEditor::flyViewTo(juce::Point<float> canvasPoint) {
    const auto delta = getLocalBounds().getCentre().toFloat() - canvasPoint * zoomLevel - panOffset;
    if (synth::ui::prefersReducedMotion() || synth::ui::animationsOff() || (delta.x == 0.0f && delta.y == 0.0f)) {
        centreViewOn(canvasPoint);
        return;
    }
    const auto read = [this](int a) { return (double)(a == 0 ? panOffset.x : panOffset.y); };
    const auto pan = [this](int a, double d) {
        (a == 0 ? panOffset.x : panOffset.y) += (float)d;
        updateTransform();
    };
    if (!wheelPanTween_.flyBy(*this, delta.x, delta.y, kViewFlyMs, read, pan))
        centreViewOn(canvasPoint);
}
