#pragma once

#include "SongSteps.h"

namespace synth::songsteps {

// The text under a built song's "Show details": one line per track (its sound modules and how many notes it plays),
// one line for the sections with their bars, and one line per automation lane. Pure: read from the envelope the build
// returned and the plan's sections, so it is the same on a rebuilt card and testable without a UI. Lines are cut at
// kMaxDetailLines tracks or lanes with a count of how many were left out. Empty when the envelope holds none of it.
juce::String buildSongDetailsText(const juce::var& envelope, const std::vector<PlanSection>& sections);

inline constexpr int kMaxDetailLines = 40;

} // namespace synth::songsteps
