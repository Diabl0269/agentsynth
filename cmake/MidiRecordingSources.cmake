# MIDI recording: the recorder that turns a take into clips, and the per-device lanes that let a take record one MIDI
# input. Split out of the root list to keep CMakeLists.txt under the file-size cap.
target_sources(Core PRIVATE
    Source/AudioEngine/MidiInputSources.cpp
    Source/AudioEngine/MidiInputSources.h
    Source/Timeline/MidiRecorder.cpp
    Source/Timeline/MidiRecorder.h
)
