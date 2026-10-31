# The Oscillator's voice rendering, note tracking and the unison and restart pieces it shares with the Wavetable
# (plus the Wavetable's own note and voicing units). Part of Core;
# its own list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/Modules/Oscillator/BandLimitedWaves.cpp
    Source/Modules/Oscillator/BandLimitedWaves.h
    Source/Modules/Oscillator/OscillatorModuleMono.cpp
    Source/Modules/Oscillator/OscillatorModuleNotes.cpp
    Source/Modules/Oscillator/OscillatorModulePoly.cpp
    Source/Modules/Oscillator/OscillatorModuleRate.cpp
    Source/Modules/Oscillator/OscillatorModuleVoicing.cpp
    Source/Modules/Oscillator/OscillatorModuleWaves.cpp
    Source/Modules/Oscillator/HeldNotes.h
    Source/Modules/Oscillator/PitchGlide.h
    Source/Modules/UnisonVoicing.h
    Source/Modules/UnisonMix.h
    Source/Modules/RestartGhosts.h
    Source/Modules/WavetableOscillatorModule/WavetableOscillatorModuleNotes.cpp
    Source/Modules/WavetableOscillatorModule/WavetableOscillatorModuleVoicing.cpp
)
