// PianoRollVelocityLane — construction, the host seam, and the strip's vertical mapping. Gestures
// live in PianoRollVelocityLaneGestures.cpp and painting in PianoRollVelocityLanePainting.cpp.

#include "PianoRollVelocityLane.h"

#include "VelocityLaneMath.h"
#include <algorithm>

namespace synth::ui {

namespace {
// Room above velocity 127's head and below velocity 1's, so neither head is clipped by the strip's
// edge (the top edge also carries the separator line).
constexpr float kPlotTopPad = 6.0f;
constexpr float kPlotBottomPad = 4.0f;
} // namespace

PianoRollVelocityLane::PianoRollVelocityLane() {
    setComponentID("pianoRollVelocityLane");
    setOpaque(true); // paint() starts with a full-bounds fill
    // Takes focus on a press so Escape reaches keyPressed mid-drag; every other key falls through
    // to the roll, so the roll's own shortcuts keep working after a click in the strip.
    setWantsKeyboardFocus(true);
    setTitle("Velocity strip");
}

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

int PianoRollVelocityLane::gutterWidth() const { return host_.gutterWidth ? host_.gutterWidth() : 0; }

std::vector<PianoRollVelocityLane::Stick> PianoRollVelocityLane::currentSticks() const {
    return host_.sticks ? host_.sticks() : std::vector<Stick>{};
}

} // namespace synth::ui
