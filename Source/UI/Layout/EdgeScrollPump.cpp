#include "EdgeScrollPump.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

EdgeScrollPump::EdgeScrollPump(juce::Component& host)
    : host_(host) {}

EdgeScrollPump::~EdgeScrollPump() { *alive_ = false; }

// Frames come from the host's display (its refresh rate, 60 or 120 Hz), so a pan steps once per drawn frame rather
// than in timer beats that beat against the display. The first frame after a start measures from the start itself.
void EdgeScrollPump::start() {
    if (running_)
        return;
    running_ = true;
    axes_ = {};
    lastFrameMs_ = juce::Time::getMillisecondCounterHiRes();
    if (frames_ == nullptr)
        frames_ = std::make_unique<juce::VBlankAttachment>(&host_, [this] { onVBlank(); });
}

// A VBlankAttachment must not be destroyed from inside its own callback, so a stop from a frame only marks the pump
// stopped and drops the attachment on the next message-loop turn (the TimelinePlayheadOverlay pattern).
void EdgeScrollPump::stop() {
    running_ = false;
    axes_ = {};
    if (frames_ == nullptr)
        return;
    if (!inFrame_) {
        frames_.reset();
        return;
    }
    juce::MessageManager::callAsync([this, alive = std::weak_ptr<bool>(alive_)] {
        if (alive.expired() || running_)
            return;
        frames_.reset();
    });
}

void EdgeScrollPump::onVBlank() {
    if (!running_)
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = (now - lastFrameMs_) * 0.001;
    lastFrameMs_ = now;
    runFrame(dt);
}

void EdgeScrollPump::frameForTest(double dtSeconds) {
    if (running_)
        runFrame(dtSeconds);
}

// A long frame (a stall, a modal loop) is clamped so the view never leaps; a zero or negative one (two callbacks in
// the same millisecond) moves nothing.
void EdgeScrollPump::runFrame(double dtSeconds) {
    dt_ = std::clamp(dtSeconds, 0.0, kMaxFrameSeconds);
    inFrame_ = true;
    if (onFrame)
        onFrame();
    inFrame_ = false;
}

// The speed slews toward the target by at most dt / kRampSeconds of full speed a frame: rest to full (and full to
// rest) takes kRampSeconds at any refresh rate. The move is speed * max * dt, plus what earlier frames could not
// move; only whole quanta (a device pixel, a logical pixel) leave, so every frame draws the view at a pixel it can
// show and the carried fraction keeps the distance per second exact.
double EdgeScrollPump::step(int axis, double target, double maxPerSec, double quantum) noexcept {
    auto& a = axes_[(size_t)std::clamp(axis, 0, 1)];
    a.target = std::clamp(target, -1.0, 1.0);
    const double slew = dt_ / kRampSeconds;
    a.speed += std::clamp(a.target - a.speed, -slew, slew);
    if (a.target == 0.0 && std::abs(a.speed) < 1.0e-9)
        a.speed = 0.0;
    a.remainder += a.speed * maxPerSec * dt_;
    if (a.speed == 0.0 && a.target == 0.0) {
        a.remainder = 0.0;
        return 0.0;
    }
    if (quantum <= 0.0) {
        const double move = a.remainder;
        a.remainder = 0.0;
        return move;
    }
    const double move = std::trunc(a.remainder / quantum) * quantum;
    a.remainder -= move;
    return move;
}

void EdgeScrollPump::halt(int axis) noexcept { axes_[(size_t)std::clamp(axis, 0, 1)] = {}; }

bool EdgeScrollPump::settled() const noexcept {
    return std::all_of(axes_.begin(), axes_.end(), [](const Axis& a) { return a.speed == 0.0 && a.target == 0.0; });
}

// The host's own units are logical pixels scaled by its transform; one device pixel is 1 / (display scale * the
// component's scale) of them. Off screen (a test, a hidden panel) it is one logical pixel.
double EdgeScrollPump::devicePixel() const {
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(host_.getScreenBounds());
    if (display == nullptr || !host_.isShowing())
        return 1.0;
    const double scale = display->scale * (double)juce::Component::getApproximateScaleFactorForComponent(&host_);
    return scale > 0.0 ? 1.0 / scale : 1.0;
}

} // namespace synth::ui
