#pragma once

#include "Transport/TransportService.h"
#include <algorithm>

namespace synth::ui {

// The length of a bar in quarter-note beats under the transport's time signature, 4 with no transport.
// The one place the timeline's snap and "one bar" widths read it from.
inline double beatsPerBarFor(const synth::TransportService* transport) {
    if (transport == nullptr)
        return 4.0;
    const auto snap = transport->getPositionSnapshot();
    const double beats = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
    return beats > 0.0 ? beats : 4.0;
}

} // namespace synth::ui
