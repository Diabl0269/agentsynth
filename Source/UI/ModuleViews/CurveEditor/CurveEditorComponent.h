#pragma once

#include "CurveEditorGeometry.h"
#include "CurveModel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** Reusable breakpoint-curve editor: draws a curve through a `CurveModel`'s nodes, each segment
 *  shaped by `EnvelopeGenerator::shape` (via the model), and lets the user drag nodes and bend
 *  handles (both modes) or add/remove points by double-click (`CurveMode::Free` only).
 *
 *  Used by the envelope card (`CurveMode::Fixed`, a fixed origin/attack/hold/decay/release
 *  topology); `CurveMode::Free` (add/remove/reorder points) is the second supported topology
 *  and has no caller yet — see docs/layout/visualizers.md.
 *
 *  Mouse handlers are thin wrappers over public primitives (`dragNodeTo`, `dragBendBy`,
 *  `addPointAt`, `removeNode`), mirroring `EQCurveComponent`, so interaction is unit-testable
 *  without synthesising `juce::MouseEvent`s (though the real mouse path is exercised too — see
 *  Tests/UI/ModuleViews/CurveEditor/CurveEditorInteractionTests.cpp).
 *
 *  No `juce::Timer` — repaints only when state actually changes; the playhead is a plain setter
 *  (`setPlayhead`), not an animation. A later change adds a gated animation driving it during
 *  playback (see the "No unconditional per-tick repaint" rule in Source/UI/CLAUDE.md).
 */
class CurveEditorComponent : public juce::Component {
public:
    CurveEditorComponent();
    ~CurveEditorComponent() override = default;

    void setModel(CurveModel model);
    CurveModel& getModel() noexcept { return model_; }
    const CurveModel& getModel() const noexcept { return model_; }

    /** Geometry tuning — see `CurveGeometryConfig`. Either call repaints. */
    void setMinVisibleRange(double minRange);
    void setVisibleRangeOverride(std::optional<double> range);
    /** Pixel width a zero-duration segment occupies — see `CurveGeometryConfig::zeroSegmentPx`. */
    void setZeroSegmentPx(float px);

    /** Overrides the default "0"/"250ms"/"1s" tick-label formatter (e.g. for an LFO's phase
     *  units, which aren't seconds). */
    void setTimeLabelFormatter(std::function<juce::String(double)> formatter);

    /** `xDivisions`/`yDivisions`: how many even cells `paintGrid` draws when a grid is set (a
     *  vertical line at every `1/xDivisions` of the visible range, horizontal at every
     *  `1/yDivisions` of the level range). */
    struct CurveGrid {
        int xDivisions = 4;
        int yDivisions = 2;
    };
    /** `nullopt` (the default) draws today's nice-tick time grid instead -- an ordinary envelope
     *  card never calls this. */
    void setGrid(std::optional<CurveGrid> grid);
    /** When enabled, a point add/drag snaps x to the nearest `1/xDivisions` of the model's visible
     *  range and y to the nearest `1/yDivisions`, unless the gesture's own bypass flag (Shift) is
     *  set. Never affects a bend drag. Off by default. */
    void setSnapToGrid(bool enabled);

    /** Baseline level `paintCurve` fills its area down (or up) to -- 0 by default. The LFO card
     *  sets 0.5 for a bipolar wave (so the fill reads from the zero line, not the bottom) and 0
     *  for unipolar. */
    void setFillBaselineLevel(float level);

    /** `nullopt` hides the playhead marker. Repaints only if the value actually changed. */
    void setPlayhead(std::optional<CurvePlayhead> playhead);
    std::optional<CurvePlayhead> getPlayhead() const noexcept { return playhead_; }

    /** Bracket every parameter-changing gesture so the host can capture one undo step per drag
     *  (or per atomic add/remove/reset). Both optional; unset, gestures still work, just without
     *  undo entries.
     */
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;
    std::function<void(int)> onNodeChanged;
    std::function<void(int)> onBendChanged;
    std::function<void()> onPointsChanged;

    /** Right-click hook (Free mode's context menu, e.g. the LFO card's Shapes/Tools/Grid menu).
     *  `hitTest(e.position)` has already run; the result is handed in so the caller doesn't redo
     *  it. Unset (the default): a right-click starts no drag and is otherwise a no-op. */
    std::function<void(const juce::MouseEvent&, CurveHitResult)> onContextMenu;

