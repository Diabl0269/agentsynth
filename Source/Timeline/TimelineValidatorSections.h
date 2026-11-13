#pragma once

#include "TimelineValidator.h"

#include <cstdint>
#include <functional>

namespace synth {

// Checks one clip of a variation's stored music (`where` names it in messages) and adds its notes to `totalNotes`.
using StoredClipValidator =
    std::function<TimelineValidationResult(const juce::var& clip, const juce::String& where, std::int64_t& totalNotes)>;

// The "sections" and "nextSectionId" checks of validateTimeline: shape, bounds, caps, one id space, no overlap, and
// each variation's letters, own tracks and stored music (clips through `validateClip`, notes counted into
// `totalNotes`).
TimelineValidationResult validateTimelineSections(const juce::DynamicObject& root,
                                                  const StoredClipValidator& validateClip, std::int64_t& totalNotes);

} // namespace synth
