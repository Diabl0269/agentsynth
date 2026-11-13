#include "KeyHold.h"
#include <cmath>

namespace synth::ui {

namespace {
// A hold is one run of the frame pump; no real hand keeps a key down this long (ten minutes).
constexpr double kHoldRunMs = 600000.0;

// Each axis's two arrows: the one that moves it backwards (Left, Down) and forwards (Right, Up).
const int kArrows[2][2] = {{juce::KeyPress::leftKey, juce::KeyPress::rightKey},
                           {juce::KeyPress::downKey, juce::KeyPress::upKey}};
} // namespace

KeyHold::KeyHold(juce::Component& owner)
    : owner_(owner)
    , pump_(owner) {}

bool KeyHold::isArrow(int keyCode) noexcept {
    return keyCode == juce::KeyPress::leftKey || keyCode == juce::KeyPress::rightKey ||
           keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey;
}

KeyAxis KeyHold::axisOf(int keyCode) noexcept {
    return keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey ? KeyAxis::value : KeyAxis::time;
}

// +1, -1, or 0 when neither (or both) of the axis's arrows is down.
int KeyHold::directionOf(int axis) const {
    return (keyIsDown(kArrows[axis][1]) ? 1 : 0) - (keyIsDown(kArrows[axis][0]) ? 1 : 0);
}

// An axis holds the hold open while one of its arrows is down and it is moving, or may join (an Alt-only axis while
// Alt is up does not: a plain Left/Right during a value hold is someone else's key).
bool KeyHold::holds(int axis) const {
    const auto& a = axes_[(std::size_t)axis];
    if (!a.enabled || (!keyIsDown(kArrows[axis][0]) && !keyIsDown(kArrows[axis][1])))
        return false;
    return a.glide.isHeld() || !a.motion.joinsWithAltOnly || (host.isAltDown && host.isAltDown());
}

bool KeyHold::anyArrowDown() const { return held_ && (holds(0) || holds(1)); }

double KeyHold::distance() const noexcept { return std::abs(distance(axisOf(keyCode_))); }

bool KeyHold::isGliding() const noexcept {
    const double now = host.nowMs ? host.nowMs() : 0.0;
    return axes_[0].glide.isGliding(now) || axes_[1].glide.isGliding(now);
}

// The axis of `keyCode` starts at once with its tap step; the other one waits until one of its arrows is held too.
void KeyHold::begin(int keyCode, const KeyAxisMotion& along, std::optional<KeyAxisMotion> across) {
    if (held_)
        return;
    const int first = index(axisOf(keyCode));
    const double now = host.nowMs();
    for (auto& axis : axes_)
        axis = Axis();
    axes_[(std::size_t)first] = Axis{along, true, KeyGlide(along.config), 0.0};
    axes_[(std::size_t)first].glide.press(now);
    const bool forwards = keyCode == juce::KeyPress::rightKey || keyCode == juce::KeyPress::upKey;
    axes_[(std::size_t)first].distance = (forwards ? 1.0 : -1.0) * along.tapDistance;
    if (across.has_value())
        axes_[(std::size_t)(1 - first)] = Axis{*across, true, KeyGlide(across->config), 0.0};
    held_ = true;
    keyCode_ = keyCode;
    ending_ = false;
    ++generation_;
    pump_.run(kHoldRunMs, [this] { tick(); });
}

// An axis whose arrows are both up (or both down) stops; one that comes back, or joins for the first time, steps its
// tap at once and glides from rest again, as a fresh press would.
void KeyHold::advanceAxis(int axis, double nowMs) {
    auto& a = axes_[(std::size_t)axis];
    const int direction = directionOf(axis);
    if (!a.enabled || direction == 0 || !holds(axis)) {
        a.glide.release();
        return;
    }
    if (!a.glide.isHeld()) {
        a.glide = KeyGlide(a.motion.config);
        a.glide.press(nowMs);
        a.distance += direction * a.motion.tapDistance;
    }
    const KeyAxis which = axis == 0 ? KeyAxis::time : KeyAxis::value;
    a.distance += direction * a.glide.advance(nowMs, speedFactor ? speedFactor(which) : 1.0);
}

// When no arrow is down the hold ends one turn later: the frame pump must not be stopped from inside its own frame.
void KeyHold::tick() {
    if (!held_ || ending_)
        return;
    if (!anyArrowDown()) {
        ending_ = true;
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<juce::Component>(&owner_), this, generation = generation_] {
                if (safe != nullptr && generation == generation_)
                    end();
            });
        return;
    }
    const double now = host.nowMs();
    advanceAxis(0, now);
    advanceAxis(1, now);
    if (onFrame)
        onFrame();
}

void KeyHold::end() {
    if (!held_)
        return;
    pump_.stop();
    axes_[0].glide.release();
    axes_[1].glide.release();
    held_ = false;
    ending_ = false;
    if (onEnd)
        onEnd();
}

void KeyHold::checkKeyUp() {
    if (held_ && !anyArrowDown())
        end();
}

} // namespace synth::ui
