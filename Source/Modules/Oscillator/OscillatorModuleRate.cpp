// OscillatorModuleRate.cpp -- the frequency the knobs set: the note's pitch (Follow notes) or the Fixed rate.
// The class and channel map: OscillatorModule.h; the rendering loops: OscillatorModuleMono.cpp,
// OscillatorModulePoly.cpp.

#include "Modules/OscillatorModule.h"

float OscillatorModule::getTargetFrequency(float midiNote) const {
    float totalPitch =
        midiNote + (octaveParam->get() * 12.0f) + (float)coarseParam->get() + (fineParam->get() / 100.0f);
    return 440.0f * std::pow(2.0f, (totalPitch - 69.0f) / 12.0f);
}

// In Fixed mode the note term is dropped and the knobs (and, per sample, their CV) still offset the rate, so
// +1 octave doubles it. Follow notes returns exactly the note frequency.
float OscillatorModule::baseFrequencyHz(float midiNote) const {
    if (!isFixedRate())
        return getTargetFrequency(midiNote);
    const float semitones = (octaveParam->get() * 12.0f) + (float)coarseParam->get() + (fineParam->get() / 100.0f);
    return fixedRateParam->get() * std::pow(2.0f, semitones / 12.0f);
}

// Follow notes keeps the audible floor of 20 Hz; a Fixed rate may be a sub-audio modulation rate.
float OscillatorModule::minRenderHz() const { return isFixedRate() ? 0.1f : 20.0f; }
