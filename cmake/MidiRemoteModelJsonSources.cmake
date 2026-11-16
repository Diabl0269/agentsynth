# The MIDI Remote model's JSON dialect, one unit per concern. Split out of the root list to keep CMakeLists.txt under
# the file-size cap.
target_sources(Core PRIVATE
    Source/MidiRemote/RemoteModelJson.cpp
    Source/MidiRemote/RemoteModelJsonMpe.cpp
    Source/MidiRemote/RemoteModelJsonOsc.cpp
    Source/MidiRemote/RemoteModelJsonOsc.h
)
