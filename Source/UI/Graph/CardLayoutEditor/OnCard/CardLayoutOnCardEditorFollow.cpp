// CardLayoutOnCardEditorFollow.cpp -- the canvas follows the card being edited when something other than the editor
// moves it (an undo or redo, an auto-arrange): editing goes on, and the view pans, eased, by the least that keeps the
// card and its Add control strip on screen. A card that stays visible moves nothing. Reduce Motion, Animations Off
// and an editor that is not showing land the pan at once (a pan has nothing to fade).
// docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "Project/ViewDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>
#include <utility>

namespace synth::ui {

namespace {

constexpr double kFollowMs = 220.0;
constexpr float kFollowMarginPx = 24.0f;

// How far (canvas units) the visible span [lo, hi] must move to hold [from, to] with `margin` to spare; 0 when the span
// already holds it. A span too short for it keeps the start in view.
float shiftToHold(float lo, float hi, float from, float to, float margin) {
    if (from >= lo && to <= hi)
        return 0.0f;
    if (to - from + 2.0f * margin > hi - lo || from < lo)
        return from - margin - lo;
    return to + margin - hi;
}

} // namespace

// Pans the view so the overlay (the card and the strip under it) is on screen. A mouse button down means the user is
// dragging the card themselves, and the canvas's own edge scroll owns that.
void CardLayoutOnCardEditor::followCard() {
    auto* graph = graphEditor_.getComponent();
    if (graph == nullptr || closed_ || getParentComponent() == nullptr ||
        juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return;
    const auto visible = graph->getVisibleCanvasRect();
    const auto view = graph->getViewDoc();
    const auto zoom = std::max(view.zoom, 0.01f);
    const auto area = getBounds().toFloat();
    const float margin = kFollowMarginPx / zoom;
    const float dx = shiftToHold(visible.getX(), visible.getRight(), area.getX(), area.getRight(), margin);
    const float dy = shiftToHold(visible.getY(), visible.getBottom(), area.getY(), area.getBottom(), margin);
    followPump_.stop();
    followFrame_ = nullptr;
    if (dx == 0.0f && dy == 0.0f)
        return;
    const juce::Point<float> from(view.panX, view.panY);
    const juce::Point<float> to(view.panX - dx * zoom, view.panY - dy * zoom);
    const auto setPan = [safe = juce::Component::SafePointer<GraphEditor>(graph)](juce::Point<float> pan) {
        if (safe == nullptr)
            return;
        auto doc = safe->getViewDoc();
        doc.panX = pan.x;
        doc.panY = pan.y;
        safe->applyViewDoc(doc);
    };
    if (prefersReducedMotion() || animationsOff() || !canAnimate())
        return setPan(to);
    // Someone else moving the view mid-pan (the user's own wheel or drag) ends the pan instead of fighting it.
    auto lastSet = std::make_shared<juce::Point<float>>(from);
    followFrame_ = [safe = juce::Component::SafePointer<GraphEditor>(graph), setPan, from, to,
                    lastSet](float elapsedMs) {
        if (safe == nullptr)
            return;
        const auto now = safe->getViewDoc();
        if (std::abs(now.panX - lastSet->x) > 0.5f || std::abs(now.panY - lastSet->y) > 0.5f)
            return;
        const float t = juce::jlimit(0.0f, 1.0f, elapsedMs / (float)kFollowMs);
        *lastSet = from + (to - from) * easeInOutCubic(t);
        setPan(*lastSet);
    };
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto frame = followFrame_;
    followPump_.run(
        kFollowMs, [frame, start] { frame((float)(juce::Time::getMillisecondCounterHiRes() - start)); },
        [this] { followFrame_ = nullptr; });
}

} // namespace synth::ui
