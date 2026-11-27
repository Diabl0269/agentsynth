// UndoGlideSurfaces.h -- the one list of undoable move surfaces. Every thing a user can move and then undo is
// registered here, and UndoGlideSurfaces.EveryMoveSurfaceGlidesOnUndoAndRedo walks the list: after Cmd+Z and
// Cmd+Shift+Z the moved thing must be drawn between its two places on the first frames and settle at the restored one
// (docs/layout/animation.md "Undo and redo glide"). A new move surface adds an entry in surfaces().
#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {
class CurveEditorComponent;
}

namespace undo_glide_test {

/** A dot of a curve surface as drawn right now: where, and how opaque (1 = fully drawn). */
struct DrawnDot {
    double x = 0.0;
    double y = 0.0;
    float alpha = 1.0f;
};

/** The dots a CurveEditorComponent draws right now (UndoGlideSurfacesLfo.cpp). */
std::vector<DrawnDot> curveEditorDots(const synth::ui::CurveEditorComponent& curve);

class Surface {
public:
    virtual ~Surface() = default;
    /** One move through the surface's real edit path (one undo step), left settled. */
    virtual void move() = 0;
    /** The real AppUndoManager::undo() / redo(), then whatever refresh the app runs after it. */
    virtual bool undo() = 0;
    virtual bool redo() = 0;
    /** One coordinate of the moved thing as it is drawn right now. */
    virtual double drawn() const = 0;
    /** True while a glide runs. */
    virtual bool gliding() const = 0;
    /** Advances the running glide to `elapsedMs` after the undo or redo (absolute, like AnimationDriver::stepForTest).
     */
    virtual void stepTo(double elapsedMs) = 0;
    /** How opaque the moved thing is drawn right now (below 1 only during a Reduce Motion fade). */
    virtual float alpha() const { return 1.0f; }
    /** A surface with a snapped live drag: presses the thing and drags it one grid step, the button still down, so
     *  drawn() trails the new line. False (and nothing done) for a surface with no snapped drag. */
    virtual bool dragStep() { return false; }
    /** Lets go of a drag dragStep() left held (the drop, one undo step). */
    virtual void releaseDrag() {}
    /** The component whose paint shows the glide: the owner of the KeyedTween the surface moves things through.
     *  nullptr only for a surface that does not use a KeyedTween (the rows);
     * EveryGlideFrameRepaintsEverythingThatChanged fails a KeyedTween surface that returns none. Callers re-read it
     * after move(), undo() and redo(): a step may rebuild it. */
    virtual juce::Component* paintedComponent() { return nullptr; }
    /** A curve surface's dots as drawn now (empty for a surface that has none). EveryCurveSurfaceKeepsTheOtherDots
     *  SteadyOnUndoAndRedo checks that a dot which exists before and after an undo or redo is drawn in place at alpha 1
     *  on every frame (the same dots flickered more than once). */
    virtual std::vector<DrawnDot> dots() const { return {}; }
    /** True for a component that runs its own glide and repaints itself whole each frame (the Sequencer grid), so no
     *  KeyedTween reports a dirty area: the frame check then counts the whole component as repainted. */
    virtual bool repaintsWholeComponent() const { return false; }
};

struct Entry {
    const char* name;
    /** True when Reduce Motion lands the restored thing at its place and fades it in there; false for the rows, which
     *  land at once. */
    bool fadesUnderReduceMotion;
    std::function<std::unique_ptr<Surface>()> make;
    /** True for a surface that still moves under Reduce Motion, in a glide shortened to 80 ms with no stagger (the
     *  Sequencer's bars): not at its place on the first frame, but settled within the short glide. */
    bool shortGlideUnderReduceMotion = false;
};

/** Every registered surface. */
const std::vector<Entry>& surfaces();

// Factories for the surfaces whose fixtures live in their own units.
std::unique_ptr<Surface> makeTimelineTrackRows();
std::unique_ptr<Surface> makeAutomationLaneReorder();
std::unique_ptr<Surface> makeLfoCardPoint();
/** An automation lane's line when a dot is deleted (Delete) or added (double-click): drawn() is the line's height at
 *  that dot's x (UndoGlideSurfacesLaneLine.cpp). */
std::unique_ptr<Surface> makeLaneLineDelete();
std::unique_ptr<Surface> makeLaneLineAdd();
/** The mod curve box: a dot added by a double-click on empty space, the line easing and the dot growing on undo/redo.
 */
std::unique_ptr<Surface> makeModCurveAddedDot();
/** The mod curve box: a middle dot deleted by a double-click on it, and its undo and redo (a dot added back). */
std::unique_ptr<Surface> makeModCurveDeletedDot();
/** The Parametric EQ curve of the card, of the pop-out EQ window, and of the mixer column's thumbnail. */
std::unique_ptr<Surface> makeEqCardCurve();
std::unique_ptr<Surface> makeEqWindowCurve();
std::unique_ptr<Surface> makeEqMixerThumbnail();
/** A mixer column's insert rows trading places, its send rows trading places, and an insert row's bypass dim. */
std::unique_ptr<Surface> makeMixerInsertRowMove();
std::unique_ptr<Surface> makeMixerSendRowMove();
std::unique_ptr<Surface> makeMixerInsertBypassDim();
/** The Drum Kit pad panel's Level knob (a pad pick glides the same knobs; see DrumKitPadGlideTests). */
std::unique_ptr<Surface> makeDrumPadKnob();
/** The Sequencer card's Key: the pitch bars glide to the notes the new scale plays, and back on undo. */
std::unique_ptr<Surface> makeSequencerKey();
/** The Sequencer card's step bars: a step's note dragged in the Pitch lane, the bar gliding on undo and redo. */
std::unique_ptr<Surface> makeStepGridBars();
/** A section moved by covering another: its clips in the clip lane, and its block on the Sections strip. */
std::unique_ptr<Surface> makeSectionMoveClips();
std::unique_ptr<Surface> makeSectionStripBlock();
/** A toggle that changes a card's jack layout: Dual I/O splitting a Channel Strip's Audio jack into Left and Right. */
std::unique_ptr<Surface> makeDualIOSplit();

/** The Controllers tab: a controller cell dragged to another grid place (UndoGlideSurfacesController.cpp). */
std::unique_ptr<Surface> makeControllerSurfaceCellMove();

/** Clip contents: the notes inside a MIDI clip whose edge was dragged, one entry per sizing mode and edge in which the
 *  notes change place (UndoGlideSurfacesClipContents.cpp builds them by looping over every mode and both edges). */
void appendClipContentsEntries(std::vector<Entry>& list);

/** The clip itself in the clip lane: its left edge, its right edge and its body, each dragged across a grid line
 *  (UndoGlideSurfacesClipDrag.cpp). drawn() is the clip's edge as drawn, so a drag step that pops fails the
 *  snapped-drag test and the undo of it the restore tests. */
void appendClipDragEntries(std::vector<Entry>& list);

/** A colour changed through its real commit path, drawn gliding between the two on undo and redo: the morph map's dot
 *  colour on the card and in the shape window, and the shape window's chips (UndoGlideSurfacesColour.cpp). drawn() is
 *  the red channel, 0..255. */
void appendColourEntries(std::vector<Entry>& list);

/** A track's colour committed through the colour picker, then undone: the timeline header's stripe, the clips, the
 *  mixer strip's dot, the automation lane's stripe and the modulator row's tag (UndoGlideSurfacesColourTrack.cpp). */
void appendTrackColourEntries(std::vector<Entry>& list);

/** A section's colour on the Sections strip, a marker's on the ruler, the key strip's shape dots and the routing
 *  pane's track stripe (UndoGlideSurfacesColourMarks.cpp). */
void appendMarksColourEntries(std::vector<Entry>& list);

/** A macro's colour on the canvas: the open macro's border and chip, the collapsed card, a port's jack and the card of
 *  a macro its track's colour picker recolours too (UndoGlideSurfacesColourGraph.cpp). */
void appendGraphColourEntries(std::vector<Entry>& list);

/** Colour surfaces whose undo still snaps, one line each with the file of its commit path. This list only shrinks:
 *  never add to it, give the surface an entry and a glide instead. */
const std::vector<const char*>& colourSurfacesNotYetCovered();

} // namespace undo_glide_test
