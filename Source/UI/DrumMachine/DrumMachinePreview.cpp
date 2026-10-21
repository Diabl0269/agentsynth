// DrumMachineComponent: Play clip. The loop itself runs on the audio thread (TimelineMidiSourceModule); this unit owns
// the button, asks the owner to start and stop it, and follows its beat from the playhead's frames, so the playing
// column and the hit flashes need no timer of their own.

#include "DrumMachineComponent.h"

#include "ShortcutManager/ShortcutManager.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace synth::ui {

namespace {
constexpr int kMaxSkippedSteps = 8; // a playhead frame that jumped further is a seek, not a missed step
}

void DrumMachineComponent::refreshPlayClipTexts() {
    const auto key = shortcutHintFor(shortcuts_, "drumMachinePlayClip", juce::KeyPress(juce::KeyPress::spaceKey));
    playClipButton_.setTitle(previewing_ ? "Stop clip" : "Play clip");
    playClipButton_.setButtonText(previewing_ ? "Stop clip" : "Play clip");
    playClipButton_.setTooltip(formatShortcutHint(previewing_ ? "Stop looping this clip"
                                                              : "Loop this clip so you can hear it, even when the "
                                                                "song is stopped",
                                                  key));
}

void DrumMachineComponent::toggleClipPreview() {
    if (previewing_)
        stopClipPreview();
    else
        startClipPreview();
}

// Loops the clip's own range. The owner answers false when nothing can play it (no track plays this kit).
void DrumMachineComponent::startClipPreview() {
    const auto* c = clip();
    if (c == nullptr || !onStartClipPreview || !(c->lengthBeats > 0.0))
        return;
    const double start = c->startBeat;
    const double end = c->startBeat + c->lengthBeats;
    if (!onStartClipPreview(start, end)) {
        stopClipPreview(); // a restart the owner can no longer serve ends the loop
        return;
    }
    const bool changed = !previewing_;
    previewing_ = true;
    previewStart_ = start;
    previewEnd_ = end;
    playClipButton_.setToggleState(true, juce::dontSendNotification);
    refreshPlayClipTexts();
    if (changed && onClipPreviewChanged)
        onClipPreviewChanged(true);
    repaint();
}

void DrumMachineComponent::stopClipPreview() {
    if (!previewing_)
        return;
    previewing_ = false;
    if (onStopClipPreview)
        onStopClipPreview();
    playClipButton_.setToggleState(false, juce::dontSendNotification);
    refreshPlayClipTexts();
    if (playStep_ >= 0)
        requestRepaintStrip(columnStrip(playStep_));
    if (onClipPreviewChanged)
        onClipPreviewChanged(false);
    repaint();
}

// While looping the playhead is the loop's, not the (stopped) song's. A loop that has ended by itself, because the
// song started or its track went, is noticed here.
void DrumMachineComponent::followPreviewBeat(double& beat) {
    if (!previewing_)
        return;
    const double polled = clipPreviewBeat ? clipPreviewBeat() : std::numeric_limits<double>::quiet_NaN();
    if (std::isfinite(polled))
        beat = polled;
    else
        stopClipPreview();
}

// Flashes the hits of `step`, and of the steps a 30 Hz frame stepped over (a 64-step bar at speed moves faster than
// frames arrive); a loop that wrapped also covers the steps up to the clip end.
void DrumMachineComponent::flashSkippedSteps(int previous, int step) {
    const int count = getStepCount();
    int from = step;
    if (previous >= 0 && step > previous && step - previous <= kMaxSkippedSteps)
        from = previous + 1;
    else if (previewing_ && previous > step && count - previous + step <= kMaxSkippedSteps)
        from = (previous + 1) % std::max(1, count);
    for (int s = from;; s = (s + 1) % std::max(1, count)) {
        const bool swung = swing_ > synth::Clip::kStraightSwing && (s & 1) != 0;
        if (!swung)
            flashStepHits(s);
        else if (std::find(pendingSwung_.begin(), pendingSwung_.end(), s) == pendingSwung_.end())
            pendingSwung_.push_back(s); // flushSwungFlashes flashes it when its time comes
        if (s == step)
            break;
    }
}

} // namespace synth::ui
