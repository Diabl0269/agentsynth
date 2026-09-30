// Concern: ReorderDragAnimator's insertion maths and tween bookkeeping.
#include "ReorderDragAnimator.h"

#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
double defaultClockMs() { return juce::Time::getMillisecondCounterHiRes(); }
} // namespace

// A tween evaluates from the clock alone, so the owner may query it at any time and a frame that
// arrives late still lands on the right value.
float ReorderDragAnimator::Tween::at(double nowMs) const {
    if (durationMs <= 0.0 || doneAt(nowMs))
        return to;
    const float t = static_cast<float>((nowMs - startMs) / durationMs);
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return from + (to - from) * (easeIn ? easeInCubic(clamped) : easeOutCubic(clamped));
}

ReorderDragAnimator::ReorderDragAnimator(Clock clock)
    : clock_(clock ? std::move(clock) : Clock(defaultClockMs)) {}

void ReorderDragAnimator::begin(const std::vector<Slot>& slots, int draggedKey, float grabOffset, float pointerAtPress,
                                bool animate) {
    cancel();
    animate_ = animate;
    if (draggedKey < 0 || draggedKey >= static_cast<int>(slots.size()))
        return;
    slots_ = slots;
    rest_.clear();
    for (const auto& slot : slots_)
        rest_.push_back(slot.start);
    offsets_.assign(slots_.size(), Tween{});
    dragged_ = draggedKey;
    insertion_ = draggedKey;
    grab_ = grabOffset;
    pointerAtPress_ = pointerAtPress;
    draggedStart_ = rest_[static_cast<size_t>(draggedKey)];
    gap_ = Tween{draggedStart_, draggedStart_, 0.0, 0.0};
    phase_ = Phase::Pressed;
}

// The insertion index is decided by the dragged item's CENTRE against the neighbours' rest
// midpoints. Those midpoints never move during a drag (the committed order is unchanged until
// release), so the answer depends only on the pointer and cannot flip back and forth the way a
// "is the pointer over a neighbour" test does when the items have unequal extents. Pressed against
// either end of the strip the dragged item takes that end outright: with equal extents its centre
// can only ever reach the last neighbour's midpoint, never pass it.
int ReorderDragAnimator::computeInsertion() const {
    const auto& self = slots_[static_cast<size_t>(dragged_)];
    const float low = slots_.front().start;
    const float high = slots_.back().start + slots_.back().extent - self.extent;
    if (draggedStart_ <= low + kEdgeSnapPx)
        return 0;
    if (draggedStart_ >= high - kEdgeSnapPx)
        return static_cast<int>(slots_.size()) - 1;

    const float centre = draggedStart_ + self.extent * 0.5f;
    int before = 0;
    for (size_t key = 0; key < slots_.size(); ++key) {
        if (static_cast<int>(key) == dragged_)
            continue;
        if (slots_[key].start + slots_[key].extent * 0.5f < centre)
            ++before;
    }
    return before;
}

std::vector<int> ReorderDragAnimator::getNewOrder() const {
    std::vector<int> order;
    for (size_t key = 0; key < slots_.size(); ++key)
        if (static_cast<int>(key) != dragged_)
            order.push_back(static_cast<int>(key));
    if (dragged_ >= 0)
        order.insert(order.begin() + std::clamp(insertion_, 0, static_cast<int>(order.size())), dragged_);
    return order;
}

// Items keep the gap the strip had between its first two slots; the strip's first start is fixed.
std::vector<float> ReorderDragAnimator::startsForInsertion(int insertion) const {
    std::vector<int> order;
    for (size_t key = 0; key < slots_.size(); ++key)
        if (static_cast<int>(key) != dragged_)
            order.push_back(static_cast<int>(key));
    order.insert(order.begin() + std::clamp(insertion, 0, static_cast<int>(order.size())), dragged_);

    const float gap = slots_.size() > 1 ? slots_[1].start - (slots_[0].start + slots_[0].extent) : 0.0f;
    std::vector<float> starts(slots_.size(), 0.0f);
    float pos = slots_.front().start;
    for (int key : order) {
        starts[static_cast<size_t>(key)] = pos;
        pos += slots_[static_cast<size_t>(key)].extent + gap;
    }
    return starts;
}

// Every affected item starts its new glide from where it is drawn RIGHT NOW, never from its rest
// position, so crossing two neighbours in quick succession bends the motion instead of snapping
// it back to the start first.
void ReorderDragAnimator::retarget() {
    const double t = now();
    ++generation_;
    const auto starts = startsForInsertion(insertion_);
    for (size_t key = 0; key < slots_.size(); ++key) {
        if (static_cast<int>(key) == dragged_) {
            if (gap_.to != starts[key])
                gap_ = Tween{gap_.at(t), starts[key], t, ms(kMakeRoomMs)};
            continue;
        }
        const float target = starts[key] - rest_[key];
        if (offsets_[key].to != target)
            offsets_[key] = Tween{offsets_[key].at(t), target, t, ms(kMakeRoomMs)};
    }
}

