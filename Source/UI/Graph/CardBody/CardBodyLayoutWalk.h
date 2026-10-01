#pragma once

// The card body's layout walk as free functions over a plan, shared by CardBody (live widgets) and
// CardBodyMeasure (a plan never built, so every widget is null and only apply = false is valid).

#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardBody/CardBodyPlan.h"

namespace synth {

/** `indices` (into plan.items) in card order, `columns` knobs per row; returns the y below them. */
int layoutCardBodyItems(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<int>& indices,
                        int columns, int y, const cardbody::BodyGeometry& g, bool apply, bool tabbed);

/** Every section of `plan`, top to bottom; returns the y below them. */
int layoutCardBodySections(const CardBodyPlan& plan, juce::AudioProcessor& module, int y,
                           const cardbody::BodyGeometry& g, bool apply, bool tabbed);

} // namespace synth
