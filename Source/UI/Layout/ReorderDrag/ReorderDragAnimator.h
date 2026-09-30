#pragma once

#include <functional>
#include <vector>

// ReorderDragAnimator.h: the pure logic behind every drag-to-reorder list (dock tabs, mixer
// columns, later the timeline track list). Works along ONE axis, so a horizontal strip passes x
// starts and widths and a vertical list passes y starts and heights. No JUCE components, no
// painting, and an injectable clock, so it is unit-tested headlessly.
//
// Owner flow: begin() on press, dragTo() on every drag event, then on release read
// getNewOrder(), commit that order, lay the items out statically, and hand their final starts to
// release(). Between those calls the owner asks getLayoutStart() / getDraggedStart() where to put
// things. Message thread only.
namespace synth::ui {

class ReorderDragAnimator {
public:
    static constexpr double kMakeRoomMs = 160.0;
    static constexpr double kSettleMs = 140.0;
    static constexpr double kLiftMs = 100.0;
    static constexpr float kDragThresholdPx = 4.0f;
    static constexpr float kEdgeSnapPx = 0.5f;

    /** Milliseconds on any monotonic clock. Empty selects juce::Time::getMillisecondCounterHiRes. */
    using Clock = std::function<double()>;

    /** One item's place along the axis. */
    struct Slot {
        float start = 0.0f;
        float extent = 0.0f;
    };

    explicit ReorderDragAnimator(Clock clock = {});

    /** `slots` are in display order; an item's key is its index here. `pointerAtPress` and
     *  `grabOffset` (pointer minus the dragged slot's start) are on the axis, both measured once.
     *  Without `animate` (an off-screen owner gets no frames) every glide lands instantly. */
    void begin(const std::vector<Slot>& slots, int draggedKey, float grabOffset, float pointerAtPress,
               bool animate = true);

    /** Feeds the pointer position; returns true once the drag has passed the threshold. */
    bool dragTo(float pointer);

    /** The dragged key's final index if released now (valid only while lifted). */
    int getInsertionIndex() const noexcept { return insertion_; }
    /** Keys in the display order a release now would produce. */
    std::vector<int> getNewOrder() const;

    /** Lands the drag. `finalStarts[key]` is where the owner's committed layout puts each item. */
    void release(const std::vector<float>& finalStarts);
    /** Abandons the drag with nothing committed: the dragged item glides back to where it was
     *  picked up, its neighbours glide back to their places. */
    void abort();
    /** Ends everything at once, leaving items at their rest starts. */
    void cancel();

    bool isPressed() const noexcept { return phase_ == Phase::Pressed; }
    /** Past the threshold and still held (not yet released or aborted). */
    bool isDragging() const noexcept { return phase_ == Phase::Dragging; }
    /** Dragging past the threshold, or settling: the owner applies offsets while this is true. */
    bool isReordering() const noexcept { return phase_ == Phase::Dragging || phase_ == Phase::Settling; }
    /** The dragged item is still under the pointer, or still gliding into its slot. */
    bool isLifted() const;
    /** True while a tween is in flight, i.e. the owner still needs frames. */
    bool needsFrames() const;
    /** Bumps whenever new tweens start; the owner restarts its frame pump when it changes. */
    unsigned getTweenGeneration() const noexcept { return generation_; }
    /** Ends a settle whose tweens have all finished; true if it did. */
    bool finishIfSettled();

    int getDraggedKey() const noexcept { return dragged_; }
    /** Eases 0 to 1 as the drag begins and back to 0 as it settles. */
    float getLift() const;
    /** Where the lifted item is drawn along the axis (the pointer, clamped, while dragging). */
    float getDraggedStart() const;
    /** Where the item's slot (and any component standing in it) sits; for the dragged key this is
     *  the gap it will land in. */
    float getLayoutStart(int key) const;

private:
    enum class Phase { Idle, Pressed, Dragging, Settling };

    struct Tween {
        float from = 0.0f;
        float to = 0.0f;
        double startMs = 0.0;
        double durationMs = 0.0;
        bool easeIn = false;
        float at(double nowMs) const;
        bool doneAt(double nowMs) const { return nowMs >= startMs + durationMs; }
    };

    int computeInsertion() const;
    std::vector<float> startsForInsertion(int insertion) const;
    void retarget();
    double now() const { return clock_(); }
    double ms(double duration) const { return animate_ ? duration : 0.0; }

    Clock clock_;
    Phase phase_ = Phase::Idle;
    bool animate_ = true;
    std::vector<Slot> slots_;
    std::vector<float> rest_;
    std::vector<Tween> offsets_;
    Tween gap_;
    Tween lift_;
    int dragged_ = -1;
    int insertion_ = -1;
    unsigned generation_ = 0;
    float grab_ = 0.0f;
    float pointerAtPress_ = 0.0f;
    float draggedStart_ = 0.0f;
};

} // namespace synth::ui