bool ReorderDragAnimator::dragTo(float pointer) {
    if (phase_ != Phase::Pressed && phase_ != Phase::Dragging)
        return false;
    if (phase_ == Phase::Pressed) {
        if (std::abs(pointer - pointerAtPress_) < kDragThresholdPx)
            return false;
        phase_ = Phase::Dragging;
        lift_ = Tween{0.0f, 1.0f, now(), ms(kLiftMs)};
        ++generation_;
    }
    const auto& self = slots_[static_cast<size_t>(dragged_)];
    const float low = slots_.front().start;
    const float high = slots_.back().start + slots_.back().extent - self.extent;
    draggedStart_ = std::clamp(pointer - grab_, low, std::max(low, high));
    const int insertion = computeInsertion();
    if (insertion != insertion_) {
        insertion_ = insertion;
        retarget();
    }
    return true;
}

// The committed layout replaces the rest positions, and every item's remaining distance to them
// becomes a fresh tween towards zero: the dragged item from where it was dropped, its neighbours
// from wherever their make-room glide had got to.
void ReorderDragAnimator::release(const std::vector<float>& finalStarts) {
    const bool animate = animate_;
    if (phase_ != Phase::Dragging || finalStarts.size() != slots_.size()) {
        cancel();
        return;
    }
    const double t = now();
    std::vector<float> current(slots_.size());
    for (size_t key = 0; key < slots_.size(); ++key)
        current[key] = static_cast<int>(key) == dragged_ ? draggedStart_ : rest_[key] + offsets_[key].at(t);

    rest_ = finalStarts;
    bool moving = false;
    for (size_t key = 0; key < slots_.size(); ++key) {
        const float from = current[key] - rest_[key];
        const bool glides = animate && std::abs(from) > 0.5f;
        offsets_[key] = glides ? Tween{from, 0.0f, t, ms(kSettleMs)} : Tween{};
        moving = moving || glides;
    }
    lift_ = animate ? Tween{lift_.at(t), 0.0f, t, ms(kSettleMs)} : Tween{};
    phase_ = moving ? Phase::Settling : Phase::Idle;
    ++generation_;
}

// Nothing is committed, so the rest positions stay the pickup layout: the dragged item returns
// into its origin (easing IN, it is leaving the pointer), and each neighbour retargets from where
// its make-room glide had got to.
void ReorderDragAnimator::abort() {
    if (phase_ != Phase::Dragging) {
        cancel();
        return;
    }
    const double t = now();
    const auto dragged = static_cast<size_t>(dragged_);
    for (size_t key = 0; key < slots_.size(); ++key) {
        if (key == dragged)
            offsets_[key] = Tween{draggedStart_ - rest_[key], 0.0f, t, ms(kSettleMs), true};
        else
            offsets_[key] = Tween{offsets_[key].at(t), 0.0f, t, ms(kMakeRoomMs)};
    }
    lift_ = Tween{lift_.at(t), 0.0f, t, ms(kSettleMs)};
    phase_ = animate_ ? Phase::Settling : Phase::Idle;
    ++generation_;
}

void ReorderDragAnimator::cancel() {
    phase_ = Phase::Idle;
    slots_.clear();
    rest_.clear();
    offsets_.clear();
    dragged_ = -1;
    insertion_ = -1;
}

bool ReorderDragAnimator::isLifted() const {
    if (phase_ == Phase::Dragging)
        return true;
    return phase_ == Phase::Settling && !offsets_[static_cast<size_t>(dragged_)].doneAt(now());
}

bool ReorderDragAnimator::needsFrames() const {
    if (phase_ != Phase::Dragging && phase_ != Phase::Settling)
        return false;
    const double t = now();
    if (!lift_.doneAt(t) || (!gap_.doneAt(t) && phase_ == Phase::Dragging))
        return true;
    return std::any_of(offsets_.begin(), offsets_.end(), [t](const Tween& tween) { return !tween.doneAt(t); });
}

bool ReorderDragAnimator::finishIfSettled() {
    if (phase_ != Phase::Settling || needsFrames())
        return false;
    phase_ = Phase::Idle;
    return true;
}

float ReorderDragAnimator::getLift() const { return isReordering() ? lift_.at(now()) : 0.0f; }

float ReorderDragAnimator::getDraggedStart() const {
    if (phase_ == Phase::Settling)
        return rest_[static_cast<size_t>(dragged_)] + offsets_[static_cast<size_t>(dragged_)].at(now());
    return draggedStart_;
}

float ReorderDragAnimator::getLayoutStart(int key) const {
    if (key < 0 || key >= static_cast<int>(rest_.size()))
        return 0.0f;
    const auto index = static_cast<size_t>(key);
    if (phase_ == Phase::Dragging)
        return key == dragged_ ? gap_.at(now()) : rest_[index] + offsets_[index].at(now());
    if (phase_ == Phase::Settling)
        return key == dragged_ ? rest_[index] : rest_[index] + offsets_[index].at(now());
    return rest_[index];
}

} // namespace synth::ui
