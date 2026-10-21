#pragma once

#include "DrumGrid.h"
#include "DrumLanes.h"
#include "DrumRowLabels.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/ScrollTween.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/TimelinePlayheadOverlay.h"
#include "UI/Timeline/TimelineViewState.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <unordered_map>
#include <vector>

class AppUndoManager;
class ShortcutManager;

namespace synth {
class TransportService;
}

// DrumMachineComponent: the step-grid editor for a clip on a Drum Kit track. TimelinePanelComponent swaps it in for
// the clip lanes (the same rect the piano roll uses) when such a clip is double-clicked. It reads and writes the same
// notes the piano roll shows; docs/timeline/drum-machine.md has the cell mapping and the gesture table.
namespace synth::ui {

class DrumMachineComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public TimelinePlayheadOverlay::LocalPlayheadClient {
public:
    static constexpr int kToolbarHeight = 20;
    static constexpr int kLabelColumnWidth = 132;
    static constexpr int kRowHeight = 24;
    static constexpr int kVelocityDragThresholdPx = 3;
    static constexpr double kVelocityPerPixel = 1.5;
    static constexpr double kHitMotionMs = 110.0;
    static constexpr double kFlashMs = 120.0;
    static constexpr double kRowMotionMs = 160.0;
    static constexpr double kReducedMotionMs = 80.0;
    static constexpr int kVelocityKeyStep = 10;

    struct Cell {
        int row = -1;
        int step = -1;
        bool operator==(const Cell& other) const noexcept { return row == other.row && step == other.step; }
        bool valid() const noexcept { return row >= 0 && step >= 0; }
    };

    DrumMachineComponent();
    ~DrumMachineComponent() override;

    // ---- Wiring (all non-owning; null degrades to read-only) ----
    void setTimelineDoc(synth::TimelineDoc* doc) noexcept { doc_ = doc; }
    void setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    void setShortcutManager(const ShortcutManager* manager) noexcept { shortcuts_ = manager; }
    void setScrollInverted(bool inverted) noexcept { scrollInverted_ = inverted; }
    void setZoomScrollInverted(bool inverted) noexcept { zoomScrollInverted_ = inverted; }

    // ---- Open and close ----
    void openClip(synth::ClipId id);
    void closeGrid();
    bool isOpen() const noexcept { return clipId_.isValid(); }
    synth::ClipId getClipId() const noexcept { return clipId_; }
    /** Called on every document change: follows the clip's notes, rows and settings; closes itself if the clip is gone.
     */
    void refreshFromDoc();

    /** Fired when the grid asks to close (Escape, or its clip disappearing), not by closeGrid(). */
    std::function<void()> onCloseRequested;
    /** Fired just before a close the grid asked for, so the owner can picture it. */
    std::function<void()> onAboutToClose;
    /** The "Piano roll" half of the view switch was chosen for the open clip. */
    std::function<void(synth::ClipId)> onPianoRollRequested;
    /** The grid's own beat-to-x mapping changed (zoom, scroll). */
    std::function<void()> onHorizontalViewChanged;
    /** A preview note on (`on` true) then off for the same pitch; the owner routes it to the track. */
    std::function<void(int pitch, float velocity01, bool on)> onAuditionNote;

    // ---- Geometry: the grid's own horizontal mapping ----
    /** Pixels the ruler band keeps blank below the toolbar for the panel's ruler. */
    void setRulerBandHeight(int heightPx);
    int canvasTop() const noexcept { return kToolbarHeight + rulerBandHeight_; }
    juce::Rectangle<int> gridRegion() const noexcept;
    int leftGutterWidth() const noexcept { return kLabelColumnWidth; }
    const TimelineViewState& getViewState() const noexcept { return view_; }
    double beatToX(double absBeat) const noexcept;
    double xToBeat(double x) const noexcept;
    void setHorizontalView(double pixelsPerBeat, double firstVisibleBeat);
    void zoomHorizontal(double factor);
    void zoomVertical(double factor);

    // ---- LocalPlayheadClient ----
    bool isLocalPlayheadActive() const override { return isOpen(); }
    void setPlayheadBeat(double absoluteBeat) override;
    /** The transport is running: the playhead's column is tinted and hits flash as it reaches them. */
    void setTransportPlaying(bool playing);

    // ---- Rows, cells and focus ----
    int getLaneCount() const noexcept;
    int getLanePitch(int row) const noexcept;
    int rowOfPitch(int pitch) const noexcept;
    int getStepCount() const;
    double getStepBeats() const;
    /** The cell's rectangle in this component's coordinates (empty when the row or step does not exist). */
    juce::Rectangle<int> cellBounds(int row, int step) const;
    /** 0 for an empty cell, else the loudest hit's velocity (the drag preview while one is live). */
    int getCellVelocity(int row, int step) const;
    std::optional<Cell> cellAt(juce::Point<int> pos) const;
    Cell getFocusCell() const noexcept { return focus_; }
    void setFocusCell(Cell cell);
    bool isShowAllDrums() const noexcept { return showAll_; }
    void setShowAllDrums(bool showAll);
    juce::String getAccessibilityValueText() const;

