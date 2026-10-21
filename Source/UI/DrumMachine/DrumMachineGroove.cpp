// DrumMachineComponent: the groove controls. The Swing knob in the header, the row the lanes edit (picked by a row's
// name), the lane chips under the grid and the Loudness lane they fold in, and the timing of a swung hit's flash.
// Swing itself lives on the clip and is applied when the snapshot is built (docs/timeline/drum-machine.md#swing).

#include "DrumMachineComponent.h"

#include "AppUndoManager.h"
#include "UI/Layout/FadeVisibility.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kSwingPercentMin = 50.0;
constexpr double kSwingPercentMax = 75.0;
} // namespace

void DrumMachineComponent::setupGroove() {
    addAndMakeVisible(swingKnob_);
    swingKnob_.setComponentID("drumSwing");
    swingKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    swingKnob_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    swingKnob_.setRange(kSwingPercentMin, kSwingPercentMax, 1.0);
    swingKnob_.setDoubleClickReturnValue(true, kSwingPercentMin);
    swingKnob_.setValue(kSwingPercentMin, juce::dontSendNotification);
    swingKnob_.setTitle("Swing");
    swingKnob_.setTooltip("Swing: push every second step later (50% is straight)");
    swingKnob_.textFromValueFunction = [](double value) { return juce::String(juce::roundToInt(value)) + "%"; };
    swingKnob_.onDragStart = [this] { swingDragging_ = true; };
    // A value that changes outside a drag (Home and End, a double-click reset) commits at once; a drag commits on
    // release, so the whole turn is one undo step.
    swingKnob_.onValueChange = [this] {
        repaint(swingValueBounds_);
        if (!swingDragging_ && !updatingKnob_)
            commitSwingKnob();
    };
    swingKnob_.onDragEnd = [this] {
        swingDragging_ = false;
        commitSwingKnob();
    };

    addAndMakeVisible(chips_);
    chips_.setComponentID("drumLaneChips");
    chips_.setLanes({"Loudness"});
    chips_.onToggle = [this](int index, bool shown) {
        if (index == 0)
            setLoudnessShown(shown);
    };

    addChildComponent(lane_);
    DrumLoudnessLane::Mapping mapping;
    mapping.gutterWidth = kLabelColumnWidth;
    mapping.stepRange = [this](int step) { return stepXRange(step); };
    mapping.stepAt = [this](int x) { return stepAtX(x); };
    lane_.setMapping(std::move(mapping));
    lane_.onCommit = [this](const std::vector<DrumLoudnessLane::Hit>& changed) { commitLoudness(changed); };
    labels_.onRowClicked = [this](int pitch) { setSelectedPitch(pitch); };
}

//==============================================================================
void DrumMachineComponent::commitSwingKnob() { setSwing(swingKnob_.getValue() / 100.0); }

void DrumMachineComponent::setSwing(double swing) {
    const auto* c = clip();
    if (c == nullptr || !synth::Clip::isValidSwing(swing) || swing == c->swing)
        return;
    const auto id = clipId_;
    record([this, id, swing] { doc_->setClipSwing(id, swing); });
    refreshFromDoc();
}

// Follows the clip: the knob, and the swing the cells' tick and the flash timing use.
void DrumMachineComponent::refreshGroove() {
    const auto* c = clip();
    swing_ = c != nullptr ? c->swing : synth::Clip::kStraightSwing;
    if (!swingDragging_) {
        updatingKnob_ = true;
        swingKnob_.setValue(swing_ * 100.0, juce::dontSendNotification);
        updatingKnob_ = false;
    }
    repaint(swingValueBounds_);
    refreshLane();
}

//==============================================================================
void DrumMachineComponent::setSelectedPitch(int pitch) {
    if (pitch < 0 || pitch > 127 || pitch == selectedPitch_)
        return;
    selectedPitch_ = pitch;
    pushRowsToLabels();
    refreshLane();
    repaint();
}

void DrumMachineComponent::refreshLane() {
    std::vector<DrumLoudnessLane::Hit> hits;
    for (const auto& [key, cell] : cells_)
        if (static_cast<int>(key / 1000000) == selectedPitch_ && !cell.empty())
            hits.push_back({static_cast<int>(key % 1000000), cell.velocity});
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.step < b.step; });
    const auto lane = drumLaneForPitch(selectedPitch_);
    lane_.setHits(std::move(hits), lane.name, lane.colour);
    repaint(forBounds_);
}

