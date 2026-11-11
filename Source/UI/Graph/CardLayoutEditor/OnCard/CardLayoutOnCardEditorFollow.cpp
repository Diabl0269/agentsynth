// CardLayoutOnCardEditorFollow.cpp -- the canvas follows the card being edited when something other than the editor
// moves it (an undo or redo, an auto-arrange): editing goes on, and the view pans, eased, by the least that keeps the
// card and its Add control strip on screen. A card that stays visible moves nothing. Reduce Motion, Animations Off
// and an editor that is not showing land the pan at once (a pan has nothing to fade).
// docs/layout/module-card-layout.md#editing-a-layout.
//
// The canvas also follows the editor's own drags: a control, a group or a row of the Add control panel dragged to the
// edge of the canvas view arms the canvas's edge scroll (CanvasEdgeScrollDriver), so a card bigger than the view, or
// one partly off it, can still take a drop anywhere on it and on the "Drop to hide" area under it. After each pan the
// drag is re-aimed at the still pointer through its own handler (the overlay moved with the canvas, so the same
// screen point is a new spot on the card); the release, Esc and the pointer moving back in stop it, and so does the
// card's far side (with its Drop to hide strip) coming clear of the edge, and the drop is the drag's own single write.
// docs/layout/layout.md, "Every drag follows the view".
#include "CardLayoutOnCardEditor.h"
#include "Project/ViewDoc.h"
#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h"
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

// ---- The editor's own drags held at the canvas view's edge ----

namespace {

// A drag event at a screen point, as the overlay would have received it: the handlers read it relative to the overlay.
juce::MouseEvent dragEventAt(juce::Component& overlay, juce::Point<int> screenPos) {
    const auto pos = overlay.getLocalPoint(nullptr, screenPos).toFloat();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), pos,
        juce::ModifierKeys::getCurrentModifiersRealtime().withFlags(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, &overlay, &overlay, now, pos, now, 1, true);
}

} // namespace

// Armed once per drag; every later event only moves the pointer the scroll reads.
void CardLayoutOnCardEditor::followDragAt(juce::Point<int> screenPos, std::function<bool(juce::Point<int>)> reaim) {
    auto* graph = graphEditor_.getComponent();
    if (graph == nullptr || closed_)
        return;
    if (edgeFollowing_)
        return graph->getEdgeScroll().notePointerAt(screenPos);
    edgeFollowing_ = true;
    juce::Component::SafePointer<CardLayoutOnCardEditor> self(this);
    graph->getEdgeScroll().beginPointerDrag(
        screenPos,
        [self, reaim = std::move(reaim)](juce::Point<int> at) {
            auto* editor = self.getComponent();
            if (editor == nullptr || editor->closed_ || !reaim(at)) {
                if (editor != nullptr)
                    editor->edgeFollowing_ = false;
                return false;
            }
            return true;
        },
        // The card and the strip under it (Add control, Drop to hide) are all a drag here can land on.
        [self] { return self != nullptr ? self->getScreenBounds() : juce::Rectangle<int>(); });
}

void CardLayoutOnCardEditor::followControlDrag(const juce::MouseEvent& e) {
    followDragAt(e.getScreenPosition(), [this](juce::Point<int> at) {
        if (!drag_.pressed || !drag_.moving)
            return false;
        dragTo(dragEventAt(*this, at));
        return true;
    });
}

// Esc ends a group drag inside the reorder session, with no call here: the re-aim sees the session no longer dragging.
void CardLayoutOnCardEditor::followGroupDrag(const juce::MouseEvent& e) {
    followDragAt(e.getScreenPosition(), [this](juce::Point<int> at) {
        if (!groupDrag_.pressed || !groupSession_.animator().isDragging())
            return false;
        dragGroup(dragEventAt(*this, at));
        return true;
    });
}

void CardLayoutOnCardEditor::endDragFollow() {
    if (!std::exchange(edgeFollowing_, false))
        return;
    if (auto* graph = graphEditor_.getComponent())
        graph->getEdgeScroll().end();
}

} // namespace synth::ui
