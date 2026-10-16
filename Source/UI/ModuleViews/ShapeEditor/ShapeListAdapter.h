#pragma once

// ShapeListAdapter.h -- what the shape list editor and the morph map need from whatever owns the shapes: read and
// write the list, bracket one undoable edit, and describe the axes. The Filter's Custom shapes are the first
// implementation (FilterShapesAdapter); another shape list (the LFO's custom wave) plugs in the same way.
#include "Modules/Lfo/LfoCustomWave.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace synth::ui {

/** One shape: a name, a curve in LfoCustomWave's point model (x across, y in 0..1) and a position on the morph map. */
struct ShapeItem {
    juce::String name;
    LfoCustomWave curve;
    std::vector<float> position; // map x, map y

    bool operator==(const ShapeItem& other) const {
        return name == other.name && curve == other.curve && position == other.position;
    }
};

/** A curve the "Start from" menu can stamp onto a shape. */
struct ShapeStartFrom {
    juce::String name;
    LfoCustomWave curve;
};

/** The band and repeat count a periodic draw shape stamps with. */
struct ShapeDrawRange {
    float lo = 0.5f; // curve y of the shape's low points
    float hi = 0.9f;
    int cycles = 3;
};

class ShapeListAdapter {
public:
    virtual ~ShapeListAdapter() = default;

    /** Message thread only for every member. */
    virtual std::vector<ShapeItem> getShapes() const = 0;
    /** Applies the list live. It is not an undo step by itself: bracket edits with beginEdit/endEdit. */
    virtual void setShapes(const std::vector<ShapeItem>& shapes) = 0;
    /** Changes whenever the list changes, whoever changed it (undo, a preset load, another view). */
    virtual int getGeneration() const = 0;
    /** Remembers the current state; endEdit records one undo step from it to now (none when nothing changed). */
    virtual void beginEdit() = 0;
    virtual void endEdit() = 0;
    virtual int getMaxShapes() const = 0;
    /** A new shape for map position (x, y), e.g. a copy of the blend there. */
    virtual ShapeItem makeShapeAt(float x, float y) const = 0;

    virtual bool hasCursor() const { return false; }
    virtual juce::Point<float> getCursor() const { return {0.5f, 0.5f}; }
    virtual void setCursor(juce::Point<float>) {}
    virtual void beginCursorGesture() {}
    virtual void endCursorGesture() {}

    virtual std::optional<CurveEditorComponent::AxisGuides> getAxisGuides() const { return std::nullopt; }
    virtual std::vector<ShapeStartFrom> getStartFrom() const { return {}; }
    virtual ShapeDrawRange getDrawRange() const { return {}; }
    virtual juce::String getWindowTitle() const = 0;
    /** The curve y that is "no change" (a reference line the fill runs down to). */
    virtual float getReferenceLevel() const { return 0.5f; }
};

} // namespace synth::ui
