// DrumMachineComponent: the groove controls. The Swing and Humanise knobs in the header and the timing of a swung hit's
// flash. Both live on the clip and are applied when the snapshot is built (docs/timeline/drum-machine.md#swing). The
// lanes under the grid are in DrumMachineLanes.cpp.

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

// A header knob that reads 0 to 100%: a drag commits on release, a key press or a double-click reset at once.
void DrumMachineComponent::setupPercentKnob(CardKnobSlider& knob, const juce::String& id, const juce::String& title,
                                            const juce::String& tooltip, const juce::Rectangle<int>& valueBounds,
                                            bool& dragging, bool& updating, std::function<void()> commit) {
    addAndMakeVisible(knob);
    knob.setComponentID(id);
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    knob.setRange(kPercentMin, kPercentMax, 1.0);
    knob.setDoubleClickReturnValue(true, kPercentMin);
    knob.setValue(kPercentMin, juce::dontSendNotification);
    knob.setTitle(title);
    knob.setTooltip(tooltip);
    knob.textFromValueFunction = [](double value) { return juce::String(juce::roundToInt(value)) + "%"; };
    knob.onDragStart = [&dragging] { dragging = true; };
    // A value that changes outside a drag (Home and End, a double-click reset) commits at once; a drag commits on
    // release, so the whole turn is one undo step.
    knob.onValueChange = [this, &valueBounds, &dragging, &updating, commit] {
        repaint(valueBounds);
        if (!dragging && !updating)
            commit();
    };
    knob.onDragEnd = [&dragging, commit] {
        dragging = false;
        commit();
    };
}

void DrumMachineComponent::setupGroove() {
    setupPercentKnob(swingKnob_, "drumSwing", "Swing", "Swing: push every second step later (0% is straight)",
                     swingValueBounds_, swingDragging_, updatingKnob_, [this] { commitSwingKnob(); });
    setupPercentKnob(humaniseKnob_, "drumHumanise", "Humanise",
                     "Humanise: small random changes to each hit's loudness and timing (0% is exact)",
                     humaniseValueBounds_, humaniseDragging_, updatingHumanise_, [this] { commitHumaniseKnob(); });
    setupLanes();
}

//==============================================================================
// The Swing knob reads 0% (straight, stored 0.5) to 100% (stored 0.75).
void DrumMachineComponent::commitSwingKnob() { setSwing(percentToSwing(swingKnob_.getValue())); }

void DrumMachineComponent::setSwing(double swing) {
    const auto* c = clip();
    if (c == nullptr || !synth::Clip::isValidSwing(swing) || swing == c->swing)
        return;
    const auto id = clipId_;
    record([this, id, swing] { doc_->setClipSwing(id, swing); });
    refreshFromDoc();
}

void DrumMachineComponent::commitHumaniseKnob() { setHumanise(humaniseKnob_.getValue() / 100.0); }

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
    if (!swingDragging_) {
        updatingKnob_ = true;
        swingKnob_.setValue(swingToPercent(swing_), juce::dontSendNotification);
        updatingKnob_ = false;
    }
    if (!humaniseDragging_) {
        updatingHumanise_ = true;
        humaniseKnob_.setValue(humanise_ * 100.0, juce::dontSendNotification);
        updatingHumanise_ = false;
    }
    repaint(swingValueBounds_);
    repaint(humaniseValueBounds_);
    refreshLane();
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
