// UndoGlideSurfaces.h -- the one list of undoable move surfaces. Every thing a user can move and then undo is
// registered here, and UndoGlideSurfaces.EveryMoveSurfaceGlidesOnUndoAndRedo walks the list: after Cmd+Z and
// Cmd+Shift+Z the moved thing must be drawn between its two places on the first frames and settle at the restored one
// (docs/layout/animation.md "Undo and redo glide"). A new move surface adds an entry in surfaces().
#pragma once

#include <functional>
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
};

struct Entry {
    const char* name;
    /** False for a surface whose existing glide ignores Reduce Motion (the shared reorder animator). */
    bool honoursReduceMotion;
    std::function<std::unique_ptr<Surface>()> make;
};

/** Every registered surface. */
const std::vector<Entry>& surfaces();

// Factories for the surfaces whose fixtures live in their own units.
std::unique_ptr<Surface> makeTimelineTrackRows();
std::unique_ptr<Surface> makeAutomationLaneReorder();

} // namespace undo_glide_test
