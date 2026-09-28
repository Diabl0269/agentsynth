#pragma once

#include "ControllerLaneEdits.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace synth::ui {

class PianoRollComponent;

// PianoRollControllerLanes — the velocity / MIDI CC lane strip docked under the piano roll's note
// canvas. A child of PianoRollComponent spanning its full width, so strip-local x IS roll x and every
// beat goes through the roll's own beatToX/xToBeat. The left gutter [0, roll.leftGutterWidth()) holds
// the lane selector; the rest is the lane.
//
// Shows ONE lane at a time: Velocity (one bar per note) or a CC number's per-clip lane. Every gesture
// previews locally and commits exactly once on mouse-up through AppUndoManager::recordTimelineChange
// (TimelineDoc::setNoteVelocities / setControllerLanePoints), so each gesture is one undo step.
// Never takes keyboard focus, so the roll's own keys (J, Q, ...) keep working.
//
// Gesture table and data model: docs/timeline/piano-roll-lanes.md. Message thread only.
class PianoRollControllerLanes : public juce::Component {
public:
    static constexpr int kStripHeight = 96;
    static constexpr int kVelocityLane = lanes::kVelocityLane;
    static constexpr int kBarHitPx = 4;    // horizontal tolerance for hitting a velocity bar
    static constexpr int kHandleHitPx = 5; // tolerance for hitting a CC point handle

    // Context-menu actions (performContextAction's ids).
    enum ContextAction { ClearLane = 1, RemoveLane, DeletePoint, CurveHold, CurveLinear, ResetVelocities };

    // `roll` must outlive this component (the roll owns it).
    explicit PianoRollControllerLanes(PianoRollComponent& roll);
    ~PianoRollControllerLanes() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // ---- Lane selection (in-session; survives clip switches) ----
    // kVelocityLane or a CC number 0..127; anything else is ignored.
    void selectLane(int laneId);
    int getSelectedLane() const noexcept;
    // The selector menu (item id = laneMenuItemId(laneId)) and its result handler.
    juce::PopupMenu buildLaneMenu() const;
    void handleLaneMenuResult(int itemId);
    static int laneMenuItemId(int laneId) noexcept;

    // ---- Right-click menu ----
    // `pos` is strip-local; point actions apply to the CC handle under it.
    juce::PopupMenu buildContextMenu(juce::Point<int> pos) const;
    void performContextAction(int action, juce::Point<int> pos);

    // ---- Called by the roll ----
    // The velocity the live gesture previews for `id` (the roll colours the note by it), if any.
    std::optional<int> previewVelocityFor(synth::NoteId id) const;
    void cancelGesture();
    void refreshFromDoc();

    // ---- Geometry (strip-local) ----
    juce::Rectangle<int> getSelectorBounds() const noexcept;
    juce::Rectangle<int> getValueArea() const noexcept;
    int yForValue(double value) const noexcept; // value in 0..127
    double valueForY(int y) const noexcept;
    int xForClipBeat(double clipBeat) const noexcept;
    // The value readout's rect for a pointer at `pos` (strip-local).
    juce::Rectangle<int> readoutBoundsFor(juce::Point<int> pos) const noexcept;

    // ---- Test hooks ----
    bool isGestureActiveForTest() const noexcept;
    // The velocity the lane is currently drawing for a note (preview or doc), or -1.
    int displayedVelocityForTest(synth::NoteId id) const;
    // The CC points the lane is currently drawing (preview or doc).
    std::vector<synth::ControllerPoint> displayedPointsForTest() const;
    // Hover state: the hovered CC handle index / velocity bar start (clip beat), if any.
    std::optional<size_t> getHoveredHandleForTest() const noexcept;
    std::optional<double> getHoveredBarBeatForTest() const noexcept;
    // The readout text a live drag shows ("" when idle).
    juce::String getReadoutTextForTest() const;

private:
    enum class Gesture { None, VelocityBar, VelocityFreehand, VelocityLine, CcMove, CcFreehand, CcLine, CcErase };

    // ---- Shared (PianoRollControllerLanes.cpp) ----
    const synth::Clip* openClip() const;
    double clipBeatAtX(int x, bool snap) const;
    std::vector<synth::ControllerPoint> docPoints() const;
    const std::vector<synth::ControllerPoint>& shownPoints() const;
    std::optional<size_t> handleAt(juce::Point<int> pos) const;
    void commitVelocities(const std::vector<std::pair<synth::NoteId, int>>& velocities);
    void commitPoints(const std::vector<synth::ControllerPoint>& points, bool removeLane = false);
    void showLaneMenu();
    void showContextMenu(juce::Point<int> pos);
    void setPreviewVelocities(std::map<std::int64_t, int> next);
    void updateHover(juce::Point<int> pos);
    juce::Rectangle<int> barColumnFor(double clipBeat) const;
    juce::Rectangle<int> handleRectFor(size_t index) const;
    juce::String readoutText() const;

    // ---- Velocity gestures (PianoRollControllerLanesVelocity.cpp) ----
    void beginVelocityGesture(juce::Point<int> pos, bool line);
    void dragVelocityGesture(juce::Point<int> pos);
    void endVelocityGesture();
    std::vector<synth::NoteId> barNotesAt(int x) const;
    std::vector<synth::NoteId> selectedNoteIds() const;

    // ---- CC gestures (PianoRollControllerLanesCC.cpp) ----
    void beginCcGesture(juce::Point<int> pos, const juce::ModifierKeys& mods);
    void dragCcGesture(juce::Point<int> pos);
    void endCcGesture(bool dragged);

    // ---- Painting (PianoRollControllerLanesPainting.cpp) ----
    void paintSelector(juce::Graphics& g);
    void paintVelocityLane(juce::Graphics& g);
    void paintCcLane(juce::Graphics& g);
    void paintGuides(juce::Graphics& g);
    void paintReadout(juce::Graphics& g);

    PianoRollComponent& roll_;
    int selectedLane_ = kVelocityLane;

    Gesture gesture_ = Gesture::None;
    juce::Point<int> anchor_;
    juce::Point<int> lastPos_;
    double anchorBeat_ = 0.0;
    double anchorValue_ = 0.0;
    std::vector<synth::NoteId> gestureNotes_;               // VelocityBar: the notes under the bar
    std::map<std::int64_t, int> previewVelocities_;         // note id -> previewed velocity
    std::vector<synth::ControllerPoint> origPoints_;        // CC: the lane as it was at mouse-down
    std::vector<synth::ControllerPoint> preview_;           // CC: what the gesture would commit
    std::vector<synth::ControllerPoint> stroke_;            // CcFreehand: raw samples, sorted by beat
    std::optional<size_t> movingIndex_;                     // CcMove: index into origPoints_
    std::set<size_t> erased_;                               // CcErase: indices into origPoints_
    mutable std::vector<synth::ControllerPoint> idleCache_; // shownPoints() with no gesture running
    std::optional<size_t> hoveredHandle_;                   // CC: handle under the pointer (idle only)
    std::optional<double> hoveredBarBeat_;                  // Velocity: bar start under the pointer (idle only)
    bool selectorHovered_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollControllerLanes)
};

} // namespace synth::ui
