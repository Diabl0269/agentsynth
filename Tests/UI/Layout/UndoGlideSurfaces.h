// UndoGlideSurfaces.h -- the one list of undoable move surfaces. Every thing a user can move and then undo is
// registered here, and UndoGlideSurfaces.EveryMoveSurfaceGlidesOnUndoAndRedo walks the list: after Cmd+Z and
// Cmd+Shift+Z the moved thing must be drawn between its two places on the first frames and settle at the restored one
// (docs/layout/animation.md "Undo and redo glide"). A new move surface adds an entry in surfaces().
#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace undo_glide_test {

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
     * EveryGlideFrameRepaintsEverythingThatChanged fails a KeyedTween surface that returns none. */
    virtual juce::Component* paintedComponent() { return nullptr; }
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
/** The Parametric EQ curve of the card, of the pop-out EQ window, and of the mixer column's thumbnail. */
std::unique_ptr<Surface> makeEqCardCurve();
std::unique_ptr<Surface> makeEqWindowCurve();
std::unique_ptr<Surface> makeEqMixerThumbnail();
/** The Drum Kit pad panel's Level knob (a pad pick glides the same knobs; see DrumKitPadGlideTests). */
std::unique_ptr<Surface> makeDrumPadKnob();
/** The Sequencer card's Key: the pitch bars glide to the notes the new scale plays, and back on undo. */
std::unique_ptr<Surface> makeSequencerKey();
/** A section moved by covering another: its clips in the clip lane, and its block on the Sections strip. */
std::unique_ptr<Surface> makeSectionMoveClips();
std::unique_ptr<Surface> makeSectionStripBlock();

/** Clip contents: the notes inside a MIDI clip whose edge was dragged, one entry per sizing mode and edge in which the
 *  notes change place (UndoGlideSurfacesClipContents.cpp builds them by looping over every mode and both edges). */
void appendClipContentsEntries(std::vector<Entry>& list);

/** The clip itself in the clip lane: its left edge, its right edge and its body, each dragged across a grid line
 *  (UndoGlideSurfacesClipDrag.cpp). drawn() is the clip's edge as drawn, so a drag step that pops fails the
 *  snapped-drag test and the undo of it the restore tests. */
void appendClipDragEntries(std::vector<Entry>& list);

} // namespace undo_glide_test
