#pragma once

#include "ReorderCancelKey.h"
#include "ReorderDragAnimator.h"
#include "ReorderFramePump.h"
#include "UI/Layout/ReducedMotion.h"
#include <functional>
#include <memory>
#include <vector>

// ReorderDragSession.h: the owner-side glue every reorder list needs around a ReorderDragAnimator --
// the frame pump that runs only while a tween is in flight, the Esc listener that lives exactly as
// long as the gesture, and the "Esc already cancelled, so the release must not commit" flag. The list keeps
// what is really its own: the slots it feeds in, how it turns the animator's starts into bounds
// (`onFrame`), and what a commit means. For a list inside a viewport follow() also keeps the view scrolling while the
// pointer rests at an edge (ViewportDragFollower: display clock, edge intent). Message thread only.
//
// Owner flow:
//   mouseDown  -> begin(slots, key, grabOffset, pointer)
//   mouseDrag  -> if (dragTo(follow(content, e, axis))) place rows from animator()   (follow: a list in a viewport)
//   mouseUp    -> switch (end()): Commit -> commit the order, lay out statically, release(finalStarts),
//                 place rows; Click -> the press never became a drag; Cancelled / Nothing -> done
//   Esc        -> handled here: abort() glides everything back, then onFrame runs
//   list rebuilt under the drag -> discard()
namespace synth::ui::viewport_drag_scroll {
enum class Axis;
class ViewportDragFollower;
} // namespace synth::ui::viewport_drag_scroll

namespace synth::ui {

class ReorderDragSession {
public:
    /** How a mouse release ends the gesture. */
    enum class End { Nothing, Cancelled, Click, Commit };

    /** `owner` must outlive the session. `onFrame` re-places the owner's rows from animator(); it
     *  runs on every VBlank while a tween is in flight and once after an Esc. */
    ReorderDragSession(juce::Component& owner, std::function<void()> onFrame);
    ~ReorderDragSession();

    ReorderDragAnimator& animator() noexcept { return animator_; }
    const ReorderDragAnimator& animator() const noexcept { return animator_; }

    /** Starts a press: `slots` in display order along the axis, `grabOffset` = pointer minus the
     *  dragged slot's start, both measured once in the owner's coordinates. */
    void begin(const std::vector<ReorderDragAnimator::Slot>& slots, int draggedKey, float grabOffset, float pointer) {
        discard();
        animator_.begin(slots, draggedKey, grabOffset, pointer, animates());
    }

    /** Every drag event of a list inside a viewport: starts or runs the edge follow for the pointer in `e` (the press
     * is the event's mouse-down, so a press inside an edge zone scrolls only once the pointer moves toward that edge)
     * and returns the pointer along `axis` in `content`'s coordinates, to hand to dragTo(). */
    float follow(juce::Component& content, const juce::MouseEvent& e, viewport_drag_scroll::Axis axis);

    /** Feeds the pointer; true once the drag is lifted, i.e. the owner should re-place its rows. */
    bool dragTo(float pointer);

    /** Ends the gesture on mouse release. On Commit the owner must follow with release(). */
    End end();

    /** After the owner committed and laid out statically: glides the rows into `finalStarts`. */
    void release(const std::vector<float>& finalStarts) {
        animator_.release(finalStarts);
        startFramesIfNeeded();
    }

    /** For a drop whose commit already rebuilt the list (this session is on the NEW list): the list is
     *  laid out at its final `slots` and item `key` glides in from `fromStart`, where it was dropped.
     *  The neighbours already sit in their final places. Lands at once when the owner is not showing or Animations
     *  is Off. */
    void settleInto(const std::vector<ReorderDragAnimator::Slot>& slots, int key, float fromStart) {
        discard();
        animator_.begin(slots, key, 0.0f, fromStart - 2.0f * ReorderDragAnimator::kDragThresholdPx, animates());
        animator_.dragTo(fromStart);
        std::vector<float> finalStarts;
        for (const auto& slot : slots)
            finalStarts.push_back(slot.start);
        animator_.release(finalStarts);
        startFramesIfNeeded();
    }

    /** Esc: nothing is committed and everything glides back to where it was picked up. */
    void abort();

    /** The list was rebuilt (or destroyed) under the gesture: everything ends at once. */
    void discard();

    /** True while a row is lifted or still gliding into its slot. */
    bool isReordering() const noexcept { return animator_.isReordering(); }

    /** Test seam: one frame of the edge follow, as the display would run it (the follow runs while the pointer rests
     *  at an edge; off screen no display frame ever comes). */
    void edgeFollowFrameForTest();

    /** Test seam: lets an off-screen owner take the timed path (a headless test of the Reduce Motion gate). */
    void setAnimateOffScreenForTest(bool animate) noexcept { animateOffScreenForTest_ = animate; }

    /** Test seam: delivers Esc through the same listener a real key press reaches. */
    bool sendEscapeForTest() { return cancelKey_.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), &owner_); }

private:
    // Off screen no VBlank arrives, and under Animations Off the frame pump lands at once: either way a tween timed on
    // the real clock would never be seen to finish, so the gesture runs untimed and lands where it is released. Reduce
    // Motion lands it at once too, in the animator itself.
    bool animates() const { return (owner_.isShowing() || animateOffScreenForTest_) && !animationsOff(); }

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
    std::unique_ptr<viewport_drag_scroll::ViewportDragFollower> follower_;
    std::function<void()> onFrame_;
    unsigned generationSeen_ = 0;
    bool cancelled_ = false;
    bool animateOffScreenForTest_ = false;
};

} // namespace synth::ui
