// PianoRollVelocityLane — construction, the host seam, and the strip's vertical mapping. Gestures
// live in PianoRollVelocityLaneGestures.cpp and painting in PianoRollVelocityLanePainting.cpp.

#include "PianoRollVelocityLane.h"

#include "UI/Chrome/ShortcutHint/ShortcutHintLayout.h"
#include "VelocityLaneMath.h"
#include <algorithm>

namespace synth::ui {

namespace {
// Room above velocity 127's head and below velocity 1's, so neither head is clipped by the strip's
// edge (the top edge also carries the separator line and the PanelResizeHandle::kHeight strip, so
// a 127 head, radius 3, sits just below it).
constexpr float kPlotTopPad = 8.0f;
constexpr float kPlotBottomPad = 4.0f;
} // namespace

PianoRollVelocityLane::PianoRollVelocityLane() {
    setComponentID("pianoRollVelocityLane");
    setOpaque(true); // paint() starts with a full-bounds fill
    // Takes focus on a press so Escape reaches keyPressed mid-drag; every other key falls through
    // to the roll, so the roll's own shortcuts keep working after a click in the strip.
    setWantsKeyboardFocus(true);
    setTitle("Velocity strip");
    setDescription(
        "Drag a stick to set a note's velocity; the Set box in the header edits the selected notes from the keyboard");
    // Added last so it wins the hit test over the top of the plot it overlaps.
    addAndMakeVisible(resizeHandle_);
    resizeHandle_.onResize = [this](int desired) {
        if (host_.onResizeRequest)
            host_.onResizeRequest(desired);
    };
    resizeHandle_.onResizeCommitted = [this](int desired) {
        if (host_.onResizeCommitted)
            host_.onResizeCommitted(desired);
    };
}

PianoRollVelocityLane::~PianoRollVelocityLane() { fade_.stop(vblank_); }

void PianoRollVelocityLane::resized() { resizeHandle_.setBounds(0, 0, getWidth(), PanelResizeHandle::kHeight); }

PanelResizeHandle& PianoRollVelocityLane::getResizeHandle() noexcept { return resizeHandle_; }
float PianoRollVelocityLane::getReadoutOpacity() const noexcept { return readoutOpacity_; }

void PianoRollVelocityLane::setHost(Host host) { host_ = std::move(host); }

bool PianoRollVelocityLane::isGestureActive() const noexcept { return gesture_ != Gesture::None; }
PianoRollVelocityLane::Gesture PianoRollVelocityLane::getGesture() const noexcept { return gesture_; }
synth::NoteId PianoRollVelocityLane::getReadoutNote() const noexcept { return readout_; }

float PianoRollVelocityLane::plotTop() const noexcept { return kPlotTopPad; }
float PianoRollVelocityLane::plotBottom() const noexcept {
    return std::max(kPlotTopPad + 1.0f, (float)getHeight() - kPlotBottomPad);
}

float PianoRollVelocityLane::yForVelocity(int velocity) const noexcept {
    return velocitylane::yForVelocity(velocity, plotTop(), plotBottom());
}

int PianoRollVelocityLane::velocityForY(float y) const noexcept {
    return velocitylane::velocityForY(y, plotTop(), plotBottom());
}

// The readout note changes only here. The value fades in when it appears (nothing -> a stick) and
// out when it goes (a stick -> nothing); hovering from one stick straight to another just moves the
// label at its current opacity. Both fades retarget from the CURRENT opacity, so a quick re-hover
// mid-fade never snaps. Headless (not showing) there is nothing to animate against: it lands at once.
void PianoRollVelocityLane::setReadout(synth::NoteId next) {
    if (next == readout_)
        return;
    const bool appeared = !readout_.isValid() && next.isValid();
    const bool vanished = readout_.isValid() && !next.isValid();
    readout_ = next;
    if (appeared)
        fadeReadoutIn();
    else if (vanished)
        fadeReadoutOut();
}

void PianoRollVelocityLane::fadeReadoutIn() {
    if (!isShowing()) {
        fade_.stop(vblank_);
        setReadoutOpacity(1.0f);
        return;
    }
    const float from = readoutOpacity_;
    fade_.start(vblank_, hint::resumeDurationMs(from, kReadoutFadeInMs), easeOutCubic,
                [this, from](float e) { setReadoutOpacity(hint::tweenUp(from, e)); });
}

void PianoRollVelocityLane::fadeReadoutOut() {
    if (!isShowing() || readoutOpacity_ <= 0.0f) {
        fade_.stop(vblank_);
        setReadoutOpacity(0.0f);
        return;
    }
    const float from = readoutOpacity_;
    fade_.start(vblank_, kReadoutFadeOutMs, easeInCubic,
                [this, from](float e) { setReadoutOpacity(hint::tweenDown(from, e)); });
}

// Repaints the whole strip (64 px tall by default) rather than the union of the old and new box
// rects: the box rides a moving stick head, and one small fixed-height repaint per VBlank is cheap.
void PianoRollVelocityLane::setReadoutOpacity(float value) {
    readoutOpacity_ = juce::jlimit(0.0f, 1.0f, value);
    if (readoutOpacity_ <= 0.0f && !readout_.isValid())
        shown_.reset(); // fully faded out: nothing left to keep painting
    repaint();
}

int PianoRollVelocityLane::gutterWidth() const { return host_.gutterWidth ? host_.gutterWidth() : 0; }

std::vector<PianoRollVelocityLane::Stick> PianoRollVelocityLane::currentSticks() const {
    return host_.sticks ? host_.sticks() : std::vector<Stick>{};
}

} // namespace synth::ui
