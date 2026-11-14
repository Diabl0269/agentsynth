// EQCurveKeyGlide.cpp -- held Alt+arrows on an EQCurveComponent: a tap steps the selected bands (the dots ease to the
// new place), holding glides them on (two arrows at once go diagonal) and the whole hold is one undo step, from the
// first real move to the last key-up. The shared KeyHold/KeyGlide model, run the way CurveEditorKeyGlide.cpp runs it.
// The curve is declared in EQCurveComponent.h.

#include "EQCurveHoldState.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <cmath>

namespace {
using Module = ParametricEQModule;
namespace lane_key = synth::ui::lane_key;
// A fifth of the axis range per second at first, up to 1.2 ranges a second: the pace of every other editor's glide.
const float kFrequencyRangeOctaves = std::log2(Module::kMaxFreq / Module::kMinFreq);
const float kGainRangeDb = 2.0f * Module::kMaxGainDb;
// One tap step: a semitone, one dB; Shift shrinks it to this fraction (as the keyboard step does).
constexpr float kHoldSemitoneOctaves = 1.0f / 12.0f;
constexpr float kHoldGainStepDb = 1.0f;
constexpr float kHoldFineFactor = 0.25f;
} // namespace

// The bands the keys move: those of the selection that are on (with nothing selected, the first one that is on).
std::vector<int> EQCurveComponent::movableSelection() {
    ensureSelectedBand();
    std::vector<int> bands;
    for (const int band : hold_->selection.selected())
        if (eqModule.isBandEnabled(band))
            bands.push_back(band);
    return bands;
}

// A press is one step; if its key is physically down the step becomes the start of a hold, whose frames carry on. A
// press whose key is not down (a synthesised event, a rebound non-arrow key) is the plain step.
bool EQCurveComponent::pressMove(int keyCode, float octaves, float dbDelta, bool fine) {
    auto& hold = hold_->hold;
    if (hold.active()) {
        if (hold.anyArrowDown())
            return true; // auto-repeat or a second key: the running hold picks the direction up
        hold.end();      // a hold whose key-up was missed
    }
    const bool alongTime = octaves != 0.0f;
    const bool arrowOfAxis =
        synth::ui::KeyHold::isArrow(keyCode) &&
        synth::ui::KeyHold::axisOf(keyCode) == (alongTime ? synth::ui::KeyAxis::time : synth::ui::KeyAxis::value);
    if (arrowOfAxis && hold.keyIsDown(keyCode) && beginKeyHold(keyCode, fine))
        return true;
    nudgeSelectedBand(octaves, dbDelta);
    return true;
}

bool EQCurveComponent::beginKeyHold(int keyCode, bool fine) {
    const auto bands = movableSelection();
    if (bands.empty() || dragBand >= 0)
        return false;
    hold_->start.clear();
    for (const int band : bands)
        hold_->start.push_back({band, {lastBands[(size_t)band].freqHz, lastBands[(size_t)band].gainDb}});
    const float scale = fine ? kHoldFineFactor : 1.0f;
    hold_->unitOctaves = kHoldSemitoneOctaves * scale;
    hold_->unitDb = kHoldGainStepDb * scale;
    hold_->gestureOpen = false;
    const auto motion = [](double step, double range) {
        return synth::ui::KeyAxisMotion{{lane_key::kValueStartRangePerSecond * range,
                                         lane_key::kValueMaxRangePerSecond * range, lane_key::kTapMs,
                                         lane_key::kRampMs},
                                        step,
                                        true}; // plain Left/Right and Up/Down pick and widen, so only Alt joins
    };
    const auto time = motion(hold_->unitOctaves, kFrequencyRangeOctaves);
    const auto value = motion(hold_->unitDb, kGainRangeDb);
    const bool alongTime = synth::ui::KeyHold::axisOf(keyCode) == synth::ui::KeyAxis::time;
    hold_->hold.begin(keyCode, alongTime ? time : value, alongTime ? value : time);
    glideMoves_ = true; // the tap step eases; the frames after it land where the keys have carried the bands
    applyKeyHold();
    glideMoves_ = false;
    return true;
}

// Moves the bands to where the keys have carried them from where the hold began; the first move that changes anything
// opens the undo step, which the end of the hold closes (a hold against a limit never reaches the undo stack).
void EQCurveComponent::applyKeyHold() {
    if (hold_->start.empty())
        return;
    const float octaves = (float)hold_->hold.distance(synth::ui::KeyAxis::time);
    const float db = (float)hold_->hold.distance(synth::ui::KeyAxis::value);
    bool changes = false;
    for (const auto& [band, place] : hold_->start) {
        const float freq = juce::jlimit(Module::kMinFreq, Module::kMaxFreq, place.x * std::pow(2.0f, octaves));
        const float gain = juce::jlimit(-Module::kMaxGainDb, Module::kMaxGainDb, place.y + db);
        changes = changes || freq != lastBands[(size_t)band].freqHz || gain != lastBands[(size_t)band].gainDb;
    }
    if (!changes)
        return;
    if (!hold_->gestureOpen) {
        beginGesture();
        hold_->gestureOpen = true;
    }
    moveBandsFrom(hold_->start, octaves, db);
    refreshFromModule();
}

void EQCurveComponent::endKeyHold() {
    if (hold_->gestureOpen)
        endGesture();
    hold_->gestureOpen = false;
    hold_->start.clear();
}

bool EQCurveComponent::keyStateChanged(bool) {
    hold_->hold.checkKeyUp();
    return false;
}

void EQCurveComponent::focusLost(FocusChangeType) {
    hold_->hold.end();
    repaint();
}
