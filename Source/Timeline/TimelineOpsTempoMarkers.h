#pragma once

// Reads and describes the timelineOps `setTempo` and `addMarker` ops. Split out of TimelineOps.cpp by
// concern (that file is at its size cap): neither op touches tracks, clips or the graph, so the field
// checks and the preview wording stand alone. Errors carry no "timelineOps[i] (op): " prefix; the caller
// adds it.

#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/** Tempo range an op may ask for. Narrower than the transport's own clamp on purpose: it is the range the
 *  server's grammar offers, and a song outside it is a mistake, not a style. */
inline constexpr double kOpMinTempoBpm = 40.0;
inline constexpr double kOpMaxTempoBpm = 220.0;
/** Longest marker name an op may carry (the server's grammar bound; the doc itself allows more). */
inline constexpr int kOpMaxMarkerNameChars = 40;

/** The checked fields of one `addMarker` op. */
struct MarkerOpFields {
    double beat = 0.0;
    juce::String name;
};

/** Reads `op`'s "bpm" (a number in [kOpMinTempoBpm, kOpMaxTempoBpm]). Empty on success, else the failure text. */
juce::String readSetTempoOp(juce::DynamicObject& op, double& bpm);

/** Reads `op`'s "beat" (0..kMaxPpqUntrusted) and "name" (1..kOpMaxMarkerNameChars chars). Empty on success. */
juce::String readAddMarkerOp(juce::DynamicObject& op, MarkerOpFields& out);

/** Preview phrase, e.g. "sets the tempo to 174 BPM". */
juce::String describeSetTempo(double bpm);

/** Preview phrase for every marker of a batch together, e.g. "adds 3 markers (Intro at beat 0, ...)". Empty for none.
 */
juce::String describeMarkers(const std::vector<MarkerOpFields>& markers);

} // namespace synth
