#pragma once

// CableRetractAnimator.h
//
// A removed cable pulls back into its source jack and fades instead of vanishing: on a disconnect from the
// canvas and when undo or redo takes a cable away. GraphEditor diffs the cables drawn before and after the
// change and arms this with the ones that are gone; it keeps their last geometry as ghosts, which the canvas
// paints over the live cables until the tween ends. Pure state like MacroCrossingAnimator: GraphEditor owns the
// driver and calls applyTweenAt()/finish(), which is also what lets a test drive it with no VBlank.

#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include <vector>

class CableRetractAnimator {
public:
    using VisibleCable = graph_editor_types::VisibleCable;

    /** Arms a ghost for every cable in `before` whose id is not in `after`; true when anything was armed. */
    bool arm(const std::vector<VisibleCable>& before, const std::vector<VisibleCable>& after);
    /** `t` is the driver's 0..1 progress. */
    void applyTweenAt(float t) noexcept;
    void finish() noexcept;
    bool isLive() const noexcept { return !ghosts_.empty(); }
    /** The ghosts as drawn now: each destination end drawn back toward its source. */
    std::vector<VisibleCable> ghosts() const;
    /** How opaque the ghosts are drawn now, 1 at the start of the retract and 0 at its end. */
    float opacity() const noexcept { return 1.0f - progress_; }

private:
    std::vector<VisibleCable> ghosts_;
    float progress_ = 1.0f;
};
