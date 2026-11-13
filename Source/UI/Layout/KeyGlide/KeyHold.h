// KeyHold.h -- arrows held down to move something, run from the frame pump: glue between KeyGlide (how far), the
// keys (still down?) and the owner (what moves). Left/Right move along time and Up/Down along the value, each axis on
// its own glide, so two arrows held at once move diagonally. The owner applies distance(axis) to a preview each frame
// and commits once when the hold ends.
#pragma once

#include "UI/Layout/KeyGlide/KeyGlide.h"
#include "UI/Layout/KeyGlide/KeyGlideHost.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** Left/Right move along time, Up/Down along the value. */
enum class KeyAxis { time, value };

/** How an arrow moves along its axis. */
struct KeyAxisMotion {
    KeyGlideConfig config;
    /** The step a press makes at once, before the glide takes over. */
    double tapDistance = 0.0;
    /** Its arrows join a running hold only while Alt is down. */
    bool joinsWithAltOnly = false;
};

class KeyHold {
public:
    explicit KeyHold(juce::Component& owner);

    /** Clock and keys; a test replaces it. */
    KeyGlideHost host = makeDefaultKeyGlideHost();
    /** Multiplies an axis's glide speed each frame (Shift, Alt+Shift); may be null for 1. */
    std::function<double(KeyAxis)> speedFactor;
    /** After each frame that may have moved something. */
    std::function<void()> onFrame;
    /** Once, when the last arrow goes up or end() is called. */
    std::function<void()> onEnd;

    static bool isArrow(int keyCode) noexcept;
    static KeyAxis axisOf(int keyCode) noexcept;

    bool active() const noexcept { return held_; }
    int keyCode() const noexcept { return keyCode_; }
    /** The key is physically down now, by the host's reading. */
    bool keyIsDown(int keyCode) const { return host.isArrowDown && host.isArrowDown(keyCode); }
    /** An arrow that moves (or may join) this hold is physically down. */
    bool anyArrowDown() const;

    /** Starts a hold on `keyCode` along its axis; `across` is what an arrow of the other axis does if it is held too
     *  (nullopt: it is ignored). No-op while active. */
    void begin(int keyCode, const KeyAxisMotion& along, std::optional<KeyAxisMotion> across = std::nullopt);
    void begin(int keyCode, const KeyGlideConfig& config, double tapDistance) {
        begin(keyCode, KeyAxisMotion{config, tapDistance});
    }
    /** How far the hold has moved along `axis`: taps plus glide, signed (+ is Right or Up). */
    double distance(KeyAxis axis) const noexcept { return axes_[index(axis)].distance; }
    /** How far along the axis the hold started on (>= 0). */
    double distance() const noexcept;
    bool isGliding() const noexcept;

    /** Ends the hold now (idempotent) and fires onEnd. */
    void end();
    /** Ends the hold at once when no arrow of it is down any more; the owner calls it from keyStateChanged. */
    void checkKeyUp();
    /** One frame, as the pump runs it; tests call it after moving the clock. */
    void tick();

private:
    struct Axis {
        KeyAxisMotion motion;
        bool enabled = false;
        KeyGlide glide;
        double distance = 0.0;
    };
    static int index(KeyAxis axis) noexcept { return axis == KeyAxis::time ? 0 : 1; }
    int directionOf(int axis) const;
    bool holds(int axis) const;
    void advanceAxis(int axis, double nowMs);

    juce::Component& owner_;
    std::array<Axis, 2> axes_;
    ReorderFramePump pump_;
    bool held_ = false;
    int keyCode_ = 0;
    bool ending_ = false;
    unsigned generation_ = 0;
};

} // namespace synth::ui
