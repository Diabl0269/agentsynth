#pragma once

// ShapeListAdapter.h -- what the shape list editor and the morph map need from whatever owns the shapes: read and
// write the list, bracket one undoable edit, and describe the axes. The Filter's Custom shapes are the first
// implementation (FilterShapesAdapter); another shape list (the LFO's custom wave) plugs in the same way.
#include "Modules/Filter/CustomFilterPerNote.h"
#include "Modules/Lfo/LfoCustomWave.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
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

/** Where the Filter's Pitch input stands, which By hand and By pitch need to tell notes apart. */
enum class PitchInput {
    Connected,       ///< A cable feeds it (or the adapter needs none).
    Missing,         ///< No cable, but one can be made from the voice chain's Pitch source.
    MissingNoSource, ///< No cable and nothing upstream carries pitch.
    NotPoly          ///< The module is mono, so notes cannot sit apart at all.
};

/** The grid control a shape list offers above its curve. `names` are the choices, the first being Off. */
struct ShapeGridOffer {
    /** The shared note-length box, or a short row of segments for a small choice. */
    enum class Control { noteLengthBox, segments };
    Control control = Control::noteLengthBox;
    juce::StringArray names;
    juce::String title;
    juce::String tooltip;
    /** The curve editor's grid for a choice; nullopt means free (no grid, no snapping). */
    std::function<std::optional<CurveEditorComponent::CurveGrid>(const juce::String& name)> gridFor;
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
    /** The grid the curve can snap to; none means the window shows no grid control. */
    virtual std::optional<ShapeGridOffer> getGridOffer() const { return std::nullopt; }
    /** Names the kind of list in the settings the window remembers its choices under ("lfo", "filter"). */
    virtual juce::String getPreferenceKind() const { return {}; }
    /** Where those choices are kept; null remembers nothing. */
    virtual juce::ApplicationProperties* getApplicationProperties() const { return nullptr; }
    /** Whether the window offers a live spectrum behind the curve (the Filter's). */
    virtual bool hasSpectrum() const { return false; }
    /** Reads the newest audio and returns the smoothed spectrum (FilterSpectrumAnalyser's points, dB, log frequency
     *  from the curve's left edge to its right), or null when there is none. */
    virtual const std::vector<float>* updateSpectrum() { return nullptr; }
    virtual std::vector<ShapeStartFrom> getStartFrom() const { return {}; }
    virtual ShapeDrawRange getDrawRange() const { return {}; }
    virtual juce::String getWindowTitle() const = 0;

    /** Per-note settings ("Notes move the cursor"). An adapter without them shows no per-note controls. */
    virtual bool hasPerNote() const { return false; }
    virtual CustomFilterPerNote getPerNote() const { return {}; }
    /** Applies the settings live. Like setShapes, not an undo step by itself: bracket with beginEdit/endEdit. */
    virtual void setPerNote(const CustomFilterPerNote&) {}
    /** Whether notes can be told apart: the Pitch input state. An adapter without per-note needs reports Connected. */
    virtual PitchInput pitchInput() const { return PitchInput::Connected; }
    /** Applies `perNote` as ONE undo step (no beginEdit/endEdit around it). For By hand and By pitch with the Pitch
     *  input missing, it also cables the voice chain's Pitch source to it in the same step. True when it cabled. */
    virtual bool setPerNoteConnectingPitch(const CustomFilterPerNote& perNote) {
        beginEdit();
        setPerNote(perNote);
        endEdit();
        return false;
    }
    /** The curve y that is "no change" (a reference line the fill runs down to). */
    virtual float getReferenceLevel() const { return 0.5f; }
};

} // namespace synth::ui
