#pragma once

#include "Modules/Envelope/EnvelopeGenerator.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <limits>
#include <vector>

namespace synth::ui {

/** A single breakpoint. `x` is in model units (seconds for the envelope card; arbitrary phase
 *  units for a future LFO custom-waveform editor); `y` is a level in [0, 1].
 *
 *  `minSegment`/`maxSegment` bound the duration of the INCOMING segment (the one ending at this
 *  node, i.e. segment `index - 1`) and are consulted only when this node's `x` is edited in
 *  `CurveModel::Fixed` mode; they are meaningless for node 0, which has no incoming segment.
 */
struct CurveNode {
    double x = 0.0;
    float y = 0.0f;
    bool xMovable = true;
    bool yMovable = true;
    double minSegment = 0.0;
    double maxSegment = std::numeric_limits<double>::infinity();
    float minY = 0.0f;
    float maxY = 1.0f;
};

/** Editing topology for a CurveModel. */
enum class CurveMode {
    /** Fixed node count/order (e.g. an envelope's origin/attack/hold/decay/release nodes): an
     *  x-edit ripples — see `CurveModel::setNodeX`. No add/remove. */
    Fixed,
    /** Free topology (e.g. an LFO custom waveform): points may be added, removed, and reordered
     *  by dragging one across a neighbour. */
    Free
};

/** Result of an x-edit: the index the moved node ends up at (Free mode may reorder it past a
 *  neighbour; Fixed mode never changes a node's index). */
struct MoveResult {
    int newIndex = -1;
};

/** Pure data + edit model for a breakpoint curve: nodes connected by segments, each segment
 *  shaped by a bend amount in [-1, 1]. No `juce::Component` dependency, so it is constructible
 *  and unit-testable headless.
 *
 *  Bend shaping defaults to `synth::EnvelopeGenerator::shape` — the exact function the DSP
 *  evaluates — so a curve drawn from this model is what actually plays, never a re-derived
 *  approximation. A caller may substitute a different shape function (e.g. a future LFO-specific
 *  shape) via the constructor.
 */
class CurveModel {
public:
    using ShapeFn = std::function<float(float progress, float bend)>;

    explicit CurveModel(CurveMode mode = CurveMode::Fixed, ShapeFn shapeFn = defaultShape());

    CurveMode getMode() const noexcept { return mode_; }
    void setMode(CurveMode mode) noexcept { mode_ = mode; }

    /** Replaces the node set wholesale (setup only — not an edit primitive). Segment bends
     *  default to 0 and bendable to true; use `setBend`/`setBendable` afterwards to configure. */
    void setNodes(std::vector<CurveNode> nodes);

    int getNumNodes() const noexcept { return (int)entries_.size(); }
    int getNumSegments() const noexcept { return juce::jmax(0, getNumNodes() - 1); }
    const CurveNode& getNode(int index) const;

    double getMinX() const;
    double getMaxX() const;

    float getBend(int segment) const;
    void setBend(int segment, float bend);
    bool isBendable(int segment) const;
    void setBendable(int segment, bool bendable);

    double segmentDuration(int segment) const;
    /** Value along `segment` at `progress` in [0, 1], via this model's shape function. */
    float valueAt(int segment, float progress) const;

    /** x-edit. Fixed mode: moving node `index` sets the duration of segment `index - 1` to
     *  clamp(newX - x[index-1], minSegment, maxSegment) of node `index`'s own constraints, and
     *  every later node shifts by the same delta (their own segment durations are preserved).
     *  A no-op (index 0, out of range, or `!xMovable`) returns the node's unchanged index.
     *
     *  Free mode: moves only `index`, clamped to [getMinX(), getMaxX()] (evaluated BEFORE the
     *  move), then re-sorts by x — a move across a neighbour changes the node's index, reported
     *  via the result. A no-op returns the node's unchanged index.
     */
    MoveResult setNodeX(int index, double newX);

    /** y-edit, clamped to [minY, maxY]; ignored if `!yMovable`. */
    void setNodeY(int index, float newY);

    /** Free mode only: refused (returns false) outside Free mode. */
    bool canRemovePoint(int index) const;

    /** Free mode only: inserts at the sorted position for `x` (new segment bend/bendable are
     *  copied from the segment being split, so both halves start identical). Returns the new
     *  node's index. */
    int addPoint(double x, float y);

    /** Free mode only: refuses a pinned node (`!xMovable`) or dropping below 2 nodes. */
    bool removePoint(int index);

    static ShapeFn defaultShape();

private:
    /** Internal bookkeeping: a node plus the bend/bendable of the segment starting AT this node
     *  (i.e. this node -> the next one). Meaningless for the last entry. `internalId` gives a
     *  stable identity across a Free-mode re-sort, so `setNodeX` can report the moved node's new
     *  index even when several nodes share a pixel/x position.
     */
    struct NodeEntry {
        CurveNode node;
        float outgoingBend = 0.0f;
        bool outgoingBendable = true;
        int internalId = 0;
    };

    void checkSegmentIndex(int segment) const;
    MoveResult setNodeXFixed(int index, double newX);
    MoveResult setNodeXFree(int index, double newX);

    CurveMode mode_;
    ShapeFn shapeFn_;
    std::vector<NodeEntry> entries_;
    int nextId_ = 0;
};

} // namespace synth::ui
