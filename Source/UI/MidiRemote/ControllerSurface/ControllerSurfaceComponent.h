#pragma once

#include "MidiRemote/RemoteEngine/RemoteEvent.h"
#include "MidiRemote/RemoteModel.h"
#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceCell.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <utility>
#include <vector>

// ControllerSurfaceComponent.h -- FRO131 (docs/control/midi-remote-ui.md#surface-centre): the MIDI
// Remote panel's centre region -- the selected controller's controls drawn on a 56 px grid
// (Control::layout {col,row}, snapped). The toolbar row above it is ControllerSurfaceToolbar
// (FRO134); this component owns only the grid itself. FRO270 adds multi-select (plain/shift/cmd
// click, a marquee drag over empty grid space) and group move/delete -- selection, marquee and
// group-drag are each their own concern unit (ControllerSurfaceSelection.cpp,
// ControllerSurfaceMarquee.cpp, ControllerSurfaceGroupDrag.cpp); this header only declares them.
// FRO331: pan/zoom (ControllerSurfaceView.cpp) makes an overflowing grid navigable, mirroring
// GraphEditor's canvas -- the cells live on a `Content` child carrying the view's AffineTransform,
// never on this component directly, so every position cell code already computes (grid math,
// marquee hit-testing, drag deltas) stays in that untransformed content-local space for free.
namespace synth::ui {

class ControllerSurfaceComponent : public juce::Component {
public:
    struct CellModel {
        synth::Control control;
        juce::String assignmentLabel; // "Filter . Cutoff" / "Play" / "-" / "(missing module)"
        bool isWarning = false;       // orphaned target -> warning-colour label
        bool isMapped = false;        // paints the MIDI badge
        float initialValue = 0.0f;    // 0..1, same domain as noteActivity(); 0 for unmapped/orphaned/action targets
    };

    /** One control's final grid position after a committed group (or single) move. */
    struct MovedCell {
        juce::String controlId;
        int col = 0;
        int row = 0;
    };

    ControllerSurfaceComponent();
    ~ControllerSurfaceComponent() override;

    /** Rebuilds the grid for a newly-selected profile (or clears it for an empty `profileId` --
     *  no controller selected). Structural only: call on profile switch, or when the shown
     *  profile's own control set/assignments change -- never on the activity tick. The current
     *  selection survives the rebuild, minus any id no longer present (FRO270). */
    void setControls(const juce::String& profileId, const std::vector<CellModel>& cells);

    const juce::String& getProfileId() const noexcept { return profileId_; }

    /** MidiRemotePanelComponent::refreshActivity()'s per-control fan-out. A no-op if `controlId`
     *  isn't on the currently shown grid -- the caller doesn't filter first. */
    void noteActivity(const juce::String& controlId, synth::midi::RemoteEventKind kind, float value);

    /** FRO134 (Detect): the control that should be pulsing (empty = none). Survives setControls():
     *  the pulse is re-applied to the rebuilt cell, since every detected control persists the
     *  profile and so rebuilds the grid. */
    void setDetectPulseControlId(const juce::String& controlId);
    /** Lights an existing control's cell briefly (Detect: "an existing control's cell lights instead"). */
    void flashControl(const juce::String& controlId);
    /** Advances pulse/flash repaints; call from the panel's gated activity tick. */
    void tickDetectHighlights();
    bool isControlPulsingForTest(const juce::String& controlId) const;

    /** Replaces the whole selection with exactly `controlId` (or clears it, if empty) -- the
     *  legacy single-select setter every non-multi-select caller (Detect, Assign, a profile
     *  switch) still uses. */
    void setSelectedControlId(const juce::String& controlId);
    /** FRO270: replaces the whole selection with `ids` (silently dropping any id not currently on
     *  the grid) -- the panel's own post-move/-delete restore call. */
    void setSelectedControlIds(const std::vector<juce::String>& ids);
    /** The single selected control, or empty if none or more than one is selected. */
    juce::String getSelectedControlId() const noexcept {
        return selectedIds_.size() == 1 ? selectedIds_.front() : juce::String();
    }
    /** FRO270: every currently selected control id, in click/marquee order. */
    const std::vector<juce::String>& getSelectedControlIds() const noexcept { return selectedIds_; }

    /** FRO262 test seam: the widget's current displayed value (0..1) for `controlId`, or -1.0f if
     *  no cell with that id is currently shown. */
    float getCellValueForTest(const juce::String& controlId) const;
    const ControllerSurfaceCell* findCellForTest(const juce::String& controlId) const;
    /** Non-const overload for a test driving a real mouseDown/mouseDrag/mouseUp on the cell found. */
    ControllerSurfaceCell* findCellForTest(const juce::String& controlId);
    /** FRO270 test seam: whether a marquee drag is currently being painted. */
    bool isMarqueeActiveForTest() const noexcept { return marqueeActive_; }

    /** FRO270: fired on every selection change (click, shift/cmd-click, marquee, Esc, a click on
     *  empty grid space) with the new full id set -- the panel's one hook for keeping its own
     *  mirrored selection and the inspector in step. */
    std::function<void(const std::vector<juce::String>& ids)> onSelectionChanged;
    /** A drag landed on new grid cell(s), already clamped to the grid's top-left bound as a block.
     *  Never fired at all if the (clamped) move would land any selected control on a cell an
     *  UNselected control already occupies -- the whole gesture is refused and every cell springs
     *  back to its pre-drag position. One entry per moved control, whether the drag started from a
     *  lone selection or a multi-selection -- the caller writes every entry back onto the profile
     *  and commits ONE updateProfile() call, then calls setControls() again to reflect it (same
     *  deferral contract the single-control version always had). */
    std::function<void(const std::vector<MovedCell>& moves)> onControlsMoved;
    /** Delete/Backspace while 1+ cells are selected -- this component itself grabs keyboard focus
     *  on selection and owns keyPressed, rather than each cell separately competing for focus. */
    std::function<void(const std::vector<juce::String>& controlIds)> onDeleteControlsRequested;

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    /** FRO331: plain wheel/trackpad-scroll pans (so an overflowing grid is navigable without a
     *  drag); Cmd+wheel zooms, anchored at the cursor. */
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    /** FRO331: trackpad pinch zooms, anchored at the pinch point. */
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

