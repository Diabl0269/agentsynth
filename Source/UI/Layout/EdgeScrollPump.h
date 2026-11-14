#pragma once

// EdgeScrollPump.h
//
// The one clock behind every drag that follows its view at an edge (the patch canvas, the controller surface and
// every timeline drag: CanvasEdgeScroller, EdgeScrollTimer). It runs one frame per display refresh (a
// juce::VBlankAttachment on the host) while a follow is live, and turns a target speed into an even step per frame:
// the speed eases toward the target over kRampSeconds (no jump from rest to full speed, no dead stop), and the part of
// a step smaller than the view's pixel is carried to the next frame. See docs/layout/layout.md "Edge scroll while
// dragging" and docs/layout/animation.md "Edge scroll".

#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class EdgeScrollPump {
public:
    /** How long the speed takes to go from rest to full (or back). */
    static constexpr double kRampSeconds = 0.1;
    /** The longest frame the pump steps by: a stall (a rebuild, a modal) never turns into one big jump. */
    static constexpr double kMaxFrameSeconds = 1.0 / 30.0;
    /** The frame frameForTest steps by when none is given. */
    static constexpr double kTestFrameSeconds = 1.0 / 30.0;

    /** Frames come from the display `host` is on; `host` must outlive the pump. */
    explicit EdgeScrollPump(juce::Component& host);
    ~EdgeScrollPump();

    /** One frame: call step() for each axis that moves, then apply what it returns. */
    std::function<void()> onFrame;

    /** Starts the frames (a no-op while running). A start from rest begins at speed zero. */
    void start();
    /** Stops the frames and drops the speed; safe from inside onFrame. */
    void stop();
    bool isRunning() const noexcept { return running_; }

    /** Inside onFrame: eases `axis`'s speed toward `target` (-1..1 of `maxPerSec`) and returns this frame's move, in
     *  whole multiples of `quantum` (0 keeps fractions), carrying the rest to the next frame. Once per axis a frame. */
    double step(int axis, double target, double maxPerSec, double quantum) noexcept;
    /** The view could not move along `axis` (a clamp): its speed and carried fraction drop to zero. */
    void halt(int axis) noexcept;
    /** Every axis is at rest and asked to stay there: the follow has eased to a stop. */
    bool settled() const noexcept;

    /** The size of one device pixel in the host's own units (half a pixel on a Retina screen); 1 off screen. */
    double devicePixel() const;

    /** Test seam: one frame of `dtSeconds`, as if the display had run it. */
    void frameForTest(double dtSeconds = kTestFrameSeconds);

private:
    void runFrame(double dtSeconds);
    void onVBlank();

    struct Axis {
        double speed = 0.0;     // eased, -1..1 of the caller's maximum
        double target = 0.0;    // what the last step asked for
        double remainder = 0.0; // the caller's units not yet moved
    };

    juce::Component& host_;
    std::unique_ptr<juce::VBlankAttachment> frames_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::array<Axis, 2> axes_{};
    bool running_ = false;
    bool inFrame_ = false;
    double lastFrameMs_ = 0.0;
    double dt_ = kTestFrameSeconds;
};

} // namespace synth::ui
