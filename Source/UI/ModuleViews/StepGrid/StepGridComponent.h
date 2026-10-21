#pragma once

#include "Modules/Sequencer/SequencerModuleBase.h"
#include "StepGridLayout.h"
#include "StepGridModel.h"
#include "StepGridPageStrip.h"
#include "UI/Graph/CardBody/CardBodyViews.h"
#include "UI/Layout/LaneChips/LaneChipRow.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** What the card gives the step grid after it is built: the undo record, and the card's height hooks. */
struct StepGridHooks {
    /** One finished edit (a drag, a key, a click): the module's extra state before and after. */
    std::function<void(const juce::var& before, const juce::var& after)> recordEdit;
    /** The view's height changed (every frame of a lane folding in or out): the card measures itself again. */
    std::function<void()> layoutChanged;
    /** The neighbours make room: once when a lane starts to open, once when a lane has closed. */
    std::function<void()> makeRoom;
};

/**
 * The sequencer card's step grid: a page strip, the Pitch lane as a mini piano roll, a row of step pads, the lane
 * chips and the other lanes stacked under them, editing the module's pattern (extra state) directly. 16 columns
 * show the viewed page; steps past Steps are dimmed, never removed. One Tab stop for the grid; the page strip and
 * the chip row are their own. Lanes shown are saved with the node (CardViewState::stepGridLanes). Edits are one undo
 * step per gesture through StepGridHooks. docs/modules/sequencer-design.md#card, docs/layout/lane-chips.md.
 */
class StepGridComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public synth::CardAnimatedHeightView {
public:
    explicit StepGridComponent(SequencerModuleBase& module);
    ~StepGridComponent() override;

    /** The height at the lanes the module remembers, for a static measure; needs no component. */
    static int preferredHeightFor(SequencerModuleBase& module);

    void setHooks(StepGridHooks hooks) { hooks_ = std::move(hooks); }
    bool hasHooks() const noexcept { return static_cast<bool>(hooks_.recordEdit); }

    /** The 15 Hz tick from the card: playhead, pattern generation and the parameters the grid draws. */
    void poll();

    // ---- Lanes ---------------------------------------------------------------------------------------
    std::uint16_t getLanes() const noexcept { return lanes_; }
    /** Shows exactly `mask` (lanes the module cannot show are dropped), remembers it on the module and folds lanes
     *  in or out with motion. */
    void setLanes(std::uint16_t mask);
    LaneChipRow& getChipRow() noexcept { return chips_; }
    int currentLayoutHeight() const override;

    // ---- Pages ---------------------------------------------------------------------------------------
    StepGridPageStrip& getPageStrip() noexcept { return pages_; }
    int getViewedPage() const noexcept { return page_; }
    /** Views `page` (clamped); the columns slide 160 ms unless motion is off or the view is not showing. */
    void setViewedPage(int page);
    bool isFollowing() const noexcept { return follow_; }
    void setFollowing(bool follow);

    // ---- Focus and geometry (keyboard, tests) --------------------------------------------------------
    int getFocusedStep() const noexcept { return focusStep_; }
    SequencerLane getFocusedLane() const noexcept { return focusLane_; }
    /** The cell of `step` in `lane`, in this component's coordinates; empty when the lane is folded. */
    juce::Rectangle<int> cellBounds(SequencerLane lane, int step) const;
    juce::Rectangle<int> padBounds(int step) const;
    /** The words a screen reader hears for the focused cell: "Step 5 of 16, F2, on, chance 100 percent". */
    juce::String describeFocus() const;
    /** The note a Pitch lane bar sits at: the pitch the step plays. */
    int playedPitchOf(int step) const;
    /** Test seams: land every running motion at once. */
    void finishMotionForTest();
    bool isMotionRunning() const;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    struct Hit {
        enum class Kind { None, Pad, Lane } kind = Kind::None;
        SequencerLane lane = SequencerLane::Pitch;
        int step = 0;
    };
    struct Gesture {
        bool active = false;
        Hit start;
        juce::var before;
        int lastStep = -1;
        float lastNormal = 0.0f;
        bool paintOn = false; ///< Pads: the state the stroke paints.
        int startPlayed = 0;  ///< Pitch: the played pitch when the drag began.
        std::optional<stepgrid::PitchRange> frozenRange;
    };

