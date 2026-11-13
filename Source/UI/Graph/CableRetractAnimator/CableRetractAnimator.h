#pragma once

// CableRetractAnimator.h
//
// Cables move in and out instead of blinking. A removed cable pulls back into its source jack and fades (a
// disconnect from the canvas, and undo or redo taking a cable away); a cable that undo or redo brings back grows
// out of its source jack to its destination. GraphEditor diffs the cables drawn before and after the change and
// arms this: removed cables are kept as ghosts, which the canvas paints over the live cables until the tween ends;
// added cables are real already, so the canvas skips them in its normal pass and draws their growing wire instead.
// Pure state like MacroCrossingAnimator: GraphEditor owns the driver and calls applyTweenAt()/finish(), which is
// also what lets a test drive it with no VBlank.
//
// Full motion is 220 ms. Retract: the end pulls in with easeInOutCubic, the opacity holds at 1 until 60% of the
// tween and then fades to 0. Grow: the end extends with easeOutCubic while the opacity rises to 1 over the first
// 40%. Reduce Motion (docs/layout/animation.md) is an 80 ms alpha fade with no geometry change, out or in.
//
// Chains (Options::morphChains): a module inserted on a cable turns the one cable A -> C into A -> B and B -> C (a
// split), and deleting the middle module of A -> B -> C joins the two cables into A -> C (a join). Neither is a cable
// leaving or arriving, so neither retracts or grows: in a split both new halves start out as the old cable and run to
// the new card (easeOutCubic), in a join both old halves run on to the new cable and the new cable appears when they
// have reached it. Under Reduce Motion a chain is the plain fade (the old cables fade out, the new ones in).

#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include <vector>

class CableRetractAnimator {
public:
    using VisibleCable = graph_editor_types::VisibleCable;

    static constexpr double kFullMs = 220.0;
    static constexpr double kReducedMs = 80.0;
    static constexpr float kRetractHoldUntil = 0.6f; // the retract keeps full opacity until this progress
    static constexpr float kGrowFadeInUntil = 0.4f;  // the grow reaches full opacity at this progress
    static constexpr size_t kMaxGrowing = 16;        // a bigger step (a mass undo) shows its new cables at once

    struct Options {
        bool growAdded = false;    // also grow the cables in `after` that are not in `before` (undo and redo)
        bool reduceMotion = false; // alpha only: no geometry change
        bool morphChains = false;  // split an inserted-on cable in two and join the halves of a deleted one (see above)
        bool chainsOnly = false;   // arm nothing but those: every other cable keeps its (instant) behaviour
    };

    /** Arms a ghost for every cable in `before` whose id is not in `after`, and (Options::growAdded) a grow for every
     *  cable in `after` that is not in `before`; true when anything was armed. */
    bool arm(const std::vector<VisibleCable>& before, const std::vector<VisibleCable>& after, Options options);
    bool arm(const std::vector<VisibleCable>& before, const std::vector<VisibleCable>& after) {
        return arm(before, after, Options{});
    }
    /** `t` is the driver's 0..1 progress. */
    void applyTweenAt(float t) noexcept;
    void finish() noexcept;
    bool isLive() const noexcept {
        return !ghosts_.empty() || !growing_.empty() || !splitFrom_.empty() || !joinHalves_.empty();
    }
    /** Test seam: chains animate although the canvas is not showing (GraphEditor::morphCablesSince). */
    void setForceMotionForTest(bool force) noexcept { forceMotion_ = force; }
    bool forcesMotionForTest() const noexcept { return forceMotion_; }
    /** How long the armed motion runs: 220 ms, or 80 ms under Reduce Motion. */
    double durationMs() const noexcept { return reduced_ ? kReducedMs : kFullMs; }

    /** The ghosts as drawn now: each destination end drawn back toward its source. */
    std::vector<VisibleCable> ghosts() const;
    /** How opaque the ghosts are drawn now, 1 at the start of the retract and 0 at its end. */
    float opacity() const noexcept;

    /** True for a cable that is growing now: the canvas skips it in its normal pass. */
    bool isGrowing(const graph_editor_types::CableId& id) const noexcept;
    size_t numGrowing() const noexcept { return growing_.size(); }
    /** `live` (a growing cable as drawn now) with its destination end drawn partway out from its source. */
    VisibleCable grown(const VisibleCable& live) const noexcept;
    /** How opaque the growing wires are drawn now, 0 at the start and 1 once fully faded in. */
    float growOpacity() const noexcept;

    /** A split half: the canvas draws `morphed(live)` for it, its ends partway from the old cable to where they belong.
     */
    bool isMorphing(const graph_editor_types::CableId& id) const noexcept;
    VisibleCable morphed(const VisibleCable& live) const noexcept;
    /** A joined cable: not drawn until the two halves running onto it (joinHalves) have reached it. */
    bool isHidden(const graph_editor_types::CableId& id) const noexcept;
    std::vector<VisibleCable> joinHalves() const;
    size_t numSplitHalves() const noexcept { return splitFrom_.size(); }
    size_t numJoins() const noexcept { return joinHidden_.size(); }

    /** Where the ghosts and growing wires are drawn now, in canvas coordinates: what a frame repaints. */
    juce::Rectangle<int> paintArea() const;

private:
    struct Run {
        VisibleCable cable;              // a split: the new half; a join: the old half
        juce::Point<float> from1, from2; // where its ends start
        juce::Point<float> to1, to2;     // a join: where they end (the joined cable's ends)
    };
    std::vector<graph_editor_types::CableId> armChains(const std::vector<VisibleCable>& before,
                                                       const std::vector<VisibleCable>& after);
    float chainProgress() const noexcept;

    std::vector<Run> splitFrom_;
    std::vector<Run> joinHalves_;
    std::vector<graph_editor_types::CableId> joinHidden_;
    std::vector<VisibleCable> ghosts_;
    std::vector<VisibleCable> growing_; // as drawn when armed
    float progress_ = 1.0f;
    bool forceMotion_ = false;
    bool reduced_ = false;
};
