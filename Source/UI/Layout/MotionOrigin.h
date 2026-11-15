// MotionOrigin -- the side a growing thing starts from and a leaving thing shrinks toward, shared by every surface
// that grows and shrinks blocks (the Sections strip, the clips under it), so a grow and the exit that reverses it
// draw the same picture. docs/layout/animation.md ("Motion rules", Origin) states the rule.
#pragma once

#include "UI/Layout/UIAnimation.h"
#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

enum class MotionOrigin { Left, Right, Centre };

/** `rect` drawn at `scale` (0..1) toward `anchorX`: the anchor stays put and the far side moves. An edge origin
 *  scales the width only; Centre scales the height too, about the rect's own middle. */
inline juce::Rectangle<float> scaledTowardOrigin(juce::Rectangle<float> rect, float scale, MotionOrigin origin,
                                                 float anchorX) {
    const float left = anchorX + (rect.getX() - anchorX) * scale;
    const float width = rect.getWidth() * scale;
    if (origin != MotionOrigin::Centre)
        return {left, rect.getY(), width, rect.getHeight()};
    const float height = rect.getHeight() * scale;
    return {left, rect.getCentreY() - height * 0.5f, width, height};
}

/** The same scaling as a transform, for a picture drawn by its own paint code at `rect`. */
inline juce::AffineTransform transformTowardOrigin(juce::Rectangle<float> rect, float scale, MotionOrigin origin,
                                                   float anchorX) {
    return juce::AffineTransform::scale(scale, origin == MotionOrigin::Centre ? scale : 1.0f, anchorX,
                                        rect.getCentreY());
}

/** How far (0..1) an exit toward `origin` has gone at linear time `t`. An edge exit follows easeOutCubic, the curve of
 *  the neighbour that closes up behind it, so the shrinking edge and that neighbour stay one line; a Centre exit is
 *  sent back with easeInCubic. */
inline float exitProgress(MotionOrigin origin, float t) {
    return origin == MotionOrigin::Centre ? easeInCubic(t) : easeOutCubic(t);
}

} // namespace synth::ui
