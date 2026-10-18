# The Oscillator's voice rendering, note tracking and the unison maths it shares with the Wavetable. Part of Core;
# its own list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/Modules/Oscillator/OscillatorModuleMono.cpp
    Source/Modules/Oscillator/OscillatorModuleNotes.cpp
    Source/Modules/Oscillator/OscillatorModulePoly.cpp
    Source/Modules/Oscillator/OscillatorModuleVoicing.cpp
    Source/Modules/Oscillator/HeldNotes.h
    Source/Modules/Oscillator/PitchGlide.h
    Source/Modules/UnisonVoicing.h
)
