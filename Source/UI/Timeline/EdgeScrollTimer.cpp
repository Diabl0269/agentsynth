// EdgeScrollTimer: where the pointer sits against the edges of a drag's view, and the eased step each display frame
// makes from it. The speeds come from edgeScrollVelocity (as a -1..1 target) scaled to kEdgeAutoScrollMaxPxPerSec by
// the shared EdgeScrollPump, so every timeline drag scrolls at the clip lanes' pace at any refresh rate.

#include "UI/Timeline/EdgeScrollTimer.h"

#include "UI/Layout/EdgeScrollPump.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/TimelineViewState.h"

namespace synth::ui {

namespace {
constexpr int kAxisX = 0;
constexpr int kAxisY = 1;
} // namespace

EdgeScrollTimer::EdgeScrollTimer(juce::Component& host)
    : EdgeScrollTimer(host, kEdgeAutoScrollMaxPxPerSec, 1.0) {}

EdgeScrollTimer::EdgeScrollTimer(juce::Component& host, double verticalMaxPerSec, double verticalQuantum)
    : pump_(std::make_unique<EdgeScrollPump>(host))
    , intent_(std::make_unique<EdgeIntent>())
    , verticalMaxPerSec_(verticalMaxPerSec)
    , verticalQuantum_(verticalQuantum) {
    pump_->onFrame = [this] { frame(); };
}

EdgeScrollTimer::~EdgeScrollTimer() = default;

// The intent is asked on every drag event (the pointer only moves then), and the answer is kept for the frames
// between. Leaving the zone does not stop the frames: the speed eases to rest and frame() stops them then.
void EdgeScrollTimer::track(bool following, juce::Point<int> pointer, juce::Rectangle<int> area, bool vertical) {
    pointer_ = pointer;
    area_ = area;
    vertical_ = vertical;
    following_ = following;
    if (following && !inDrag_) {
        const auto at = pendingPress_.value_or(pointer);
        intent_->press(at.x, at.y);
        pendingPress_.reset();
    }
    inDrag_ = following;
    armedX_ = intent_->x.armed(pointer.x, area.getX(), area.getRight(), kEdgeZonePx);
    armedY_ = intent_->y.armed(pointer.y, area.getY(), area.getBottom(), kEdgeZonePx);
    const bool inZone = following && (horizontalTarget() != 0.0 || verticalTarget() != 0.0);
    if (inZone && !pump_->isRunning())
        pump_->start();
    else if (!following && pump_->isRunning())
        pump_->stop();
}

void EdgeScrollTimer::stop() {
    pump_->stop();
    stepX_ = stepY_ = 0.0;
    inDrag_ = false;
    following_ = false;
    pendingPress_.reset();
}

bool EdgeScrollTimer::isRunning() const noexcept { return pump_->isRunning(); }

double EdgeScrollTimer::horizontalTarget() const noexcept {
    if (!following_ || !armedX_)
        return 0.0;
    return edgeScrollVelocity(pointer_.x, area_.getX(), area_.getRight(), kEdgeZonePx, 1.0);
}

double EdgeScrollTimer::verticalTarget() const noexcept {
    if (!following_ || !vertical_ || !armedY_)
        return 0.0;
    return edgeScrollVelocity(pointer_.y, area_.getY(), area_.getBottom(), kEdgeZonePx, 1.0);
}

// The steps are worked out once per frame, before the owner's tick reads them. The timeline origin moves in whole
// logical pixels (its painters round x to logical integers, FollowPlayhead.h), so that is the horizontal quantum.
void EdgeScrollTimer::frame() {
    stepX_ = pump_->step(kAxisX, horizontalTarget(), kEdgeAutoScrollMaxPxPerSec, 1.0);
    stepY_ = pump_->step(kAxisY, verticalTarget(), verticalMaxPerSec_, verticalQuantum_);
    if (pump_->settled()) {
        pump_->stop();
        stepX_ = stepY_ = 0.0;
        return;
    }
    if (onTick)
        onTick();
}

double EdgeScrollTimer::scrollView(TimelineViewState& view) noexcept {
    const double px = stepX_;
    if (px == 0.0 || view.pixelsPerBeat <= 0.0)
        return 0.0;
    const double before = view.firstVisibleBeat;
    view.scrollBeats(px / view.pixelsPerBeat);
    const double moved = (view.firstVisibleBeat - before) * view.pixelsPerBeat;
    if (moved == 0.0)
        pump_->halt(kAxisX);
    return moved;
}

void EdgeScrollTimer::haltVertical() noexcept { pump_->halt(kAxisY); }

void EdgeScrollTimer::frameForTest() { pump_->frameForTest(); }

} // namespace synth::ui
