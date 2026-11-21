// DrumMachineComponent: the groove controls. The Swing and Humanise fields in the header and the timing of a swung
// hit's flash. Both live on the clip and are applied when the snapshot is built (docs/timeline/drum-machine.md#swing).
// The lanes under the grid are in DrumMachineLanes.cpp.

#include "DrumMachineComponent.h"

#include "AppUndoManager.h"
#include "UI/Layout/FadeVisibility.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kPercentMin = 0.0;
constexpr double kPercentMax = 100.0;
} // namespace

void DrumMachineComponent::setupGroove() {
    setupSwing();
    setupHumanise();
    setupLanes();
}

//==============================================================================
// The Swing field reads 0% (straight, stored 0.5) to 100% (stored 0.75). Its steps only move the shown number; the
// clip is written once when the gesture ends (a click, a hold, a drag, a key, a reset), so each is one undo step.
void DrumMachineComponent::setupSwing() {
    addAndMakeVisible(swingField_);
    swingField_.setComponentID("drumSwing");
    swingField_.setRange(static_cast<int>(kPercentMin), static_cast<int>(kPercentMax), 0);
    swingField_.setWidestValueText("100%");
    swingField_.setTypable(true);
    swingField_.setTooltip("Swing: push every second step later (0% is straight).\n"
                           "Click an arrow or press Up and Down to step, hold to repeat,\n"
                           "drag the number (Shift for fine), double-click to type,\n"
                           "Cmd-click to reset.");
    swingField_.onGestureBegin = [this] { swingGesture_ = true; };
    swingField_.onStep = [this](int) { swingField_.setValueText(juce::String(swingField_.getValue()) + "%"); };
    swingField_.onGestureEnd = [this] {
        swingGesture_ = false;
        commitSwingField();
    };
    swingField_.onTypedValue = [this](const juce::String& text) {
        if (text.containsAnyOf("0123456789"))
            swingField_.setValue(juce::jlimit(0, 100, text.getIntValue()));
        commitSwingField();
        showSwing();
    };
    showSwing();
}

void DrumMachineComponent::commitSwingField() { setSwing(percentToSwing(swingField_.getValue())); }

void DrumMachineComponent::showSwing() {
    const int percent = juce::roundToInt(swingToPercent(swing_));
    swingField_.setValue(percent);
    swingField_.setValueText(juce::String(percent) + "%");
}

void DrumMachineComponent::setSwing(double swing) {
    const auto* c = clip();
    if (c == nullptr || !synth::Clip::isValidSwing(swing) || swing == c->swing)
        return;
    const auto id = clipId_;
    record([this, id, swing] { doc_->setClipSwing(id, swing); });
    refreshFromDoc();
}

// Humanise is the same kind of field: 0 to 100% shown, 0 to 1 stored, one undo step per gesture.
void DrumMachineComponent::setupHumanise() {
    addAndMakeVisible(humaniseField_);
    humaniseField_.setComponentID("drumHumanise");
    humaniseField_.setRange(static_cast<int>(kPercentMin), static_cast<int>(kPercentMax), 0);
    humaniseField_.setWidestValueText("100%");
    humaniseField_.setTypable(true);
    humaniseField_.setTooltip("Humanise: small random changes to each hit's loudness and timing (0% is exact).\n"
                              "Click an arrow or press Up and Down to step, hold to repeat,\n"
                              "drag the number (Shift for fine), double-click to type,\n"
                              "Cmd-click to reset.");
    humaniseField_.onGestureBegin = [this] { humaniseGesture_ = true; };
    humaniseField_.onStep = [this](int) { humaniseField_.setValueText(juce::String(humaniseField_.getValue()) + "%"); };
    humaniseField_.onGestureEnd = [this] {
        humaniseGesture_ = false;
        commitHumaniseField();
    };
    humaniseField_.onTypedValue = [this](const juce::String& text) {
        if (text.containsAnyOf("0123456789"))
            humaniseField_.setValue(juce::jlimit(0, 100, text.getIntValue()));
        commitHumaniseField();
        showHumanise();
    };
    showHumanise();
}

void DrumMachineComponent::commitHumaniseField() { setHumanise(humaniseField_.getValue() / 100.0); }

void DrumMachineComponent::showHumanise() {
    const int percent = juce::roundToInt(humanise_ * 100.0);
    humaniseField_.setValue(percent);
    humaniseField_.setValueText(juce::String(percent) + "%");
}

void DrumMachineComponent::setHumanise(double amount) {
    const auto* c = clip();
    if (c == nullptr || !synth::Clip::isValidHumanise(amount) || amount == c->humanise)
        return;
    const auto id = clipId_;
    record([this, id, amount] { doc_->setClipHumanise(id, amount); });
    refreshFromDoc();
}

// Follows the clip: the knobs, and the swing the cells' tick and the flash timing use.
void DrumMachineComponent::refreshGroove() {
    const auto* c = clip();
    swing_ = c != nullptr ? c->swing : synth::Clip::kStraightSwing;
    humanise_ = c != nullptr ? c->humanise : 0.0;
    if (!swingGesture_)
        showSwing();
    if (!humaniseGesture_)
        showHumanise();
    refreshLane();
}

//==============================================================================
// A swung step plays up to half a step late, so its hits flash when the playhead reaches that time, not on entering
// the column.
void DrumMachineComponent::flashStepHits(int step) {
    for (const auto& slot : slots_) {
        // A drum with its own length plays cell (step mod length) past its end, and that is the cell that flashes.
        const int length = getRowLength(slot.lane.pitch);
        const int source = length > 0 && step >= length ? step % length : step;
        if (cells_.count(drumgrid::cellKey(slot.lane.pitch, source)) > 0 && !rowSilenced(slot.lane.pitch))
            beginFlash(drumgrid::cellKey(slot.lane.pitch, step));
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
