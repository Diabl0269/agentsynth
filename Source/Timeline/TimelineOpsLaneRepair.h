#pragma once

// Repairs one `writeLane` op before TimelineOps validates it, for an edit plan that should lose a
// small mistake rather than the whole answer: a value outside the parameter's range is clamped into
// it, and an op that names a node or parameter that does not exist is reported so the caller can
// drop just that op. Split out of TimelineOps.cpp by concern (that file is at its size cap). The
// bounds are the ones runWriteLane enforces, so a repaired op always validates.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace synth {

/** What repairWriteLane found. */
struct LaneRepair {
    enum class Outcome { Ok, UnknownNode, UnknownParam };
    Outcome outcome = Outcome::Ok;
    int clamped = 0;       // point values moved into [minAllowed, maxAllowed]
    double minAllowed = 0; // the range, set when the parameter resolved
    double maxAllowed = 0;
    juce::String moduleName;    // the module the op's "nodeUuid" names, set when the node resolved
    juce::String parameterName; // the parameter's display name, set when it resolved
};

/**
 * Checks `op` (an object addressing its node by "nodeUuid") against `graph` and clamps every point "value" into
 * the parameter's range (narrowed by an existing lane's range, as runWriteLane does). Mutates `op` only by
 * clamping. An op that is malformed in any other way reports Ok and is left for TimelineOps to reject.
 */
LaneRepair repairWriteLane(juce::DynamicObject& op, const TimelineDoc& doc, const juce::AudioProcessorGraph& graph);

} // namespace synth
