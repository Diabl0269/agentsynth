#pragma once

#include "Modules/Shapes/ShapeLibrary.h"
#include <juce_core/juce_core.h>

namespace synth::ui {

/** What a component offers to take a dragged Shape: it is reached through SoundDropAdapter::shapeDropTarget(), so a
 *  drop arrives with the sound drag's own outlines and cursor. */
class ShapeDropTarget {
public:
    virtual ~ShapeDropTarget() = default;
    /** Whether a shape of this kind would be taken here right now; false makes the target refuse the drag. */
    virtual bool acceptsShapeDrop(synth::shapes::ShapeKind kind) const = 0;
    /** The pointer entered (`over`) or left the target while carrying the shape called `name`. */
    virtual void shapeDragOver(bool over, const juce::String& name) = 0;
    /** The shape was dropped; stamp it as one undo step. */
    virtual void shapeDropped(const synth::shapes::LibraryShape& shape) = 0;
};

} // namespace synth::ui
