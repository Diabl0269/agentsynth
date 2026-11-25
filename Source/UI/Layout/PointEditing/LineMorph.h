// LineMorph.h -- a drawn line easing from its old shape into its new one when points are added or removed (or an
// undo or redo changes their number): no dot of the old line pairs with one of the new, so the whole line blends.
// Pure math over left-to-right lists of pixel points, shared by the curve editor and the automation lanes; the owner
// runs the weight 1 -> 0 on its own tween (docs/layout/animation.md#dots-added-and-deleted-in-a-curve-editor).
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth::ui {

namespace line_morph {

using Line = std::vector<juce::Point<float>>;

/** The level of `points` at `x`: linear between samples, held past the ends; at a vertical step the first sample
 *  there. 0 for an empty line. */
float lineYAt(const Line& points, float x);

/** True when two neighbouring samples sit under half a pixel apart in x (a vertical step, dots stacked at one x). */
bool hasVerticalStep(const Line& points);

/** Both lines sampled at every fraction of their length either has, each pair blended by `w` (1 = `before`). */
Line blendByLength(const Line& before, const Line& now, float w);

/** The curve editor's blend: every sample of either line in x order, each level blended by `w` (1 = `before`); by
 *  length instead when either line has a vertical step. */
Line blend(const Line& before, const Line& now, float w);

/** A blend for lines that are functions of x with steps (an automation lane): at every x either line has, the level
 *  arriving and the level leaving are each blended by `w` (1 = `before`), so a step stays a step and every drawn level
 *  lies between the two lines' levels at that x. */
Line blendAcrossSteps(const Line& before, const Line& now, float w);

} // namespace line_morph

} // namespace synth::ui
