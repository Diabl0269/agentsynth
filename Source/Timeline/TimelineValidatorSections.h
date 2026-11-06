#pragma once

#include "TimelineValidator.h"

namespace synth {

// The "sections" and "nextSectionId" checks of validateTimeline: shape, bounds, caps, one id space, no overlap.
TimelineValidationResult validateTimelineSections(const juce::DynamicObject& root);

} // namespace synth
