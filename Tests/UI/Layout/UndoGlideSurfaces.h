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
};

/** Every registered surface. */
const std::vector<Entry>& surfaces();

// Factories for the surfaces whose fixtures live in their own units.
std::unique_ptr<Surface> makeTimelineTrackRows();
std::unique_ptr<Surface> makeAutomationLaneReorder();
std::unique_ptr<Surface> makeLfoCardPoint();

/** Clip contents: the notes inside a MIDI clip whose edge was dragged, one entry per sizing mode and edge in which the
 *  notes change place (UndoGlideSurfacesClipContents.cpp builds them by looping over every mode and both edges). */
void appendClipContentsEntries(std::vector<Entry>& list);

} // namespace undo_glide_test
