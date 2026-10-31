#pragma once

#include "CurveEditorGeometry.h"
#include "CurveModel.h"
#include "UI/Layout/KeyGlide/KeyHold.h"
#include "UI/Layout/KeyGlide/KeyedPointTween.h"
#include "UI/Layout/PointEditing/PointBoxSelect.h"
#include "UI/Layout/PointEditing/PointClipboard.h"
#include "UI/Layout/PointEditing/PointSelectionModel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace synth::ui {

/** Reusable breakpoint-curve editor: draws a curve through a `CurveModel`'s nodes, each segment
 *  shaped by `EnvelopeGenerator::shape` (via the model), and lets the user drag nodes and bend
 *  handles (both modes) or add/remove points by double-click (`CurveMode::Free` only).
 *
 *  Used by the envelope card (`CurveMode::Fixed`, a fixed origin/attack/hold/decay/release
 *  topology) and by the LFO custom-wave editor and the modulation curve box (`CurveMode::Free`,
 *  add/remove/reorder points) — see docs/layout/visualizers.md.
 *
 *  Mouse handlers are thin wrappers over public primitives (`dragNodeTo`, `dragBendBy`,
 *  `addPointAt`, `removeNode`), mirroring `EQCurveComponent`, so interaction is unit-testable
 *  without synthesising `juce::MouseEvent`s (though the real mouse path is exercised too — see
 *  Tests/UI/ModuleViews/CurveEditor/CurveEditorInteractionTests.cpp).
 *
 *  Keyboard (the shared point-editor key map, docs/control/point-editing.md; a Tab stop with the accent focus ring):
 * Left/Right select the previous/next movable point (clamped at the ends), Shift+Left/Right extend or shrink the
 * selection toward that side, Alt+Left/Right move every selected point in time and Alt+Up/Down in level (one grid cell
 * while snap-to-grid is on, else a small step; Shift: a fifth of the step, unsnapped), plain Up/Down move the level
 * like Alt+Up/Down, Ctrl+T inserts a point halfway between the cursor point and the next, Delete/Backspace remove them
 * in Free mode. Cmd+A selects every movable point, Cmd+C copies the selection, and in Free mode Cmd+V pastes it after
 * the cursor point and Cmd+D duplicates it. Every press is one gesture bracket, so one undo step, and runs the same
 * callbacks a drag does. A screen reader reads the cursor point, e.g. "Point 3 of 5, time 0.25, level 0.80", led by "3
 * points selected" when several are.
 *
 *  Mouse selection: Shift- or Cmd-click toggles a point; dragging a selected point moves the whole selection.
 *
 *  No `juce::Timer` — repaints only when state actually changes; the playhead is a plain setter
 *  (`setPlayhead`), not an animation. A later change adds a gated animation driving it during
 *  playback (see the "No unconditional per-tick repaint" rule in Source/UI/CLAUDE.md).
 */