    // ---------- interaction primitives (public so tests can drive them directly) ----------

    CurveHitResult hitTest(juce::Point<float> point) const;

    /** Drags node `index` to `point`, honouring its `xMovable`/`yMovable` constraints. When
     *  snap-to-grid is enabled, the target x/y is rounded to the grid first unless `bypassSnap`
     *  (Shift) is set. Returns the node's index afterwards (Free mode may have reordered it past
     *  a neighbour). */
    int dragNodeTo(int index, juce::Point<float> point, bool bypassSnap = false);

    /** Applies an incremental vertical pixel delta to `segment`'s bend (full [-1, 1] range over
     *  `kBendSensitivityPx`), sign chosen so dragging toward the side the curve already bulges
     *  increases the bulge, for both rising and falling segments. No-op on a non-bendable or
     *  flat (start == end level) segment. */
    void dragBendBy(int segment, float deltaPixelsY);

    /** Resets `segment`'s bend to 0 (double-click on its handle). */
    void resetBend(int segment);

    /** Free mode only. Snaps like `dragNodeTo` (same `bypassSnap` meaning). Returns the new
     *  node's index, or -1 outside Free mode. */
    int addPointAt(juce::Point<float> point, bool bypassSnap = false);

    /** Free mode only. Returns false if refused (pinned node, or would drop below 2 nodes) or
     *  outside Free mode. */
    bool removeNode(int index);

    static constexpr float kBendSensitivityPx = 150.0f;

    // ---------- mouse ----------
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // ---------- Component ----------
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    CurveEditorGeometry currentGeometry() const;
    void beginGesture();
    void endGesture();
    void updateHover(juce::Point<float> point);
    /** Rounds `point`, already converted to model units via `geometry`, to the grid -- a no-op
     *  (returns `point` unchanged) unless `snapToGrid_` is set and `bypassSnap` is false. */
    juce::Point<double> snapModelPoint(const CurveEditorGeometry& geometry, juce::Point<double> point,
                                       bool bypassSnap) const;

    // CurveEditorPaint.cpp
    struct ThemeColours {
        juce::Colour background;
        juce::Colour grid;
        juce::Colour mutedText;
        juce::Colour accent;
    };
    ThemeColours resolveThemeColours() const;
    void paintGrid(juce::Graphics& g, const CurveEditorGeometry& geometry, const ThemeColours& colours) const;
    void paintCurve(juce::Graphics& g, const CurveEditorGeometry& geometry, const ThemeColours& colours) const;
    void paintHandles(juce::Graphics& g, const CurveEditorGeometry& geometry, const ThemeColours& colours) const;
    void paintPlayheadMarker(juce::Graphics& g, const CurveEditorGeometry& geometry, const ThemeColours& colours) const;

    CurveModel model_;
    CurveGeometryConfig geometryConfig_;
    std::function<juce::String(double)> timeLabelFormatter_ = &CurveEditorGeometry::defaultTimeLabel;

    std::optional<CurveGrid> grid_;
    bool snapToGrid_ = false;
    float fillBaselineLevel_ = 0.0f;

    std::optional<CurvePlayhead> playhead_;

    enum class DragKind { None, Node, Bend };
    DragKind dragKind_ = DragKind::None;
    int dragIndex_ = -1;
    juce::Point<float> lastDragPos_;
    bool gestureActive_ = false;

    /** Frozen visible-range snapshot for the duration of a node drag started via the real mouse
     *  path (set in `mouseDown` on a Node hit, cleared in `mouseUp`). Without this, `currentGeometry()`
     *  would recompute the visible range off the model's just-edited total duration on every
     *  `mouseDrag`, rescaling the mapping under the cursor mid-gesture (a runaway feedback loop
     *  when dragging the last node near the view's edge). Bend drags never set this -- they don't
     *  move x, so there's nothing to freeze against. Not used by `dragNodeTo` called directly
     *  (outside a real mouse gesture), which keeps its original always-auto-fit behaviour. */
    std::optional<double> dragFrozenRange_;

    CurveHitKind hoveredKind_ = CurveHitKind::None;
    int hoveredIndex_ = -1;
    int selectedIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveEditorComponent)
};

} // namespace synth::ui
