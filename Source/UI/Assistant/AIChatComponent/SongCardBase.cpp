#include "SongCardBase.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth {

SongCardBase::SongCardBase(bool fadeIn)
    : fadePending(fadeIn) {}

SongCardBase::~SongCardBase() { fadeAnim.stop(vblank); }

void SongCardBase::paint(juce::Graphics& g) {
    const auto& c = colours();
    const auto b = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(c.surface);
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(c.border);
    g.drawRoundedRectangle(b, 8.0f, 1.0f);
}

// The fade starts once the card is on screen; not on screen (a test, a hidden panel) or with motion off, it is simply
// there.
void SongCardBase::parentHierarchyChanged() {
    if (!fadePending || !isShowing())
        return;
    fadePending = false;
    if (synth::ui::animationsOff())
        return;
    setAlpha(0.0f); // the first frame is already the first fade frame
    fadeAnim.start(
        vblank, synth::ui::motionMs(160.0, 80.0), synth::ui::easeOutCubic, [this](float t) { setAlpha(t); },
        [this] { setAlpha(1.0f); });
}

} // namespace synth
