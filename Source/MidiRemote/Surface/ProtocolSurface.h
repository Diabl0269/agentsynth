#pragma once

// The MIDI-path half of a protocol control surface
// (docs/control/midi-remote-mcu-hui.md#2-protocolsurface-midi-path-half): one instance per bound port, decoding into
// its own pre-allocated ring. Nothing is applied here.

#include "AudioEngine/SpscRing.h"
#include "MidiRemote/Surface/SurfaceEvent.h"

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace synth::midi {

inline constexpr int kSurfaceRingCapacity = 512;

class ProtocolSurface {
public:
    ProtocolSurface();
    virtual ~ProtocolSurface() = default;
    ProtocolSurface(const ProtocolSurface&) = delete;
    ProtocolSurface& operator=(const ProtocolSurface&) = delete;

    /** MIDI THREAD, one producer per instance. Lock-, allocation- and log-free. */
    virtual void decode(const juce::MidiMessage& message) noexcept = 0;

    /** MESSAGE THREAD. Calls `fn(const SurfaceEvent&)` per queued event, oldest first. */
    template <typename Fn>
    void drain(Fn&& fn) {
        ring_.drain(std::forward<Fn>(fn));
    }

    /** Either thread. Messages dropped on the MIDI path: a full ring, or sysex over kSurfaceSysexBytes. */
    int droppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }

protected:
    void push(const SurfaceEvent& event) noexcept;
    void noteDropped() noexcept { dropped_.fetch_add(1, std::memory_order_relaxed); }

private:
    SpscRing<SurfaceEvent> ring_;
    std::atomic<int> dropped_{0};
};

} // namespace synth::midi
