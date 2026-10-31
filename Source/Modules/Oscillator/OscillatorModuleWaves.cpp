// OscillatorModuleWaves.cpp -- the waveform switch. Out of line so the generators stay out of OscillatorModule.h, which
// most of the app includes.

#include "Modules/Oscillator/BandLimitedWaves.h"
#include "Modules/OscillatorModule.h"

float OscillatorModule::generateSample(int waveform, float phase, float dt, float pulseWidth) const {
    switch (waveform) {
    case 0:
        return std::sin(phase * juce::MathConstants<float>::twoPi);
    case 1:
        return synth::bandlimited::square(phase, dt, pulseWidth);
    case 2:
        return synth::bandlimited::saw(phase, dt);
    case 3:
        return synth::bandlimited::triangle(phase, dt);
    default:
        return 0.0f;
    }
}
