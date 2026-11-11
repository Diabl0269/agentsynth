// WavetableOscillatorModuleLiveDisplay.cpp -- what the card's display draws: the Position and Warp the oscillator
// plays now, published by the audio thread once per block, and the drawn frame. The class: WavetableOscillatorModule.h;
// the display: Source/UI/ModuleViews/WavetableDisplayComponent.h; docs/modules/wavetable.md#the-display.

#include "WavetableOscillatorModule.h"

// The audio thread stores the block's last sample of knob plus cable, relaxed: each value is one float the display
// reads on its own, and nothing else is published with it, so no ordering is needed. A negative value means "at
// rest", which the getters answer with the knob, so a knob turn with nothing sounding shows at once rather than a
// block later.
void WavetableOscillatorModule::publishLiveDisplayValues(int cacheLen) {
    const bool live = cacheLen > 0 && displayFollowsSound();
    const int last = std::max(0, cacheLen - 1);
    const float position = live && hasCV(kJackPosition)
                               ? juce::jlimit(0.0f, 1.0f, positionRamp[(size_t)last] + cvAt(kJackPosition, last))
                               : -1.0f;
    const float warp =
        live && hasCV(kJackWarp) ? juce::jlimit(0.0f, 1.0f, warpRamp[(size_t)last] + cvAt(kJackWarp, last)) : -1.0f;
    liveScanPosition.store(position, std::memory_order_relaxed);
    liveWarpAmount.store(warp, std::memory_order_relaxed);
}

// At rest (no note held) the display shows the knob, not the last position that sounded. A note is held while a MIDI
// note is down or any voice's Gate is high. Before any note has started (a drone with no keyboard or gate patched)
// the oscillator itself is the sound, so the display follows it. Poly voices played by a pitch cable with no Gate
// fan count as notes only when they start, so such a patch shows the knob once its first note has started.
bool WavetableOscillatorModule::displayFollowsSound() const {
    if (!noteSeen_ || heldMidiNotes_ > 0)
        return true;
    for (const auto& voice : voices)
        if (voice.gateEdges.previous > GateEdges::kThreshold)
            return true;
    return false;
}

void WavetableOscillatorModule::countHeldMidiNotes(const juce::MidiMessage& msg) {
    if (msg.isNoteOn())
        ++heldMidiNotes_;
    else if (msg.isNoteOff())
        heldMidiNotes_ = std::max(0, heldMidiNotes_ - 1);
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        heldMidiNotes_ = 0;
}

float WavetableOscillatorModule::getModulatedScanPosition() const {
    const float live = liveScanPosition.load(std::memory_order_relaxed);
    return live >= 0.0f ? live : positionParam->get();
}

float WavetableOscillatorModule::getModulatedWarpAmount() const {
    const float live = liveWarpAmount.load(std::memory_order_relaxed);
    return live >= 0.0f ? live : warpAmountParam->get();
}

void WavetableOscillatorModule::getDisplayWaveformAt(std::vector<float>& out, int numPoints, float position,
                                                     float warpAmount) const {
    out.assign((size_t)std::max(2, numPoints), 0.0f);
    const auto* wt = selectedTableForMessageThread();
    if (wt == nullptr || wt->numFrames <= 0)
        return;

    const float posFrames = juce::jlimit(0.0f, 1.0f, position) * (float)(wt->numFrames - 1);
    const int n = (int)out.size();

    // Draw what the oscillator actually plays, warp included: a Warp knob you cannot see move is a knob users do not
    // trust.
    const Warp warp = (Warp)juce::jlimit(0, (int)Warp::Count - 1, warpParam->getIndex());
    const float amount = juce::jlimit(0.0f, 1.0f, warpAmount);
    const bool hermite = interpolationParam->getIndex() == 1;

    for (int i = 0; i < n; ++i)
        out[(size_t)i] = readWarped(*wt, 0, posFrames, (float)i / (float)n, warp, amount, hermite);
}

int WavetableOscillatorModule::getWarpSignature(float warpAmount) const {
    return warpParam->getIndex() * 1024 + (int)std::round(juce::jlimit(0.0f, 1.0f, warpAmount) * 200.0f) * 2 +
           interpolationParam->getIndex();
}