    /** FRO331: the content-space rectangle currently visible through this component's bounds, at
     *  the current pan/zoom -- the inverse of the view transform applied to getLocalBounds(). */
    juce::Rectangle<float> getVisibleContentRect() const noexcept;
    float getZoomLevelForTest() const noexcept { return zoomLevel_; }
    juce::Point<float> getPanOffsetForTest() const noexcept { return panOffset_; }

    static constexpr int kCellSize = ControllerSurfaceCell::kCellSize;
    static constexpr int kCellMargin = ControllerSurfaceCell::kCellMargin;
    /** FRO331: zoom clamp, same shape as GraphEditor's own -- a control-surface grid never needs
     *  GraphEditor's full [0.1, 2.0] range (a cell below ~0.4x is illegible; above 2x is rarely
     *  useful for a fixed 56 px grid). */
    static constexpr float kMinZoom = 0.4f;
    static constexpr float kMaxZoom = 2.0f;

private:
    // ---- FRO270 selection (ControllerSurfaceSelection.cpp) ----
    void setSelectionInternal(std::vector<juce::String> ids, bool notify = true);
    void handleCellSelected(const juce::String& controlId, const juce::ModifierKeys& mods);

    // ---- FRO270 marquee / FRO331 pan (ControllerSurfaceMarquee.cpp) ----
    std::vector<juce::String> collectMarqueeHits() const;

    // ---- FRO270 group drag (ControllerSurfaceGroupDrag.cpp) ----
    void handleCellDragged(const juce::String& controlId, int deltaCol, int deltaRow);
    void handleCellDragEnded(const juce::String& controlId);
    ControllerSurfaceCell* findMutableCell(const juce::String& controlId);
    std::vector<juce::String> dragGroupFor(const juce::String& controlId) const;
    static void moveCellToLayout(ControllerSurfaceCell& cell, int col, int row);

    // ---- FRO331 pan/zoom (ControllerSurfaceView.cpp) ----
    // The cells' real parent: carries the view's AffineTransform (scale then translate) so every
    // child stays in one untransformed content-local coordinate space regardless of pan/zoom.
    class Content : public juce::Component {
    public:
        explicit Content(ControllerSurfaceComponent& owner);
        void paint(juce::Graphics& g) override;
        void paintOverChildren(juce::Graphics& g) override;

    private:
        ControllerSurfaceComponent& owner_;
    };

    void updateTransform();
    void applyZoom(float newZoomLevel, juce::Point<float> screenAnchor);
    void restoreOrResetView(const juce::String& newProfileId);

    Content content_;
    juce::String profileId_;
    std::vector<juce::String> selectedIds_;
    juce::String pulseControlId_;
    double pulseSinceMs_ = 0.0;
    juce::OwnedArray<ControllerSurfaceCell> cells_;

    // Marquee drag state (ControllerSurfaceMarquee.cpp), in content-local (canvas) coordinates so
    // it survives pan/zoom: a Shift-press on empty grid space that turns into a drag once the
    // pointer actually moves, mirroring ControllerSurfaceCell's own click-vs-drag debounce. A plain
    // (non-Shift) press on empty space is pan or a deselect-click instead -- see pendingEmptyClick_.
    juce::Point<int> marqueeAnchor_;
    juce::Rectangle<int> marqueeRect_;
    bool marqueeActive_ = false;
    bool marqueeArmed_ = false; // Shift was down at mouseDown -- this gesture can only ever be a marquee
    bool marqueeAdditive_ = false;

    // Pan state (ControllerSurfaceMarquee.cpp): a plain press on empty space is pan-or-click,
    // decided at mouseUp by whether any drag actually moved the view (pendingEmptyClick_ latches
    // false on the first pan step) -- mirrors GraphEditor::pendingEmptyCanvasClick.
    bool pendingEmptyClick_ = false;
    juce::Point<float> lastPanMouse_;

    // View transform (ControllerSurfaceView.cpp): screen = content * zoomLevel_ + panOffset_.
    juce::Point<float> panOffset_;
    float zoomLevel_ = 1.0f;
    // FRO331: trivial in-memory (session-only, never written to disk) view-per-controller memory
    // -- switching back to a profile already visited this session restores the pan/zoom it was
    // left at, keyed by profileId. Deliberately not part of ControllerProfile/persisted state: see
    // this member's use in restoreOrResetView()'s own comment.
    std::map<juce::String, std::pair<juce::Point<float>, float>> savedViewByProfileId_;

    // Live group-drag state (ControllerSurfaceGroupDrag.cpp): the last delta reported by whichever
    // cell has mouse capture for the gesture, read back by handleCellDragEnded() (a cell's own
    // onDragEnded carries no arguments).
    juce::String liveDragOriginId_; // the drag-owning cell -- a stale delta from a previous
                                    // gesture must never be replayed for a new one
    int liveDragDeltaCol_ = 0;
    int liveDragDeltaRow_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllerSurfaceComponent)
};

} // namespace synth::ui
