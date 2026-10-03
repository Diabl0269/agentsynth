#pragma once

// Checks on the SHAPE of an AI response, run on its parsed JSON: not "is it valid" (validatePatch and
// the plan validator answer that) but "does it do what the words asked for". Pure functions over a
// juce::var response root (patch keys plus a sibling "timelineOps" list), so the unit tests and
// Tools/AIEvalHarness score a response without a graph or a model.

#include <juce_core/juce_core.h>

namespace synth::soundshape {

/** A verdict plus a short reason: why it passed, or the first thing that is wrong. */
struct ShapeCheck {
    bool pass = false;
    juce::String reason;
};

/** A plucky sound: some envelope (an addInstrumentTrack op's, or an ADSR node's) with sustain <= 0.01, decay <= 0.5,
 * attack <= 0.02. */
ShapeCheck checkPluck(const juce::var& response);

/** A modulation from an envelope (a track's envelope id, or an ADSR node of the response or `existingPatch`) onto a
 * Filter's cutoff. */
ShapeCheck checkFilterEnvelope(const juce::var& response, const juce::var& existingPatch = {});

/** checkFilterEnvelope plus: the Filter is the 24 dB low-pass with resonance >= 60% of its range and the envelope's
 * sustain <= 0.3. */
ShapeCheck checkAcid(const juce::var& response, const juce::var& existingPatch = {});

} // namespace synth::soundshape
