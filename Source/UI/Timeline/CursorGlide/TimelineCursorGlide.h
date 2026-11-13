#pragma once

#include "Transport/CursorGlide.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// TimelineCursorGlide.h (docs/timeline/transport.md#gliding-the-cursor): holding a glide key moves the
// transport cursor, slowly at first and faster the longer the key stays down; releasing lands it.
//
// The motion model is Core (Source/Transport/CursorGlide.h, clock-free). This collaborator is the
// glue: it feeds the model's positions to the transport on every VBlank frame while a key is held,
// plays the landing settle, and runs NOTHING when idle -- both the hold frames and the settle go
// through a ReorderFramePump, which stops on its own.
//
// Owned by TimelinePanelComponent, which supplies the Host (the transport, the snap grid, the view
// scroll). Every Host member is required, so a test supplies a clock and observes the writes.

namespace synth::ui {

class TimelineCursorGlide {
public:
    struct Host {
        std::function<double()> nowMs;             // monotonic milliseconds
        std::function<double()> cursorBeat;        // where the transport cursor effectively is now
        std::function<double()> beatsPerBar;       // current time signature
        std::function<double()> gridBeats;         // snap step in beats, 0 when snap is off
        std::function<void(double)> moveCursor;    // locate the transport
        std::function<void(double)> ensureVisible; // keep a beat on screen
        /** Whether `key` (its key code and modifiers) is physically down right now. */
        std::function<bool(const juce::KeyPress&)> isKeyHeld;
    };

    /** `owner` must outlive this object; the settle lands at once while it is not showing. */
    TimelineCursorGlide(juce::Component& owner, Host host);
    ~TimelineCursorGlide();

    /** Message thread only. A glide key went down, or auto-repeated. Returns true (the key is
     *  consumed); a repeat leaves the running hold, and its acceleration, untouched. */
    bool press(synth::GlideDirection direction, const juce::KeyPress& key);

    /** Message thread only. Call when the keyboard state may have changed: ends the hold when the
     *  glide key or its modifiers are no longer down. Cheap when nothing is held. */
    void keyStateMayHaveChanged();

    /** Message thread only. Ends the hold and lands the cursor. No-op when nothing is held. */
    void release();

    bool isHeld() const noexcept { return glide_.isHeld(); }
    /** True while frames are being requested (a hold or a settle in flight); false when idle. */
    bool isAnimating() const noexcept { return holdFramesRunning_ || settleRunning_; }

    /** One hold frame. The pump calls it each VBlank; tests call it with a test clock. */
    void tick();

private:
    void writeCursor(double beat);
    void startSettle(double from, double target);
    void releaseSoon();

    juce::Component& owner_;
    Host host_;
    synth::CursorGlide glide_;
    juce::KeyPress heldKey_;
    ReorderFramePump holdPump_;
    ReorderFramePump settlePump_;
    bool holdFramesRunning_ = false;
    bool settleRunning_ = false;
    double lastBeat_ = 0.0;
    double settleStartMs_ = 0.0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineCursorGlide)
};

} // namespace synth::ui