class CurveEditorComponent
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    CurveEditorComponent();
    ~CurveEditorComponent() override = default;

    /** With `glideMoved` (an undo or redo), nodes of a same-shaped curve (same count and mode) are drawn gliding from
     *  where they were; a different shape lands at once. */
    void setModel(CurveModel model, bool glideMoved = false);
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
     *  `1/yDivisions` of the level range). A positive `xStep` replaces the even x cells with lines at
     *  `xOrigin + k * xStep` in model units (a log axis whose octaves are not a whole number of cells). */
    struct CurveGrid {
        int xDivisions = 4;
        int yDivisions = 2;
        double xStep = 0.0;
        double xOrigin = 0.0;
    };
    /** `nullopt` (the default) draws today's nice-tick time grid instead -- an ordinary envelope
     *  card never calls this. */
    void setGrid(std::optional<CurveGrid> grid);
    /** When enabled, a point add/drag snaps x to the nearest `1/xDivisions` of the model's visible
     *  range and y to the nearest `1/yDivisions`, unless the gesture's own bypass flag (Shift) is
     *  set. Never affects a bend drag. Off by default. */
    void setSnapToGrid(bool enabled);

    /** One labelled guide line: `position` is in model units along x, or a level in [0, 1] along y. */
    struct AxisGuide {
        double position = 0.0;
        juce::String label;
        bool emphasised = false; // drawn brighter (a zero line)
    };
    struct AxisGuides {
        std::vector<AxisGuide> x;
        std::vector<AxisGuide> y;
    };
    /** Draws exactly these guides, with their labels, in place of the even grid or the time grid (for an axis that
     *  is not linear, such as frequency). `nullopt` (the default) restores the grid. */
    void setAxisGuides(std::optional<AxisGuides> guides);

    /** Baseline level `paintCurve` fills its area down (or up) to -- 0 by default. The LFO card
     *  sets 0.5 for a bipolar wave (so the fill reads from the zero line, not the bottom) and 0
     *  for unipolar. */
    void setFillBaselineLevel(float level);

    /** Paints behind the grid and the curve (a live spectrum under a response). Unset (the default) paints nothing,
     *  so the editor looks as it did. The caller repaints the editor when what it draws changes. */
    using BackdropPainter = std::function<void(juce::Graphics&, const CurveEditorGeometry&)>;
    void setBackdropPainter(BackdropPainter painter);

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
    /** Set: a Ctrl+T press that cannot insert is used up and reports why here. Unset: it is left to the app. */
    std::function<void(const juce::String& why)> onInsertRefused;

    /** Right-click hook (Free mode's context menu, e.g. the LFO card's Shapes/Tools/Grid menu).
     *  `hitTest(e.position)` has already run; the result is handed in so the caller doesn't redo
     *  it. Unset (the default): a right-click starts no drag and is otherwise a no-op. */
    std::function<void(const juce::MouseEvent&, CurveHitResult)> onContextMenu;

    /** Free mode: a double-click on empty space (not a node or a bend handle) asks this first; returning true
     *  means the owner handled it, and no point is added. Unset (the default): the double-click adds a point. */
    std::function<bool(const juce::MouseEvent&)> onEmptyDoubleClick;

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
     *  node's index, or -1 outside Free mode or when the curve is at CurveModel::kMaxNodes. */
    int addPointAt(juce::Point<float> point, bool bypassSnap = false);

    /** Free mode only. On: a press on the drawn line adds a point (and a drag moves it, as one gesture), and a
     *  double-click no longer adds or removes points. Off by default. */
    void setLineClickEditing(bool enabled);

    /** Free mode only. Returns false if refused (pinned node, or would drop below 2 nodes) or
     *  outside Free mode. */
    bool removeNode(int index);

    /** Moves every selected point by `dx` model units in time and `dy` in level as one gesture, through
     *  the same path as a drag (snapping included unless `bypassSnap`). With several selected the move is
     *  rigid: the delta is clamped so no point leaves the range or crosses an unselected neighbour, and a
     *  pinned axis stays put. False when no point is selected or the move changes nothing. */
    bool nudgeSelectedNode(double dx, float dy, bool bypassSnap = false);

    /** Selects the previous (-1) or next (+1) movable point, collapsing the selection to it; the first
     *  one when none is selected. Returns the cursor index, or -1 when no point can be selected. */
    int selectNodeRelative(int delta);

    /** Moves the cursor one movable point toward `delta`'s side and selects every movable point between
     *  it and the anchor (the point the selection started from). Returns the cursor index. */
    int extendSelectionRelative(int delta);

    /** Adds `index` to the selection and makes it the cursor, or removes it when already selected. False
     *  for a pinned or out-of-range node. */
    bool toggleSelected(int index);

    /** Selects every movable point. False when there is none. */
    bool selectAllMovable();

    /** Free mode only: removes every selected point that can go (one gesture), then puts the cursor on the
     *  next remaining movable point. False when nothing was removed. */
    bool removeSelectedNodes();

    /** Copies the selected points (spacing relative to the first, level, bend) to a process-wide clipboard
     *  shared by all curve editors. False when nothing is selected. */
    bool copySelection();

    /** Free mode only: pastes the clipboard so the group starts just after the cursor point, clamped to the
     *  curve's range and the room left; the pasted points become the selection. One gesture. False when the
     *  clipboard is empty, the curve is full, or the mode is not Free. */
    bool pasteAtCursor();

    /** Free mode only: pastes a copy of the selection just after its last point without touching the
     *  clipboard. False when nothing is selected. */
    bool duplicateSelection();

    /** Free mode only: inserts a point halfway (in time and level) between the cursor point and the next one, or
     *  halfway to the end of the range when the cursor is the last point; the new point becomes the selection.
     *  One gesture. False when there is no cursor, the curve is full, or there is no room between the points. */
    bool insertAfterCursor();

    /** Why insertAfterCursor() would do nothing, as a sentence for the user; empty when it would insert. */
    juce::String insertRefusal() const;

    /** The cursor point: where Left/Right continue from and what a screen reader reads. -1 when none. */
    int getSelectedIndex() const noexcept { return cursorIndex(); }
    /** Every selected point, ascending. */
    const std::vector<int>& getSelectedIndices() const noexcept { return selection_.selected(); }

    /** The selected point as spoken, e.g. "Point 3 of 5, time 0.25, level 0.80"; a summary when none is. */
    juce::String getAccessibilityValueText() const;

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

    // ---------- keyboard and screen reader (CurveEditorKeyboard.cpp) ----------
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    bool keyStateChanged(bool isKeyDown) override;

    /** Tests: the held-arrow drag (inject host.nowMs/isArrowDown/modifiers, then tick() per frame) and the eased
     *  drawn points. */
    KeyHold& getKeyHoldForTest() noexcept { return keyHold_; }
    KeyedPointTween& getPointTweenForTest() noexcept { return pointTween_; }
    juce::Path getDrawnCurvePathForTest() const;

