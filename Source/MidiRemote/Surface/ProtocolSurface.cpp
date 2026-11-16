// ProtocolSurface: the ring every protocol decode pushes into. A full ring drops the newest event
// and counts it, the same rule as the generic RemoteEngine lanes -- the MIDI path never waits.
#include "ProtocolSurface.h"

namespace synth::midi {

ProtocolSurface::ProtocolSurface()
    : ring_(kSurfaceRingCapacity) {}

void ProtocolSurface::push(const SurfaceEvent& event) noexcept {
    if (!ring_.push(event))
        noteDropped();
}

} // namespace synth::midi
