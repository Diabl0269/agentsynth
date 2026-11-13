#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>

namespace synth::ui {

// What a lane needs to be bound to a parameter: the node's uuid, the parameter id and index hint, and the
// parameter's real range. Resolved by the host (it owns the graph), applied to the doc by AutomationLaneActions.
struct LaneTarget {
    juce::String nodeUuid;
    juce::String paramId;
    int paramIndex = -1;
    synth::AutomationLane::RangeSnapshot range;
};

} // namespace synth::ui
