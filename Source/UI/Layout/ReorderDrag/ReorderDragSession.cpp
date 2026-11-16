// ReorderDragSession.cpp -- the parts of the reorder session that drive the edge follow (ViewportDragScroll.h), kept
// out of the header so the lists that own a session do not all include the follower.
#include "ReorderDragSession.h"

#include "ViewportDragScroll.h"

namespace synth::ui {

ReorderDragSession::ReorderDragSession(juce::Component& owner, std::function<void()> onFrame)
    : owner_(owner)
    , frames_(owner)
    , follower_(std::make_unique<viewport_drag_scroll::ViewportDragFollower>(owner))
    , onFrame_(std::move(onFrame)) {
    // A frame of the edge follow moved the view under the resting pointer: re-aim the drag and re-place the rows.
    follower_->onScrolled = [this] {
        if (animator_.isDragging() && dragTo(follower_->locate()) && onFrame_)
            onFrame_();
    };
}

ReorderDragSession::~ReorderDragSession() = default;

float ReorderDragSession::follow(juce::Component& content, const juce::MouseEvent& e, viewport_drag_scroll::Axis axis) {
    follower_->track(content, viewport_drag_scroll::pointerOf(e), axis, animator_.isDragging(),
                     e.getMouseDownScreenPosition());
    return follower_->locate();
}

bool ReorderDragSession::dragTo(float pointer) {
    if (!animator_.isPressed() && !animator_.isDragging())
        return false;
    if (!animator_.dragTo(pointer))
        return false;
    if (!cancelKey_.isArmed())
        cancelKey_.arm(owner_, [this] { abort(); });
    follower_->lifted();
    startFramesIfNeeded();
    return true;
}

ReorderDragSession::End ReorderDragSession::end() {
    cancelKey_.disarm();
    follower_->stop();
    if (cancelled_) {
        cancelled_ = false;
        return End::Cancelled;
    }
    if (animator_.isPressed()) {
        animator_.cancel();
        return End::Click;
    }
    return animator_.isDragging() ? End::Commit : End::Nothing;
}

void ReorderDragSession::abort() {
    cancelKey_.disarm();
    follower_->stop();
    if (!animator_.isDragging())
        return;
    cancelled_ = true;
    animator_.abort();
    startFramesIfNeeded();
    frame();
}

void ReorderDragSession::discard() {
    cancelKey_.disarm();
    follower_->stop();
    frames_.stop();
    animator_.cancel();
    cancelled_ = false;
}

void ReorderDragSession::edgeFollowFrameForTest() { follower_->frameForTest(); }

} // namespace synth::ui
