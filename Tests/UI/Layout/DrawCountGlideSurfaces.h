// DrawCountGlideSurfaces.h -- one Draw surface for DrawCountGlideTests: a box being dragged at a cycle count (grid
// Off), the ways to change the count and the wave it paints. The shape window's pen lives in UndoGlideSurfaces.cpp,
// which already reaches the curve editor's headers (a new .cpp reaching them would grow their include reach).
#pragma once

#include "UI/Layout/PointEditing/DrawCountGlide.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <memory>
#include <string>
#include <vector>

namespace draw_count_glide_test {

using Line = synth::ui::line_morph::Line;
using synth::ui::DrawCountGlide;
using synth::ui::DrawShape;

class DrawSurface {
public:
    virtual ~DrawSurface() = default;
    virtual void begin(DrawShape shape, int cycles) = 0;
    virtual void step(int delta) = 0; // `[` and `]`
    virtual Line drawn() = 0;         // the preview wave as it is painted now
    virtual std::string chip() = 0;
    virtual DrawCountGlide& glide() = 0;
    virtual juce::Component& owner() = 0;
    /** Lets go: the stamp is committed. Returns the committed wave in the same pixels as drawn(). */
    virtual Line release() = 0;
    /** Wave y-range tolerance: a lane's per-column samples take a column to turn a step. */
    virtual bool byLength(DrawShape shape) const = 0;
};

/** The Edit shapes window's pen (UndoGlideSurfaces.cpp). */
std::unique_ptr<DrawSurface> makeShapeWindowPen();

} // namespace draw_count_glide_test
