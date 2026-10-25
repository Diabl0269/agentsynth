// DrumMachineComponent: motion. A hit grows in over 110 ms when added and shrinks out when removed, a hit flashes as
// the playing playhead reaches it, and rows grow in or out when "Show all drums" changes them. All of it is
// time-bounded (an AnimationDriver that stops itself), lands in its final state at once when the grid is not on screen
// or Animations is Off, and under Reduce Motion becomes a plain 80 ms fade. docs/layout/animation.md#motion-rules.

#include "DrumMachineComponent.h"

#include "UI/Layout/FadeVisibility.h"
#include <algorithm>

namespace synth::ui {

namespace {
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
double hitMotionDuration() {
    return motionMs(DrumMachineComponent::kHitMotionMs, DrumMachineComponent::kReducedMotionMs);
}
} // namespace

void DrumMachineComponent::setAnimateOffScreenForTest(bool animate) {
    FadeVisibility::setAnimateOffScreenForTest(animate);
}

bool DrumMachineComponent::motionAllowed() const { return FadeVisibility::canAnimateIn(this); }

juce::Rectangle<int> DrumMachineComponent::rectForKey(drumgrid::CellKey key) const {
    const int pitch = static_cast<int>(key / 1000000);
    const int step = static_cast<int>(key % 1000000);
    const int row = rowOfPitch(pitch);
    return row < 0 ? juce::Rectangle<int>() : cellBounds(row, step).expanded(2);
}

// `delayMs` holds a growing hit back (the left-to-right stagger of an idea); it draws nothing until its time.
void DrumMachineComponent::beginHitMotion(drumgrid::CellKey key, bool growing, int velocity, double delayMs) {
    if (!motionAllowed())
        return;
    hitMotions_[key] = {nowMs() + delayMs, growing, velocity};
    staggerTailMs_ = std::max(staggerTailMs_, delayMs);
    startMotionClock();
}

// A flash is not needed to understand anything, so Reduce Motion drops it.
void DrumMachineComponent::beginFlash(drumgrid::CellKey key) {
    if (!motionAllowed() || prefersReducedMotion())
        return;
    flashes_[key] = nowMs();
    startMotionClock();
}

// One driver serves every motion. Restarting it on each new motion keeps it running until the newest one ends; each
// motion reads its own progress from its start time.
void DrumMachineComponent::startMotionClock() {
    const double duration = std::max(kFlashMs, hitMotionDuration() + staggerTailMs_);
    motionAnim_.start(
        vblank_, duration, [](float t) { return t; }, [this](float) { tickMotion(); },
        [this] {
            hitMotions_.clear();
            flashes_.clear();
            ghostFades_.clear();
            staggerTailMs_ = 0.0;
            repaint();
        });
}

void DrumMachineComponent::tickMotion() {
    const double now = nowMs();
    const double hitMs = hitMotionDuration();
    for (const auto& [key, motion] : hitMotions_)
        repaint(rectForKey(key));
    for (const auto& [key, start] : flashes_)
        repaint(rectForKey(key));
    for (const auto& [key, start] : ghostFades_)
        repaint(rectForKey(key));
    for (auto it = hitMotions_.begin(); it != hitMotions_.end();)
        it = now - it->second.startMs >= hitMs ? hitMotions_.erase(it) : std::next(it);
    for (auto it = flashes_.begin(); it != flashes_.end();)
        it = now - it->second >= kFlashMs ? flashes_.erase(it) : std::next(it);
    for (auto it = ghostFades_.begin(); it != ghostFades_.end();)
        it = now - it->second >= hitMs ? ghostFades_.erase(it) : std::next(it);
}

// The scale a growing or shrinking hit is drawn at (1 when it has no motion), and under Reduce Motion the fade it takes
// instead.
float DrumMachineComponent::hitScaleAt(drumgrid::CellKey key, float& alphaOut) const {
    alphaOut = 1.0f;
    const auto it = hitMotions_.find(key);
    if (it == hitMotions_.end())
        return 1.0f;
    const float p = juce::jlimit(0.0f, 1.0f, static_cast<float>((nowMs() - it->second.startMs) / hitMotionDuration()));
    const bool growing = it->second.growing;
    if (prefersReducedMotion()) {
        alphaOut = growing ? p : 1.0f - p;
        return 1.0f;
    }
    return growing ? easeOutBackGrow(p) : 1.0f - easeInCubic(p);
}

float DrumMachineComponent::flashAmount(drumgrid::CellKey key) const {
    const auto it = flashes_.find(key);
    if (it == flashes_.end())
        return 0.0f;
    return 1.0f - juce::jlimit(0.0f, 1.0f, static_cast<float>((nowMs() - it->second) / kFlashMs));
}

bool DrumMachineComponent::isHitMotionActive(int row, int step) const {
    return slotIndexForRow(row) >= 0 && hitMotions_.count(drumgrid::cellKey(slots_[(size_t)row].lane.pitch, step)) > 0;
}

bool DrumMachineComponent::isFlashActive(int row, int step) const {
    return slotIndexForRow(row) >= 0 && flashes_.count(drumgrid::cellKey(slots_[(size_t)row].lane.pitch, step)) > 0;
}

void DrumMachineComponent::finishMotionsForTest() {
    motionAnim_.stop(vblank_);
    hitMotions_.clear();
    flashes_.clear();
    ghostFades_.clear();
    changed_.clear();
    changeAnim_.stop(vblank_);
    laneAnim_.completeForTest();
    rowAnim_.completeForTest();
    if (edgeAnim_.isRunning()) {
        edgeAnim_.completeForTest();
    }
    repaint();
}

//==============================================================================
// Rows arriving or leaving ("Show all drums", a note on a pitch outside the table) grow in or shrink out over 160 ms.
void DrumMachineComponent::startRowMotion() {
    rowAnim_.start(
        vblank_, motionMs(kRowMotionMs, kReducedMotionMs), easeOutCubic,
        [this](float t) {
            for (auto& slot : slots_)
                slot.reveal = slot.from + (slot.to - slot.from) * t;
            layoutRows();
            repaint();
        },
        [this] { finishRowMotion(); });
}

void DrumMachineComponent::finishRowMotion() {
    const int focusPitch = slotIndexForRow(focus_.row) >= 0 ? slots_[(size_t)focus_.row].lane.pitch : -1;
    for (auto& slot : slots_)
        slot.reveal = slot.from = slot.to;
    slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [](const LaneSlot& s) { return s.to <= 0.0f; }),
                 slots_.end());
    focus_.row = std::max(0, std::min(focus_.row, static_cast<int>(slots_.size()) - 1));
    for (size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].lane.pitch == focusPitch)
            focus_.row = static_cast<int>(i);
    layoutRows();
    repaint();
}

} // namespace synth::ui