    CardSegmentedSwitch& getViewSwitch() noexcept { return viewSwitch_; }
    juce::ComboBox& getStepsCombo() noexcept { return stepsCombo_; }
    juce::Button& getShowAllButton() noexcept { return showAllButton_; }
    DrumRowLabels& getRowLabels() noexcept { return labels_; }
    bool isHitMotionActive(int row, int step) const;
    /** Test seams: animate although the grid is off screen, and land every running motion. */
    static void setAnimateOffScreenForTest(bool animate);
    void finishMotionsForTest();

    // ---- juce::Component ----
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    void visibilityChanged() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

protected:
    virtual void requestRepaintStrip(juce::Rectangle<int> strip) { repaint(strip); }

private:
    struct LaneSlot {
        DrumLane lane;
        float reveal = 1.0f;
        float from = 1.0f;
        float to = 1.0f;
    };
    struct Gesture {
        enum class Kind { None, PendingHit, Velocity };
        Kind kind = Kind::None;
        Cell cell;
        int pitch = 0;
        int startVelocity = 0;
        int startY = 0;
        int previewVelocity = 0;
        bool shift = false;
    };
    struct HitMotion {
        double startMs = 0.0;
        bool growing = true;
        int velocity = 0;
    };

    // DrumMachineComponent.cpp
    const synth::Clip* clip() const;
    double beatsPerBar() const;
    int stepsPerBar() const;
    void syncLanes(bool animate);
    void layoutRows();
    void pushRowsToLabels();
    void requestClose();
    void fitViewToClip();

    // DrumMachineGeometry.cpp
    int rowTop(int slotIndex) const noexcept;
    int rowHeightOf(const LaneSlot& slot) const noexcept;
    int slotIndexForRow(int row) const noexcept;
    double maxScrollY() const noexcept;
    bool setScrollY(double y);
    int stepAtX(double x) const noexcept;
    juce::Rectangle<int> columnStrip(int step) const;
    void ensureCellVisible(Cell cell);
    void notifyViewChanged();
    void zoomHorizontalAroundX(double factor, double anchorGridX);
    void zoomVerticalAround(double factor);
    bool rowSilenced(int pitch) const;

    // DrumMachineEdit.cpp
    void record(const std::function<void()>& mutation);
    void addHit(int pitch, int step, int velocity);
    void removeHits(int pitch, int step);
    void setCellVelocity(int pitch, int step, int velocity);
    void toggleAccent(int pitch, int step);
    void toggleHit(int pitch, int step, bool accent);
    void setStepsPerBar(int stepsPerBar);
    void setMuted(int pitch, bool muted);
    void setSoloed(int pitch, bool soloed);
    void audition(int pitch, int velocity);
    void rebuildCells(bool animate = true);

    // DrumMachineMotion.cpp
    bool motionAllowed() const;
    void beginHitMotion(drumgrid::CellKey key, bool growing, int velocity);
    void beginFlash(drumgrid::CellKey key);
    void startMotionClock();
    void tickMotion();
    float hitScaleAt(drumgrid::CellKey key, float& alphaOut) const;
    float flashAmount(drumgrid::CellKey key) const;
    void startRowMotion();
    juce::Rectangle<int> rectForKey(drumgrid::CellKey key) const;
    void finishRowMotion();

    // DrumMachinePainting.cpp
    void paintToolbar(juce::Graphics& g);
    void paintCells(juce::Graphics& g);
    void paintCell(juce::Graphics& g, const LaneSlot& slot, int row, int step, juce::Rectangle<int> rect, bool dense);
    void paintPlayhead(juce::Graphics& g);
    void layoutToolbar();

    // DrumMachineMouse.cpp
    void endGesture(bool commit);

    // DrumMachineKeys.cpp
    bool matchesAction(const juce::KeyPress& key, const juce::String& actionId, const juce::KeyPress& fallback) const;
    bool moveFocus(int dRow, int dStep);
    bool nudgeFocusedVelocity(int delta);
    void announceFocus();

    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::TransportService* transport_ = nullptr;
    const ShortcutManager* shortcuts_ = nullptr;
    bool scrollInverted_ = false;
    bool zoomScrollInverted_ = false;

    synth::ClipId clipId_;
    TimelineViewState view_;
    int rulerBandHeight_ = 0;
    double scrollY_ = 0.0;
    bool showAll_ = false;

    std::vector<LaneSlot> slots_;
    std::vector<int> rowTops_;
    std::unordered_map<drumgrid::CellKey, drumgrid::CellHits> cells_;
    double cellsStepBeats_ = 0.0;
    Cell focus_{0, 0};
    Gesture gesture_;

    double playheadBeat_ = 0.0;
    int playheadLineX_ = 0;
    bool hasPlayheadX_ = false;
    int playStep_ = -1;
    bool playing_ = false;

    std::unordered_map<drumgrid::CellKey, HitMotion> hitMotions_;
    std::unordered_map<drumgrid::CellKey, double> flashes_;
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver motionAnim_;
    AnimationDriver rowAnim_;
    ScrollTweenRunner wheelTween_;

    CardSegmentedSwitch viewSwitch_;
    juce::ComboBox stepsCombo_;
    juce::TextButton showAllButton_{"Show all drums"};
    DrumRowLabels labels_;
    juce::Rectangle<int> nameBounds_, stepsCaptionBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumMachineComponent)
};

} // namespace synth::ui
