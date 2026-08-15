#pragma once

#include "EpochExchange.h"
#include "TimelineSnapshot.h"

namespace synth {

// The timeline's message-thread -> audio-thread hand-off (TL2-2). All of the machinery — the single
// atomic pointer, the epoch reclamation protocol, the never-null empty fallback and the threading
// contract — lives in EpochExchange<T> (see EpochExchange.h, which is where the correctness argument
// is written down); TL4-2 lifted it out of here verbatim so the automation binding table could be
// published through the same protocol.
//
// This subclass exists purely to keep the spelling every caller already uses: `emptySnapshot()`
// rather than the template's generic `emptyValue()`. Same object, same semantics, no added state —
// so it is safe that EpochExchange's destructor is non-virtual (an exchange is always held by
// value, never through a base pointer).
class TimelineSnapshotExchange : public EpochExchange<TimelineSnapshot> {
public:
    // The zero-track snapshot handed out before anything is published.
    static const TimelineSnapshot& emptySnapshot() noexcept { return emptyValue(); }
};

} // namespace synth
