#pragma once

#include "DrumCellMarks.h"
#include "DrumGrid.h"
#include "DrumHitLane.h"
#include "DrumIdeasPanel.h"
#include "DrumLanes.h"
#include "DrumRowLabels.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Layout/LaneChips/LaneChipRow.h"
#include "UI/Layout/ScrollTween.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/TimelinePlayheadOverlay.h"
#include "UI/Timeline/TimelineViewState.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <unordered_map>
#include <vector>

class AppUndoManager;
class ShortcutManager;
namespace synth::ui::test_hooks {
/** A test sets this to receive the Ideas panel instead of showing it in a popover. */
std::function<void(std::unique_ptr<DrumIdeasPanel>)>& drumIdeasPanelHookForTest();
} // namespace synth::ui::test_hooks

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
    static constexpr int kLaneHeight = 72;
    static constexpr int kMinLaneHeight = 36; ///< a lane squeezes to this when the panel is short
    static constexpr int kMinGridHeight = 48; ///< the rows keep at least two rows of room above the lanes
    static constexpr int kBottomPad = 4;
    static constexpr double kLaneMotionMs = 160.0;
    static constexpr int kDefaultSelectedPitch = 36;
    static constexpr double kStaggerMsPerColumn = 8.0;
    static constexpr double kMaxStaggerMs = 400.0;
    static constexpr double kChangedOutlineMs = 1200.0;

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
    void setShortcutManager(const ShortcutManager* manager) noexcept {
        shortcuts_ = manager;
        refreshPlayClipTexts();
    }
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

    // ---- Play clip: loops the open clip, even with the song stopped ----
    /** Asks the owner to loop [startBeat, endBeat) (absolute beats) on the clip's track; false when nothing can. */
    std::function<bool(double startBeat, double endBeat)> onStartClipPreview;
    std::function<void()> onStopClipPreview;
    /** The loop's current absolute beat, NaN once it has ended. */
    std::function<double()> clipPreviewBeat;
    /** The loop started or stopped; the owner keeps the playhead's frames running while it plays. */
    std::function<void(bool playing)> onClipPreviewChanged;
    bool isClipPreviewing() const noexcept { return previewing_; }
    void startClipPreview();
    /** Stops the loop; every way the grid stops showing its clip ends it too. */
    void stopClipPreview();
    juce::Button& getPlayClipButton() noexcept { return playClipButton_; }

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

    // ---- Groove: swing and humanise (DrumMachineGroove.cpp) ----
    /** The clip's swing, 0.5 (straight) to 0.75; set through the clip, one undo step. The knob reads it as 0 to 100%.
     */
    double getSwing() const noexcept { return swing_; }
    void setSwing(double swing);
    /** The clip's humanise, 0 to 1; set through the clip, one undo step. The knob reads it as 0 to 100%. */
    double getHumanise() const noexcept { return humanise_; }
    void setHumanise(double amount);
    CardKnobSlider& getSwingKnob() noexcept { return swingKnob_; }
    CardKnobSlider& getHumaniseKnob() noexcept { return humaniseKnob_; }
    /** The knob positions (percent) of a clip's swing and back: 0.5 is 0%, 0.75 is 100%. */
    static double swingToPercent(double swing) noexcept { return (swing - synth::Clip::kStraightSwing) * 400.0; }
    static double percentToSwing(double percent) noexcept { return synth::Clip::kStraightSwing + percent / 400.0; }

    // ---- Lanes under the grid: Loudness, Chance, Repeat, Nudge, Condition (DrumMachineLanes.cpp) ----
    /** The row the lanes under the grid edit, by pitch; Kick until a row name is chosen. */
    int getSelectedPitch() const noexcept { return selectedPitch_; }
    void setSelectedPitch(int pitch);
    bool isLaneShown(DrumLaneKind kind) const noexcept { return bands_[(size_t)kind].to > 0.5f; }
    /** Shows or folds a lane (the same as its chip); lanes stack under the grid in chip order. */
    void setLaneShown(DrumLaneKind kind, bool shown);
    DrumHitLane& getLane(DrumLaneKind kind) noexcept { return *bands_[(size_t)kind].lane; }
    LaneChipRow& getLaneChips() noexcept { return chips_; }
    /** The height the lane band takes right now (it grows and folds with the lanes). */
    int bottomBandHeight() const noexcept;
    bool isLaneMotionRunning() const noexcept { return laneAnim_.isRunning(); }
    /** What the cell at (row, step) shows beyond its fill; nothing for an empty cell. */
    DrumCellMarks getCellMarks(int row, int step) const;

    // ---- Ideas: starters, variations, randomize (DrumMachineIdeas.cpp) ----
    juce::Button& getIdeasButton() noexcept { return ideasButton_; }
    void openIdeasPanel();
    /** Fills the clip with `style`'s pattern, as one undo step (the grid becomes 16 steps per bar, 32 for Trap). */
    void fillWithStarter(drumideas::Style style);
    void randomizePattern(double density, bool keepKickAndSnare, int seed);
    /** Puts a take in the clip (`replace`) or in a new clip right after it, and returns the clip that now holds it. */
    synth::ClipId pickTake(const std::vector<synth::MidiNote>& notes, bool replace);
    /** The owner is told when a pick made a new clip (it selects it); the grid has already opened it. */
    std::function<void(synth::ClipId)> onClipCreated;
    bool isChangedOutlineActive(int row, int step) const;
    /** Gives the Ideas button the keyboard focus (the popover closed). */
    void focusIdeasButton();
    int getIdeasFocusReturns() const noexcept { return ideasFocusReturns_; }
    const juce::String& getLastAnnouncement() const noexcept { return lastAnnouncement_; }

    CardSegmentedSwitch& getViewSwitch() noexcept { return viewSwitch_; }
    juce::ComboBox& getStepsCombo() noexcept { return stepsCombo_; }
    juce::Button& getShowAllButton() noexcept { return showAllButton_; }
    DrumRowLabels& getRowLabels() noexcept { return labels_; }
    bool isHitMotionActive(int row, int step) const;
    /** True while the hit at (row, step) is flashing because the playhead reached it. */
    bool isFlashActive(int row, int step) const;
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
    /** One lane under the grid with its reveal (0 folded, 1 grown); `from` and `to` are the motion's ends. */
    struct LaneBand {
        std::unique_ptr<DrumHitLane> lane;
        float reveal = 0.0f;
        float from = 0.0f;
        float to = 0.0f;
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
    void beginHitMotion(drumgrid::CellKey key, bool growing, int velocity, double delayMs = 0.0);
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
    struct CellPaint;
    void paintCellBase(juce::Graphics& g, const CellPaint& cell, int step);
    void paintCellHit(juce::Graphics& g, const LaneSlot& slot, const CellPaint& cell, int velocity);
    /** DrumMachineMarks.cpp: the marks of one hit, drawn in `area` (the cell, in the hit's scaled space). */
    void paintCellMarks(juce::Graphics& g, juce::Rectangle<float> area, const DrumCellMarks& marks, float alpha,
                        bool dense);
    void paintPlayhead(juce::Graphics& g);
    void paintLaneCaption(juce::Graphics& g);
    void layoutToolbar();

    // DrumMachineGroove.cpp
    void setupGroove();
    void setupPercentKnob(CardKnobSlider& knob, const juce::String& id, const juce::String& title,
                          const juce::String& tooltip, const juce::Rectangle<int>& valueBounds, bool& dragging,
                          bool& updating, std::function<void()> commit);
    void refreshGroove();
    void commitSwingKnob();
    void commitHumaniseKnob();
    void flushSwungFlashes(double relativeBeat);
    void flashStepHits(int step);

    // DrumMachineLanes.cpp
    void setupLanes();
    void refreshLane();
    void commitLane(DrumLaneKind kind, const std::vector<DrumHitLane::Hit>& changed);
    void layoutBottom();
    void startLaneMotion();
    int laneUnitHeight() const noexcept;
    int laneHeightOf(DrumLaneKind kind) const noexcept;
    int gridHeight() const noexcept;
    juce::Range<int> stepXRange(int step) const;

    // DrumMachineIdeas.cpp
    void setupIdeas();
    drumideas::Grid ideaGrid() const;
    DrumIdeasContext ideasContext() const;
    void applyIdeaNotes(std::vector<synth::MidiNote> notes, int requiredSteps, const juce::String& announcement);
    void markChanged(const std::vector<drumideas::ChangedCell>& cells);
    float changedAmount(drumgrid::CellKey key) const;
    void announce(const juce::String& text);

    // DrumMachineMouse.cpp
    void endGesture(bool commit);

    // DrumMachineAccessibility.cpp
    juce::String describeHitSettings(int pitch) const;

    // DrumMachineKeys.cpp
    bool matchesAction(const juce::KeyPress& key, const juce::String& actionId, const juce::KeyPress& fallback) const;
    bool moveFocus(int dRow, int dStep);
    bool nudgeFocusedVelocity(int delta);
    void announceFocus();

    // DrumMachinePreview.cpp
    void toggleClipPreview();
    void refreshPlayClipTexts();
    void followPreviewBeat(double& beat);
    void flashSkippedSteps(int previous, int step);
    bool showsPlayhead() const noexcept { return playing_ || previewing_; }

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

    double swing_ = synth::Clip::kStraightSwing;
    int selectedPitch_ = kDefaultSelectedPitch;
    double humanise_ = 0.0;
    bool swingDragging_ = false, updatingKnob_ = false;
    bool humaniseDragging_ = false, updatingHumanise_ = false;
    std::vector<int> pendingSwung_; ///< swung steps the playhead entered whose hits have not reached their time yet

    double playheadBeat_ = 0.0;
    int playheadLineX_ = 0;
    bool hasPlayheadX_ = false;
    int playStep_ = -1;
    bool playing_ = false;
    bool previewing_ = false;
    double previewStart_ = 0.0;
    double previewEnd_ = 0.0;

    std::unordered_map<drumgrid::CellKey, HitMotion> hitMotions_;
    std::unordered_map<drumgrid::CellKey, double> flashes_;
    std::unordered_map<drumgrid::CellKey, double> changed_; ///< cells an idea changed, by the time they were marked
    double staggerTailMs_ = 0.0;
    bool staggerNext_ = false;
    int ideasFocusReturns_ = 0;
    juce::String lastAnnouncement_;
    AnimationDriver changeAnim_;
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver motionAnim_;
    AnimationDriver rowAnim_;
    AnimationDriver laneAnim_;
    ScrollTweenRunner wheelTween_;

    CardSegmentedSwitch viewSwitch_;
    juce::ComboBox stepsCombo_;
    juce::TextButton showAllButton_{"Show all drums"};
    juce::TextButton playClipButton_{"Play clip"};
    juce::TextButton ideasButton_{"Ideas"};
    CardKnobSlider swingKnob_;
    CardKnobSlider humaniseKnob_;
    LaneChipRow chips_;
    std::array<LaneBand, kDrumLaneKindCount> bands_;
    DrumRowLabels labels_;
    juce::Rectangle<int> nameBounds_, stepsCaptionBounds_, swingCaptionBounds_, swingValueBounds_, forBounds_;
    juce::Rectangle<int> humaniseCaptionBounds_, humaniseValueBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumMachineComponent)
};

} // namespace synth::ui
