// SurfaceRenderer: diff-only motor fader and button LED sends. A 5-pin DIN MCU has about 3 KB/s, so
// nothing is resent unless it changed, the surface asked (device-ready), or the bank moved.
#include "SurfaceRenderer.h"

namespace synth::midi {

namespace {
constexpr int kUnsent = -1;
}

// A touched fader is skipped and keeps its mirror stale on purpose: the hand is the truth while it is
// down ("never fight a moving hand", docs/control/midi-remote.md#controller-feedback). setTouched(false)
// forgets the mirror so the final value goes out on the next render, even if it matches the last send.
void SurfaceRenderer::render(const SurfaceFrame& frame) {
    const bool full = dirty_;
    dirty_ = false;
    for (int strip = 0; strip < mcu::kStripsPerUnit; ++strip) {
        const auto i = static_cast<std::size_t>(strip);
        if (touched_[i])
            continue;
        if (full || sentFader_[i] != frame.fader[i]) {
            sink_.sendFeedback(output_, juce::MidiMessage::pitchWheel(strip + 1, frame.fader[i]));
            sentFader_[i] = frame.fader[i];
        }
    }
    for (int note = 0; note < SurfaceFrame::kManagedLeds; ++note) {
        const auto i = static_cast<std::size_t>(note);
        if (full || sentLed_[i] != frame.led[i]) {
            sink_.sendFeedback(output_, juce::MidiMessage::noteOn(1, note, frame.led[i] ? mcu::kLedOn : mcu::kLedOff));
            sentLed_[i] = frame.led[i];
        }
    }
}

void SurfaceRenderer::setTouched(int strip, bool touched) noexcept {
    if (strip < 0 || strip >= mcu::kStripsPerUnit)
        return;
    const auto i = static_cast<std::size_t>(strip);
    if (touched_[i] && !touched)
        sentFader_[i] = kUnsent;
    touched_[i] = touched;
}

} // namespace synth::midi
