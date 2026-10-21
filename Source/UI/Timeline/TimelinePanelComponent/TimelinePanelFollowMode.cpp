// TimelinePanelFollowMode.cpp
//
// Follow playhead's mode (Page or Smooth) and Smooth's pin point: the setters that persist and forward them, and the
// view move itself. The on/off flag and its persistence live in TimelinePanelLayout.cpp.

#include "TimelinePanelComponent.h"

namespace synth::ui {

// The mode menu's choice and the playhead's frames both land here.
void TimelinePanelComponent::wireFollowPlayheadModes() {
    followPlayheadButton_.onModeChosen = [this](FollowMode mode) { setFollowMode(mode); };
    // Smooth follow rides the playhead's own 30 Hz frames; Page follow rides the 10 Hz poll in updateFromTransport.
    playhead_.onDrawnBeat = [this](double beat) {
        if (follow_.enabled && follow_.mode == FollowMode::Smooth && playhead_.isPlayheadTimerRunning())
            followViewToBeat(beat);
    };
}

// Smooth moves the view on every frame so the cursor stays at its pin; Page defers to the cursor glide's page flip.
void TimelinePanelComponent::followViewToBeat(double beat) {
    if (clipEditorOpen() || clipLaneArea_.isDragInProgress() || viewState_.pixelsPerBeat <= 0.0)
        return;
    if (follow_.mode == FollowMode::Page) {
        pageViewToShowBeat(beat);
        return;
    }
    const double visibleBeats = (double)gridLanesBounds_.getWidth() / viewState_.pixelsPerBeat;
    const double origin =
        followViewOrigin(FollowMode::Smooth, follow_.pointPercent, beat, viewState_.firstVisibleBeat, visibleBeats);
    // Under half a pixel is no visible move: an unmoved view repaints nothing.
    if (std::abs(origin - viewState_.firstVisibleBeat) * viewState_.pixelsPerBeat < 0.5)
        return;
    viewState_.firstVisibleBeat = origin;
    ruler_.repaint();
    repaint();
}

void TimelinePanelComponent::setFollowMode(FollowMode mode) {
    follow_.mode = mode;
    followPlayheadButton_.mode = mode;
    pianoRoll_.setFollowStyle(follow_.mode, follow_.pointPercent);
    persistFollowPlayheadChoice();
    refreshShortcutTooltips();
}

// Settings writes the same key, so a value the panel already holds is a no-op and the settings broadcast cannot loop.
void TimelinePanelComponent::setFollowPointPercent(int percent) {
    percent = clampFollowPointPercent(percent);
    if (percent == follow_.pointPercent)
        return;
    follow_.pointPercent = percent;
    pianoRoll_.setFollowStyle(follow_.mode, follow_.pointPercent);
    persistFollowPlayheadChoice();
}

} // namespace synth::ui
