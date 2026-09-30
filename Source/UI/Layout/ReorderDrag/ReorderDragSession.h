#pragma once

#include "ReorderCancelKey.h"
#include "ReorderDragAnimator.h"
#include "ReorderFramePump.h"
#include <functional>
#include <vector>

// ReorderDragSession.h: the owner-side glue every reorder list needs around a ReorderDragAnimator --
// the frame pump that runs only while a tween is in flight, the Esc listener that lives exactly as
// long as the gesture, the drag auto-repeat that keeps events coming while the pointer rests at a
// scroll edge, and the "Esc already cancelled, so the release must not commit" flag. The list keeps
// what is really its own: the slots it feeds in, how it turns the animator's starts into bounds
// (`onFrame`), and what a commit means. Message thread only.
//
// Owner flow:
//   mouseDown  -> begin(slots, key, grabOffset, pointer)
//   mouseDrag  -> if (dragTo(pointer)) place rows from animator()
//   mouseUp    -> switch (end()): Commit -> commit the order, lay out statically, release(finalStarts),
//                 place rows; Click -> the press never became a drag; Cancelled / Nothing -> done
//   Esc        -> handled here: abort() glides everything back, then onFrame runs
//   list rebuilt under the drag -> discard()
namespace synth::ui {

class ReorderDragSession {
public:
    /** How a mouse release ends the gesture. */
    enum class End { Nothing, Cancelled, Click, Commit };

    static constexpr int kDragRepeatMs = 40;

    /** `owner` must outlive the session. `onFrame` re-places the owner's rows from animator(); it
     *  runs on every VBlank while a tween is in flight and once after an Esc. */
    ReorderDragSession(juce::Component& owner, std::function<void()> onFrame)
        : owner_(owner)
        , frames_(owner)
        , onFrame_(std::move(onFrame)) {}

    ReorderDragAnimator& animator() noexcept { return animator_; }
    const ReorderDragAnimator& animator() const noexcept { return animator_; }

    /** Starts a press: `slots` in display order along the axis, `grabOffset` = pointer minus the
     *  dragged slot's start, both measured once in the owner's coordinates. */
    void begin(const std::vector<ReorderDragAnimator::Slot>& slots, int draggedKey, float grabOffset, float pointer) {
        discard();
        animator_.begin(slots, draggedKey, grabOffset, pointer, owner_.isShowing());
    }

    /** Feeds the pointer; true once the drag is lifted, i.e. the owner should re-place its rows. */
    bool dragTo(float pointer) {
        if (!animator_.isPressed() && !animator_.isDragging())
            return false;
        if (!animator_.dragTo(pointer))
            return false;
        if (!cancelKey_.isArmed()) {
            cancelKey_.arm(owner_, [this] { abort(); });
            // A process-wide setting on the mouse source, so only for a real (showing) gesture: a
            // synthesized drag never releases and would leave the repeat running under later code.
            if (owner_.isShowing())
                juce::Component::beginDragAutoRepeat(kDragRepeatMs);
        }
        startFramesIfNeeded();
        return true;
    }

    /** Ends the gesture on mouse release. On Commit the owner must follow with release(). */
    End end() {
        cancelKey_.disarm();
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

    /** After the owner committed and laid out statically: glides the rows into `finalStarts`. */
    void release(const std::vector<float>& finalStarts) {
        animator_.release(finalStarts);
        startFramesIfNeeded();
    }

    /** For a drop whose commit already rebuilt the list (this session is on the NEW list): the list is
     *  laid out at its final `slots` and item `key` glides in from `fromStart`, where it was dropped.
     *  The neighbours already sit in their final places. Lands at once when the owner is not showing. */
    void settleInto(const std::vector<ReorderDragAnimator::Slot>& slots, int key, float fromStart) {
        discard();
        animator_.begin(slots, key, 0.0f, fromStart - 2.0f * ReorderDragAnimator::kDragThresholdPx, owner_.isShowing());
        animator_.dragTo(fromStart);
        std::vector<float> finalStarts;
        for (const auto& slot : slots)
            finalStarts.push_back(slot.start);
        animator_.release(finalStarts);
        startFramesIfNeeded();
    }

    /** Esc: nothing is committed and everything glides back to where it was picked up. */
    void abort() {
        cancelKey_.disarm();
        if (!animator_.isDragging())
            return;
        cancelled_ = true;
        animator_.abort();
        startFramesIfNeeded();
        frame();
    }

    /** The list was rebuilt (or destroyed) under the gesture: everything ends at once. */
    void discard() {
        cancelKey_.disarm();
        frames_.stop();
        animator_.cancel();
        cancelled_ = false;
    }

    /** True while a row is lifted or still gliding into its slot. */
    bool isReordering() const noexcept { return animator_.isReordering(); }

    /** Test seam: delivers Esc through the same listener a real key press reaches. */
    bool sendEscapeForTest() { return cancelKey_.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), &owner_); }

private:
    void frame() {
        animator_.finishIfSettled();
        if (onFrame_)
            onFrame_();
    }

    // A frame pump runs only while a tween is in flight, so a held drag with the pointer at rest and
    // a settled list cost no frames.
    void startFramesIfNeeded() {
        if (animator_.getTweenGeneration() == generationSeen_)
            return;
        generationSeen_ = animator_.getTweenGeneration();
        if (animator_.needsFrames())
            frames_.run(ReorderDragAnimator::kMakeRoomMs + 20.0, [this] { frame(); });
    }

    juce::Component& owner_;
    ReorderDragAnimator animator_;
    ReorderFramePump frames_;
    ReorderCancelKey cancelKey_;
    std::function<void()> onFrame_;
    unsigned generationSeen_ = 0;
    bool cancelled_ = false;
};

} // namespace synth::ui
