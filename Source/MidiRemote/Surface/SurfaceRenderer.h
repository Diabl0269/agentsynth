#pragma once

// One Mackie Control unit's feedback (docs/control/midi-remote-mcu-hui.md#4-the-surface-renderer-feedback): a mirror of
// what the hardware shows, and only the differences sent through RemoteFeedbackSink.

#include "MidiRemote/RemoteEngine/RemoteFeedbackSink.h"
#include "MidiRemote/RemoteModel.h"
#include "MidiRemote/Surface/McuProtocol.h"

#include <array>

namespace synth::midi {

/** What the surface should show right now, built by SurfaceController each drain tick. */
struct SurfaceFrame {
    static constexpr int kManagedLeds = mcu::kVPotPushFirstNote; // REC, SOLO, MUTE, SELECT: notes 0-31
    std::array<int, mcu::kStripsPerUnit> fader{};                // 14-bit; an empty slot is 0 (motor down)
    std::array<bool, kManagedLeds> led{};                        // indexed by note number
};

class SurfaceRenderer {
public:
    /** `sink` must outlive the renderer. */
    SurfaceRenderer(RemoteFeedbackSink& sink, ControllerProfile::Input output) noexcept
        : sink_(sink)
        , output_(std::move(output)) {}

    /** Message thread. Sends what differs from the mirror (everything after markDirty). */
    void render(const SurfaceFrame& frame);
    /** The next render resends every element: on bind, device-ready and bank change. */
    void markDirty() noexcept { dirty_ = true; }
    /** A touched fader's motor is never driven; release resends its final value on the next render. */
    void setTouched(int strip, bool touched) noexcept;

    bool isDirty() const noexcept { return dirty_; }

private:
    RemoteFeedbackSink& sink_;
    ControllerProfile::Input output_;
    bool dirty_ = true;
    std::array<bool, mcu::kStripsPerUnit> touched_{};
    std::array<int, mcu::kStripsPerUnit> sentFader_{};
    std::array<bool, SurfaceFrame::kManagedLeds> sentLed_{};
};

} // namespace synth::midi
