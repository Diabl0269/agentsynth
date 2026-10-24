// KeyHold.h -- a key held down to move something, run from the frame pump: glue between KeyGlide (how far), the
// keys (still down?) and the owner (what moves). The owner applies distance() to a preview each frame and commits
// once when the hold ends.
#pragma once

#include "UI/Layout/KeyGlide/KeyGlide.h"
#include "UI/Layout/KeyGlide/KeyGlideHost.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class KeyHold {
public:
    explicit KeyHold(juce::Component& owner);

    /** Clock and keys; a test replaces it. */
    KeyGlideHost host = makeDefaultKeyGlideHost();
    /** Multiplies the glide speed each frame (Shift, Alt+Shift); may be null for 1. */
    std::function<double()> speedFactor;
    /** After each frame that may have moved something. */
    std::function<void()> onFrame;
    /** Once, when the key goes up or end() is called. */
    std::function<void()> onEnd;

    bool active() const noexcept { return glide_.isHeld(); }
    int keyCode() const noexcept { return keyCode_; }
    /** The key is physically down now, by the host's reading. */
    bool keyIsDown(int keyCode) const { return host.isArrowDown && host.isArrowDown(keyCode); }

    /** Starts a hold on `keyCode`; `tapDistance` is what the first press already moved. No-op while active. */
    void begin(int keyCode, const KeyGlideConfig& config, double tapDistance);
    /** Tap distance plus everything the glide has covered since (>= 0). */
    double distance() const noexcept { return tap_ + glided_; }
    bool isGliding() const noexcept { return glide_.isGliding(host.nowMs()); }

    /** Ends the hold now (idempotent) and fires onEnd. */
    void end();
    /** Ends the hold at once when its key is no longer down; the owner calls it from keyStateChanged. */
    void checkKeyUp();
    /** One frame, as the pump runs it; tests call it after moving the clock. */
    void tick();

private:
    juce::Component& owner_;
    KeyGlide glide_;
    ReorderFramePump pump_;
    int keyCode_ = 0;
    double tap_ = 0.0;
    double glided_ = 0.0;
    bool ending_ = false;
    unsigned generation_ = 0;
};

} // namespace synth::ui