private:
    CurveEditorGeometry currentGeometry() const;
    void beginGesture();
    void endGesture();
    /** Writes the already-snapped target to node `index` honouring its movable flags, keeps the
     *  selection on the node through a Free-mode reorder, and fires the change callbacks. Returns the
     *  node's index afterwards. */
    int applyNodeTarget(int index, juce::Point<double> target);
    /** The model edit alone (movable flags honoured), returning the node's index afterwards. */
    static int moveNodeInModel(CurveModel& model, int index, juce::Point<double> target);
    /** Collapses the selection to `index` (-1 clears it). */
    void setSelectedIndex(int index);
    void setSelection(std::vector<int> indices, int cursor, int anchor);
    void selectionChanged();
    /** The movable nodes whose drawn centre lies inside `box`, then the box selection applied
     * (CurveEditorBoxSelect.cpp). */
    std::vector<int> movableNodesInBox(juce::Rectangle<float> box) const;
    void dragBoxTo(juce::Point<float> position);
    void endBox();
    bool isSelected(int index) const;
    int cursorIndex() const noexcept;
    /** Every node a selection key may land on, ascending. */
    std::vector<int> movableIndices() const;
    /** Re-points the selection after node `from` was re-sorted to `to`. */
    void remapSelectionForMove(int from, int to);
    /** The model with the selection moved by (dx, dy), clamped; `changed` lists the nodes that differ. */
    CurveModel modelWithSelectionMoved(double dx, double dy, std::vector<int>& changed) const;
    /** Installs a model from `modelWithSelectionMoved` and fires the change callbacks. */
    void commitSelectionMove(CurveModel moved, const std::vector<int>& changed);
    /** Drags the whole selection so node `index` follows `point`. */
    void dragSelectionTo(int index, juce::Point<float> point, bool bypassSnap);
    /** The move behind nudgeSelectedNode without its gesture bracket: `holdGesture` opens the hold's bracket at the
     *  first real move instead. */
    bool moveSelectionBy(double dx, float dy, bool bypassSnap, bool holdGesture, bool ease);

    // ---- Keyboard drag (CurveEditorKeyGlide.cpp) ----
    void pressNudge(int keyCode, double dx, double dy, bool fine);
    bool beginKeyDrag(int keyCode, double dx, double dy, bool fine);
    double coveredAlong(KeyAxis axis, double unit) const;
    std::vector<juce::Point<float>> selectionPositions() const;
    juce::Point<float> segmentTweenOffset(int seg, float progress) const;
    void buildCurvePaths(const CurveEditorGeometry& geometry, juce::Path& curvePath, juce::Path& fillPath) const;
    std::vector<juce::Point<float>> nodePositions() const;
    void glideNodesFrom(const std::vector<juce::Point<float>>& before);
    void applyKeyDrag(bool tapStep);
    void endKeyDrag();
    double keyDragSpeedFactor() const;

    /** What a copied curve point carries besides its time and level. */
    struct ClipBend {
        float bend = 0.0f;
        bool hasBend = false;
    };
    using Clip = PointClip<ClipBend>;
    static Clip& clipboard();
    Clip selectionAsClip() const;
    bool insertClip(const Clip& clip, double startX);
    /** Posts a value-changed event when the spoken text differs from the last one handed out. */
    void refreshAccessibilityValue();
    /** One keyboard step along each axis, in model units: a grid cell while snapping, else a fixed
     *  fraction of the visible range / level range. */
    juce::Point<double> keyboardStep() const;
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
    void paintGridLines(juce::Graphics& g, const CurveEditorGeometry& geometry, const ThemeColours& colours) const;
    /** The x of every vertical grid line, in model units. */
    std::vector<double> gridXPositions() const;
    void paintCurve(juce::Graphics& g, const CurveEditorGeometry& geometry, const ThemeColours& colours) const;
    void paintHandles(juce::Graphics& g, const CurveEditorGeometry& geometry, const ThemeColours& colours) const;
    void paintPlayheadMarker(juce::Graphics& g, const CurveEditorGeometry& geometry, const ThemeColours& colours) const;

    CurveModel model_;
    CurveGeometryConfig geometryConfig_;
    std::function<juce::String(double)> timeLabelFormatter_ = &CurveEditorGeometry::defaultTimeLabel;

    std::optional<CurveGrid> grid_;
    std::optional<AxisGuides> guides_;
    BackdropPainter backdrop_;
    bool snapToGrid_ = false;
    float fillBaselineLevel_ = 0.0f;

    std::optional<CurvePlayhead> playhead_;

    enum class DragKind { None, Node, Bend };
    int insertPointAt(juce::Point<float> point, bool bypassSnap);
    bool isOnCurveLine(juce::Point<float> point) const;
    static constexpr float kLineClickSlopPx = 6.0f;
    bool lineClickEditing_ = false;

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
    /** The curve's answers where the shared key map leaves room (docs/control/point-editing.md). */
    static constexpr PointSelectionRules kSelectionRules{PointSelectionRules::FirstStep::firstPoint, false,
                                                         PointSelectionRules::Extend::rangeFromAnchor,
                                                         PointSelectionRules::SelectAllCursor::keepSelectedElseLast};
    PointSelectionModel<int> selection_;
    PointBoxSelect<int> boxSelect_{*this};
    /** A press on an already-selected point settles on release when no drag followed: -1 none. */
    int pendingToggleIndex_ = -1;
    int pendingCollapseIndex_ = -1;
    juce::String announcedValueText_;

    KeyedPointTween pointTween_{*this};
    KeyHold keyHold_{*this};
    struct KeyDrag {
        juce::Point<double> start; // the cursor point when the hold began
        juce::Point<double> unit;  // one step along each axis: the tap, and the snapped quantum
        bool snapped = false;
        bool gestureOpen = false;
    } keyDrag_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveEditorComponent)
};

} // namespace synth::ui