// The lane's one gesture (a whole drag, or one key press) as ONE undo step.
void DrumMachineComponent::commitLoudness(const std::vector<DrumLoudnessLane::Hit>& changed) {
    std::vector<std::pair<std::vector<synth::NoteId>, int>> edits;
    for (const auto& hit : changed) {
        const auto it = cells_.find(drumgrid::cellKey(selectedPitch_, hit.step));
        if (it != cells_.end())
            edits.emplace_back(it->second.ids, juce::jlimit(1, 127, hit.velocity));
    }
    if (edits.empty() || clip() == nullptr)
        return;
    record([this, edits] {
        for (const auto& [ids, velocity] : edits)
            for (const auto id : ids)
                doc_->setNoteVelocity(id, velocity);
    });
    refreshFromDoc();
}

juce::Range<int> DrumMachineComponent::stepXRange(int step) const {
    const auto* c = clip();
    if (c == nullptr || step < 0)
        return {};
    const double stepLen = getStepBeats();
    const int x0 = static_cast<int>(std::lround(beatToX(c->startBeat + step * stepLen)));
    const int x1 = static_cast<int>(std::lround(beatToX(c->startBeat + (step + 1) * stepLen)));
    return {x0, std::max(x0 + 1, x1)};
}

//==============================================================================
// The band under the rows: the lane (as tall as its reveal) and, below it, the chips row.
int DrumMachineComponent::bottomBandHeight() const noexcept {
    return LaneChipRow::kHeight + 2 * kBottomPad + static_cast<int>(std::lround(kLaneHeight * laneReveal_));
}

int DrumMachineComponent::gridHeight() const noexcept {
    return std::max(0, getHeight() - canvasTop() - bottomBandHeight());
}

void DrumMachineComponent::layoutBottom() {
    const int laneHeight = static_cast<int>(std::lround(kLaneHeight * laneReveal_));
    const int chipsTop = getHeight() - LaneChipRow::kHeight - kBottomPad;
    chips_.setBounds(kBottomPad, chipsTop, std::max(0, getWidth() - 2 * kBottomPad), LaneChipRow::kHeight);
    lane_.setBounds(0, chipsTop - kBottomPad - laneHeight, getWidth(), laneHeight);
    lane_.setVisible(laneHeight > 0);
    const int chipsRight =
        chips_.getNumChips() > 0 ? chips_.getX() + chips_.getChip(chips_.getNumChips() - 1)->getRight() : 0;
    forBounds_ = {chipsRight + 8, chipsTop, std::max(0, getWidth() - chipsRight - 16), LaneChipRow::kHeight};
}

void DrumMachineComponent::setLoudnessShown(bool shown) {
    chips_.setShown(0, shown);
    startLaneMotion(shown ? 1.0f : 0.0f);
}

// The lane grows under the grid (and folds back) over 160 ms; the rows' viewport follows each frame. It lands at once
// when the grid is off screen or Animations is Off.
void DrumMachineComponent::startLaneMotion(float target) {
    laneTo_ = target;
    laneAnim_.stop(vblank_);
    const auto settle = [this] {
        laneReveal_ = laneTo_;
        layoutBottom();
        layoutRows();
        repaint();
    };
    if (!motionAllowed()) {
        settle();
        return;
    }
    laneFrom_ = laneReveal_;
    laneAnim_.start(
        vblank_, motionMs(kLaneMotionMs, kReducedMotionMs), easeOutCubic,
        [this](float t) {
            laneReveal_ = laneFrom_ + (laneTo_ - laneFrom_) * t;
            layoutBottom();
            layoutRows();
            repaint();
        },
        settle);
}

//==============================================================================
// A swung step plays up to half a step late, so its hits flash when the playhead reaches that time, not on entering
// the column.
void DrumMachineComponent::flashStepHits(int step) {
    for (const auto& slot : slots_) {
        const auto key = drumgrid::cellKey(slot.lane.pitch, step);
        if (cells_.count(key) > 0 && !rowSilenced(slot.lane.pitch))
            beginFlash(key);
    }
}

void DrumMachineComponent::flushSwungFlashes(double relativeBeat) {
    if (pendingSwung_.empty())
        return;
    const double stepLen = getStepBeats();
    for (auto it = pendingSwung_.begin(); it != pendingSwung_.end();) {
        const double due = (*it + (swing_ - synth::Clip::kStraightSwing) * 2.0) * stepLen;
        const bool reached = relativeBeat >= due;
        if (reached)
            flashStepHits(*it);
        // Gone once flashed, or once the playhead has left the step (a seek or a wrap).
        const bool left = playStep_ != *it;
        it = reached || left ? pendingSwung_.erase(it) : std::next(it);
    }
}

} // namespace synth::ui
