#pragma once

// Repairs for a project.generate answer that wires a Sequencer or Poly Sequencer to a track's instrument.
// This mirrors synth-platform's packages/capabilities/src/project-generate/sequencer-repair.ts and the
// post-processing order in capability.ts (`generateProject`); a change there must be ported here.

#include <juce_core/juce_core.h>

namespace synth::projectgenerate {

/** True for a "Sequencer" or "Poly Sequencer" node that carries a pattern ("state"). */
bool isPatternSequencer(const juce::var& node);

/** Drops the placeClips ops on tracks (matched by name) whose new instrument a pattern sequencer feeds, so a part
 * plays once. Returns `envelope` itself when nothing needs repair. */
juce::var dropNotesOnSequencerTracks(const juce::var& envelope);

/** Rewrites a sequencer-to-new-instrument cable to the MIDI sentinel (srcPort and dstPort -1) and gives the
 * sequencer `run` 1 when it states none. Returns `envelope` itself when nothing needs repair. */
juce::var fixSequencerInstrumentWiring(const juce::var& envelope);

/** expandSong, then dropNotesOnSequencerTracks, then fixSequencerInstrumentWiring: what the hosted service does to a
 * model's answer before the app sees it. */
juce::var applyServerRepairs(const juce::var& envelope, juce::StringArray* warnings = nullptr);

} // namespace synth::projectgenerate
