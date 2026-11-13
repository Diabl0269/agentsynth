#pragma once

#include "SequencerPattern.h"

namespace synth {

/** Reads the per-step parameters the sequencers used to have into `pattern`, for projects, presets and AI patches
 *  written before the pattern became state. Mono reads `Pitch N` (0 turns the step off), `Gate N` and `F.Env N`;
 *  poly reads `Step N Root`, `Step N Chord` (a name or an index) and `Gate N`, for N = 1..8. Only keys present are
 *  applied, every value is clamped. Returns whether any key was applied. */
bool adoptLegacyStepParams(SequencerPattern& pattern, const juce::NamedValueSet& params, bool poly);

/** Whether `id` is one of the per-step parameter ids adoptLegacyStepParams reads for this module kind. */
bool isLegacyStepParamId(const juce::String& id, bool poly);

} // namespace synth
