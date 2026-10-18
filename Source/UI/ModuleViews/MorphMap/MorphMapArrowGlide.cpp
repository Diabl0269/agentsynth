// MorphMapArrowGlide.cpp -- holding an arrow on a dot, the cursor or a slider: the first press steps once, and a hold
// glides on (MorphMapGlide is the model; this is the glue to the frame pump and the keys).
#include "MorphMap.h"
#include "MorphMapInternal.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include "UI/Timeline/CursorGlide/PhysicalKeyState.h"
#include <algorithm>
#include <iterator>
#include <optional>

namespace synth::ui {

namespace {
// A hold is one run of the frame pump; no real hand keeps a key down this long (ten minutes).
constexpr double kHoldRunMs = 600000.0;

std::optional<ArrowKey> arrowOf(int keyCode) {
    if (keyCode == juce::KeyPress::leftKey)
        return ArrowKey::Left;
    if (keyCode == juce::KeyPress::rightKey)
        return ArrowKey::Right;
    if (keyCode == juce::KeyPress::upKey)
        return ArrowKey::Up;
    if (keyCode == juce::KeyPress::downKey)
        return ArrowKey::Down;
    return std::nullopt;
}

/** The OS's own view where there is one (a held Cmd chord loses its key-up in JUCE's state on macOS), JUCE's otherwise.
 */
bool arrowPhysicallyDown(int keyCode) {
    std::optional<bool> physical;
    if (const auto arrow = arrowOf(keyCode))
        physical = physicalArrowKeyDown(*arrow);
    return physical.value_or(juce::KeyPress::isKeyCurrentlyDown(keyCode));
}

const int kArrows[] = {juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::upKey,
                       juce::KeyPress::downKey};
} // namespace

MorphMap::GlideHost MorphMap::makeDefaultGlideHost() const {
    GlideHost host;
    host.nowMs = [] { return juce::Time::getMillisecondCounterHiRes(); };
    host.isArrowDown = arrowPhysicallyDown;
    host.isShiftDown = [] { return juce::ModifierKeys::currentModifiers.isShiftDown(); };
    return host;
}

void MorphMap::setGlideHostForTest(GlideHost host) { glideHost_ = std::move(host); }

juce::Point<float> MorphMap::stopPosition(int stopIndex) const {
    if (stopIndex < 0)
        return cursor_;
    if (stopIndex >= (int)dots_.size())
        return {};
    return {dots_[(size_t)stopIndex].x, dots_[(size_t)stopIndex].y};
}

/** A press steps once, exactly as a tap always has. If the arrow is physically down the hold then runs: every frame
    moves the stop on while the arrows stay down, inside the one gesture bracket opened by that first step. A press
    whose key is not physically down (a synthesised key event, an assistive tool) is a plain step. */
bool MorphMap::pressArrow(Stop& stop, int keyCode, bool shift) {
    const bool anyDown = std::any_of(std::begin(kArrows), std::end(kArrows), glideHost_.isArrowDown);
    if (glide_.isHeld()) {
        if (anyDown)
            return true; // OS auto-repeat or a second arrow: the running hold picks the direction up
        endGlide();      // a hold whose key-up was missed
    }
    const float step = shift ? kBigStep : kStep;
    const int along = (keyCode == juce::KeyPress::rightKey || keyCode == juce::KeyPress::downKey) ? 1 : -1;
    float dx = keyCode == juce::KeyPress::leftKey ? -step : keyCode == juce::KeyPress::rightKey ? step : 0.0f;
    float dy = keyCode == juce::KeyPress::upKey ? -step : keyCode == juce::KeyPress::downKey ? step : 0.0f;
    if (stop.isStrip()) { // a slider moves along its own axis; either pair of arrows points along it
        dx = stop.axis() == Axis::X ? step * (float)along : 0.0f;
        dy = stop.axis() == Axis::Y ? step * (float)along : 0.0f;
    }
    if (!glideHost_.isArrowDown(keyCode))
        return stepStop(stop, dx, dy);

    glideStopIndex_ = stop.getIndex();
    glideGestureOpen_ = false;
    glideEnding_ = false;
    const auto from = stopPosition(glideStopIndex_);
    const juce::Point<float> to{juce::jlimit(0.0f, 1.0f, from.x + dx), juce::jlimit(0.0f, 1.0f, from.y + dy)};
    moveGlideTarget(to);
    glide_.press(glideHost_.nowMs(), {(double)to.x, (double)to.y});
    if (glidePump_ == nullptr)
        glidePump_ = std::make_unique<ReorderFramePump>(*this);
    glidePump_->run(kHoldRunMs, [this] { glideTick(); });
    return true;
}

/** Moves the held stop to `to` (a slider only along its own axis), opening the gesture bracket on the first real
    movement so a hold against the map edge never reaches the undo stack. */
void MorphMap::moveGlideTarget(juce::Point<float> to) {
    const int index = glideStopIndex_;
    if (index >= (int)dots_.size())
        return;
    if (index == -2)
        to.y = cursor_.y;
    else if (index == -3)
        to.x = cursor_.x;
    if (to == stopPosition(index))
        return;
    const Gesture kind = index < 0 ? Gesture::Cursor : Gesture::Dot;
    if (!glideGestureOpen_) {
        glideGestureOpen_ = true;
        if (onGestureStart)
            onGestureStart(kind);
    }
    if (index < 0)
        moveCursor(to);
    else
        moveDot(index, to);
}

/** Each frame the arrows that are down give the direction (a slider takes either pair along its axis) and Shift the
    speed, so pressing a second arrow mid-hold turns it diagonal. When none is down the hold ends, one turn later: the
    frame pump must not be stopped from inside its own frame. */
void MorphMap::glideTick() {
    if (!glide_.isHeld() || glideEnding_)
        return;
    const auto& host = glideHost_;
    const bool left = host.isArrowDown(juce::KeyPress::leftKey);
    const bool right = host.isArrowDown(juce::KeyPress::rightKey);
    const bool up = host.isArrowDown(juce::KeyPress::upKey);
    const bool down = host.isArrowDown(juce::KeyPress::downKey);
    if (!left && !right && !up && !down) {
        glideEnding_ = true;
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MorphMap>(this)] {
            if (safe != nullptr)
                safe->endGlide();
        });
        return;
    }
    int dx = (right ? 1 : 0) - (left ? 1 : 0);
    int dy = (down ? 1 : 0) - (up ? 1 : 0);
    if (glideStopIndex_ <= -2) {
        const int along = (right || down ? 1 : 0) - (left || up ? 1 : 0);
        dx = glideStopIndex_ == -2 ? along : 0;
        dy = glideStopIndex_ == -3 ? along : 0;
    }
    const double now = host.nowMs();
    const auto position = glide_.advance(now, dx, dy, host.isShiftDown() ? morph_glide::kShiftSpeedFactor : 1.0);
    if (glide_.isGliding(now))
        moveGlideTarget({(float)position.x, (float)position.y});
}

void MorphMap::endGlide() {
    if (!glide_.isHeld())
        return;
    if (glidePump_ != nullptr)
        glidePump_->stop();
    glide_.release();
    glideEnding_ = false;
    if (glideGestureOpen_) {
        glideGestureOpen_ = false;
        if (onGestureEnd)
            onGestureEnd(glideStopIndex_ < 0 ? Gesture::Cursor : Gesture::Dot);
    }
}

/** Only observes (it returns false so the event keeps bubbling): the end of a hold is seen here as well as by the
    frame poll, so it lands at once and not a frame late. */
bool MorphMap::keyStateChanged(bool /*isKeyDown*/) {
    if (glide_.isHeld() && !std::any_of(std::begin(kArrows), std::end(kArrows), glideHost_.isArrowDown))
        endGlide();
    return false;
}

} // namespace synth::ui
