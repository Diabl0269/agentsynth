// Concern: driving the cursor-glide motion model from VBlank frames -- hold frames, the landing
// settle, and the key-up detection (see the header).
#include "UI/Timeline/CursorGlide/TimelineCursorGlide.h"

#include "UI/Layout/UIAnimation.h"
#include <algorithm>

namespace synth::ui {

namespace {
// A hold is one run of the frame pump; no real hand keeps a key down this long (ten minutes).
constexpr double kHoldRunMs = 600000.0;
} // namespace

TimelineCursorGlide::TimelineCursorGlide(juce::Component& owner, Host host)
    : owner_(owner)
    , host_(std::move(host))
    , holdPump_(owner)
    , settlePump_(owner) {}

TimelineCursorGlide::~TimelineCursorGlide() {
    holdPump_.stop();
    settlePump_.stop();
}

bool TimelineCursorGlide::press(synth::GlideDirection direction, const juce::KeyPress& key) {
    if (glide_.isHeld())
        return true; // OS key repeat: the ramp keeps running

    // A new press during a settle starts from where the settle had got to, not from the old target.
    double start = host_.cursorBeat();
    if (settleRunning_) {
        settlePump_.stop();
        settleRunning_ = false;
        start = lastBeat_;
    }

    heldKey_ = key;
    glide_.press(direction, host_.nowMs(), start, host_.beatsPerBar());
    lastBeat_ = start;
    holdFramesRunning_ = true;
    holdPump_.run(kHoldRunMs, [this] { tick(); });
    return true;
}

void TimelineCursorGlide::tick() {
    if (!glide_.isHeld())
        return;
    const double now = host_.nowMs();
    // The OS key-up may never reach the panel (focus moved mid-hold), so every frame also checks
    // the physical key state. The release itself runs outside the frame callback: the pump must
    // not be stopped from inside its own frame.
    if (!host_.isKeyHeld(heldKey_)) {
        releaseSoon();
        return;
    }
    if (glide_.isGliding(now))
        writeCursor(glide_.positionAt(now));
}

void TimelineCursorGlide::releaseSoon() {
    juce::MessageManager::callAsync([this, alive = std::weak_ptr<bool>(alive_)] {
        if (alive.expired())
            return;
        release();
    });
}

void TimelineCursorGlide::keyStateMayHaveChanged() {
    if (glide_.isHeld() && !host_.isKeyHeld(heldKey_))
        release();
}

void TimelineCursorGlide::release() {
    if (!glide_.isHeld())
        return;
    holdPump_.stop();
    holdFramesRunning_ = false;

    const auto landing = glide_.release(host_.nowMs(), host_.gridBeats());
    if (landing.settle)
        startSettle(landing.from, landing.target);
    else
        writeCursor(landing.target);
}

void TimelineCursorGlide::writeCursor(double beat) {
    lastBeat_ = beat;
    host_.moveCursor(beat);
    host_.ensureVisible(beat);
}

void TimelineCursorGlide::startSettle(double from, double target) {
    // No frame reaches a component that is not on screen (headless tests, a hidden panel): land now.
    if (!owner_.isShowing()) {
        writeCursor(target);
        return;
    }
    settleStartMs_ = host_.nowMs();
    settleRunning_ = true;
    const auto frame = [this, from, target] {
        const double t = std::clamp((host_.nowMs() - settleStartMs_) / synth::cursor_glide::kSettleMs, 0.0, 1.0);
        writeCursor(from + (target - from) * (double)easeOutCubic((float)t));
    };
    settlePump_.run(synth::cursor_glide::kSettleMs, frame, [this, target] {
        settleRunning_ = false;
        writeCursor(target);
    });
}

} // namespace synth::ui
