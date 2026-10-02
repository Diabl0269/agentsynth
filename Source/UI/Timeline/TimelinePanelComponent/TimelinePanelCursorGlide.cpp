// TimelinePanelCursorGlide.cpp
//
// The panel's side of the cursor glide: matching the two glide actions, seeing the key go up, the
// Host that connects TimelineCursorGlide to the transport / snap grid / view scroll, and the page
// flip that keeps the cursor on screen (shared with follow-playhead).

#include "TimelinePanelComponent.h"

#include "ShortcutManager/ShortcutManager.h"

namespace synth::ui {

namespace {
// Whether the glide key and every modifier it was pressed with are physically down right now.
bool keyAndModifiersDown(const juce::KeyPress& key) {
    if (!juce::KeyPress::isKeyCurrentlyDown(key.getKeyCode()))
        return false;
    const int wanted = key.getModifiers().getRawFlags() & juce::ModifierKeys::allKeyboardModifiers;
    const int current = juce::ModifierKeys::currentModifiers.getRawFlags() & juce::ModifierKeys::allKeyboardModifiers;
    return (current & wanted) == wanted;
}
} // namespace

bool TimelinePanelComponent::handleCursorGlideKey(const juce::KeyPress& key) {
    if (transport_ == nullptr)
        return false;
    const auto command = juce::ModifierKeys::commandModifier;
    const bool back = matchesAction(key, "timelineGlideBack", juce::KeyPress(juce::KeyPress::leftKey, command, 0));
    const bool forward =
        !back && matchesAction(key, "timelineGlideForward", juce::KeyPress(juce::KeyPress::rightKey, command, 0));
    if (!back && !forward)
        return false;
    return cursorGlide_.press(back ? synth::GlideDirection::Back : synth::GlideDirection::Forward, key);
}

// Both overrides only observe: the release check is cheap and idle when no glide key is held.
bool TimelinePanelComponent::keyStateChanged(bool /*isKeyDown*/) {
    cursorGlide_.keyStateMayHaveChanged();
    return false;
}

void TimelinePanelComponent::modifierKeysChanged(const juce::ModifierKeys& /*modifiers*/) {
    cursorGlide_.keyStateMayHaveChanged();
}

// The playhead's own rule: a beat outside the visible span pages the view so it lands about 10%
// into the new page, rather than flush against the seam where the music after it is hidden.
void TimelinePanelComponent::pageViewToShowBeat(double beat) {
    if (viewState_.pixelsPerBeat <= 0.0)
        return;
    const double visibleBeats = (double)gridLanesBounds_.getWidth() / viewState_.pixelsPerBeat;
    const double lastVisibleBeat = viewState_.firstVisibleBeat + visibleBeats;
    if (beat >= viewState_.firstVisibleBeat && beat <= lastVisibleBeat)
        return;
    viewState_.firstVisibleBeat = std::max(0.0, beat - 0.1 * visibleBeats);
    ruler_.repaint();
    repaint();
}

TimelineCursorGlide::Host TimelinePanelComponent::makeCursorGlideHost() {
    TimelineCursorGlide::Host host;
    host.nowMs = [] { return juce::Time::getMillisecondCounterHiRes(); };
    host.cursorBeat = [this] {
        return transport_ == nullptr ? 0.0
                                     : synth::effectiveCursorBeat(*nudge_, transport_->getPositionSnapshot(),
                                                                  juce::Time::getMillisecondCounter());
    };
    host.beatsPerBar = [this] {
        return transport_ == nullptr ? 4.0 : synth::beatsPerBarOf(transport_->getPositionSnapshot());
    };
    host.gridBeats = [this] {
        return viewState_.divisionBeats(
            transport_ == nullptr ? 4.0 : synth::beatsPerBarOf(transport_->getPositionSnapshot()));
    };
    host.moveCursor = [this](double beat) {
        if (transport_ == nullptr)
            return;
        synth::locateTransportTracked(*transport_, *nudge_, beat);
        // The playhead otherwise repaints on the 10 Hz poll while stopped; handing it the fresh
        // position each frame is what makes the glide read as motion.
        playhead_.updateFromTransport(transport_->getPositionSnapshot(), lastOutputLatencySeconds_);
    };
    host.ensureVisible = [this](double beat) {
        if (!pianoRoll_.isOpen())
            pageViewToShowBeat(beat);
    };
    host.isKeyHeld = keyAndModifiersDown;
    return host;
}

} // namespace synth::ui