    // StepGridComponent.cpp
    void syncSettings();
    void syncPattern();
    void updatePages();
    void layoutParts();
    stepgrid::StepGridLayout currentLayout() const;
    void rebuildScale();
    void refreshPitchRange();
    bool laneShown(SequencerLane lane) const noexcept { return (lanes_ & sequencerLaneBit(lane)) != 0; }
    double columnWidth() const;
    double columnLeft(int step) const;
    Hit hitTest(juce::Point<int> p) const;
    int stepAtX(int x) const;
    int stepCount() const { return stepsParam_ != nullptr ? stepsParam_->get() : 8; }
    int runningStep() const;

    // StepGridMotion.cpp
    void startRevealMotion(std::uint16_t from, std::uint16_t to);
    void startPageSlide(float toPage);
    void startGlide(const SequencerPattern& from);
    void startDim(int fromSteps);
    float glideMix(int step) const;
    float dimAmount(int step) const;

    // StepGridPaint.cpp
    void paintGutter(juce::Graphics& g, const stepgrid::StepGridLayout& layout);
    void paintColumnBackdrop(juce::Graphics& g, const stepgrid::StepGridLayout& layout);
    void paintPitchLane(juce::Graphics& g, juce::Rectangle<int> lane);
    void paintBarLane(juce::Graphics& g, SequencerLane laneId, juce::Rectangle<int> lane);
    void paintSlideLane(juce::Graphics& g, juce::Rectangle<int> lane);
    void paintConditionLane(juce::Graphics& g, juce::Rectangle<int> lane);
    void paintPads(juce::Graphics& g, juce::Rectangle<int> row);
    void paintFocus(juce::Graphics& g, const stepgrid::StepGridLayout& layout);
    float shownNormal(SequencerLane lane, int step) const;
    float shownPitch(int step) const;
    juce::String chordLabel(int step, int playedRoot) const;

    // StepGridEdit.cpp
    void beginGesture(const Hit& hit);
    void applyAt(const Hit& hit, juce::Point<int> p, bool shift);
    void endGesture();
    void editStep(int step, const std::function<void(SequencerStep&)>& edit);
    void commit(const juce::var& before);
    void setPlayedPitch(int step, int played);
    int pitchAtY(int y, const stepgrid::StepGridLayout& layout) const;
    void openConditionMenu(int step);
    void setStepCondition(int step, int hit, int of);

    // StepGridKeys.cpp
    bool moveFocus(int dx, int dy, bool shift);
    bool changeFocusedValue(int direction, bool shift);
    void moveFocusLane(int direction);
    void setFocusStep(int step);

    SequencerModuleBase& module_;
    juce::AudioParameterInt* stepsParam_ = nullptr;
    juce::AudioParameterBool* runParam_ = nullptr;
    StepGridHooks hooks_;
    StepGridPageStrip pages_;
    LaneChipRow chips_;

    std::uint16_t lanes_ = kDefaultSequencerLanes;
    std::array<float, kSequencerLaneCount> reveal_{};
    bool settling_ = false;
    std::uint16_t revealTarget_ = kDefaultSequencerLanes;

    int page_ = 0;
    float slidePos_ = 0.0f; ///< The viewed page while columns slide (fractional between pages).
    bool follow_ = false;

    int seenGeneration_ = -1;
    SequencerPattern lastSeen_;
    int seenActive_ = -1;
    int seenSteps_ = -1;
    int seenTranspose_ = 0;
    int seenRoot_ = -1;
    int seenScale_ = -1;
    std::uint16_t seenMask_ = 0;
    bool seenRunning_ = false;
    MusicalScale scale_;
    int transpose_ = 0;
    stepgrid::PitchRange pitchRange_;

    Gesture gesture_;
    int focusStep_ = 0;
    SequencerLane focusLane_ = SequencerLane::Pitch;

    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver revealAnim_;
    AnimationDriver pageAnim_;
    AnimationDriver glideAnim_;
    AnimationDriver dimAnim_;
    std::array<float, kSequencerLaneCount> revealFrom_{};
    std::array<float, kSequencerLaneCount> revealTo_{};
    float pageFrom_ = 0.0f;
    SequencerPattern glideFrom_;
    float glideElapsed_ = 1.0f; ///< 0..1 of the whole glide including the stagger.
    bool gliding_ = false;
    int dimFromSteps_ = 0;
    float dimT_ = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepGridComponent)
};

} // namespace synth::ui
