// Concern (docs/control/midi-remote-ui.md#surface-centre): the surface follows a drag held at its edge. A marquee or a
// cell (or a group of cells) dragged into the outer band of the surface pans it, faster nearer the edge, on the same
// gated 30 Hz timer and speed curve the patch canvas uses (CanvasEdgeScroller, canvasEdgePanDelta). The pointer does
// not move while the content does, so after each pan the drag is re-aimed at the still pointer with a synthetic
// mouseDrag through its own handler: the marquee rect grows over what scrolled in, and the cell's content-space delta
// grows, so it keeps moving under the pointer. The release, Esc (until the release) and moving back inward (on the next
// tick) stop it; the
// move is still committed once, on release, by the cell's own onDragEnded. docs/layout/layout.md, "Every drag follows
// the view".

#include "ControllerSurfaceComponent.h"

#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScroller.h"

#include <algorithm>

namespace synth::ui {

namespace {

juce::MouseEvent syntheticDragAt(juce::Component& target, juce::Point<float> pos) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), pos,
        juce::ModifierKeys::getCurrentModifiersRealtime().withFlags(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, &target, &target, now, pos, now, 1, true);
}

} // namespace

void ControllerSurfaceComponent::noteEdgePointer(const juce::MouseEvent& event) {
    edgePointer_ = getLocalPoint(event.eventComponent, event.position);
}

// Never past the content's origin (cells clamp to col/row 0, nothing lives left of or above it) and never past its
// far end; a view already outside that range is left where it is rather than snapped back.
bool ControllerSurfaceComponent::panByForEdgeScroll(juce::Point<float> d) {
    const auto clampAxis = [](float cur, float delta, float viewExtent, float contentExtent) {
        const float lo = std::min(cur, viewExtent - contentExtent);
        const float hi = std::max(cur, 0.0f);
        return std::clamp(cur + delta, lo, hi);
    };
    const float extent = (float)kContentExtent * zoomLevel_;
    const juce::Point<float> next(clampAxis(panOffset_.x, d.x, (float)getWidth(), extent),
                                  clampAxis(panOffset_.y, d.y, (float)getHeight(), extent));
    if (next == panOffset_)
        return false;
    panOffset_ = next;
    updateTransform();
    return true;
}

CanvasEdgeScroller& ControllerSurfaceComponent::edgeScroller() {
    if (edgeScroll_ == nullptr)
        edgeScroll_ = std::make_unique<CanvasEdgeScroller>(CanvasEdgeScroller::Hooks{
            [this] { return getLocalBounds(); }, [this] { return edgePointer_; },
            [this](juce::Point<float> d) { return panByForEdgeScroll(d); },
            [this] {
                return edgeTickForTest_ || juce::Desktop::getInstance().getMainMouseSource().isDragging() ||
                       juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown();
            }});
    return *edgeScroll_;
}

// Armed once per gesture (every drag event calls it; re-arming would restart the timer's phase), and not again after
// Esc stopped it, until the release.
void ControllerSurfaceComponent::beginMarqueeEdgeScroll() {
    if (edgeScrollStoppedByEscape_ || edgeScroller().isActive())
        return;
    // Only while the press is still a marquee: a synthetic event must never fall through to the pan branch.
    edgeScroller().begin([this] {
        if (marqueeArmed_)
            mouseDrag(syntheticDragAt(*this, edgePointer_));
    });
}

void ControllerSurfaceComponent::beginCellEdgeScroll(ControllerSurfaceCell& cell) {
    if (edgeScrollStoppedByEscape_ || edgeScroller().isActive())
        return;
    juce::Component::SafePointer<ControllerSurfaceCell> safe(&cell);
    edgeScroller().begin([this, safe] {
        auto* c = safe.getComponent();
        if (c == nullptr)
            return endEdgeScroll(); // a rebuild took the cell away mid-drag
        c->mouseDrag(syntheticDragAt(*c, c->getLocalPoint(this, edgePointer_)));
    });
}

void ControllerSurfaceComponent::endEdgeScroll() {
    if (edgeScroll_ != nullptr)
        edgeScroll_->end();
}

// True when a pan was running and Esc stopped it.
bool ControllerSurfaceComponent::stopEdgeScrollForEscape() {
    if (edgeScroll_ == nullptr || !edgeScroll_->isActive())
        return false;
    edgeScroll_->end();
    edgeScrollStoppedByEscape_ = true;
    return true;
}

void ControllerSurfaceComponent::tickEdgeScrollForTest() {
    if (edgeScroll_ == nullptr)
        return;
    const juce::ScopedValueSetter<bool> held(edgeTickForTest_, true);
    edgeScroll_->tick();
}

bool ControllerSurfaceComponent::isEdgeScrollActiveForTest() const noexcept {
    return edgeScroll_ != nullptr && edgeScroll_->isActive();
}

} // namespace synth::ui
