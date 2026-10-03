#pragma once

// Reads and checks the fields of a timelineOps `addInstrumentTrack` op other than "op" and "name"
// (those, the duplicate-name rule and the track cap stay in TimelineOps.cpp beside the other ops).
// Split out of TimelineOps.cpp by concern: this part needs the module factory and the patch
// validator's per-node parameter check, which no other op does.

#include "Timeline/TimelineOps.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/** The checked fields of one `addInstrumentTrack` op. */
struct InstrumentTrackOpFields {
    juce::String instrument; // one of TimelineOps::kAuthorableInstrumentTypes
    bool poly = false;
    std::vector<InstrumentTrackInsert> inserts; // at most TimelineOps::kMaxInstrumentInserts
};

/** Reads `op`'s instrument/poly/instrumentId/inserts. Empty on success, else the failure text. */
juce::String readInstrumentTrackOpFields(juce::DynamicObject& op, InstrumentTrackOpFields& out);

/** The preview's parenthetical, e.g. "Oscillator with envelope and channel strip, inserts: Filter". */
juce::String describeInstrumentTrack(const InstrumentTrackOpFields& fields);

} // namespace synth
