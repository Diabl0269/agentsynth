# Recording audio from each track's own input: the capture core shared with the Rec Tap, the engine's input
# recorder and input switch-on, and MainComponent's take list. Split out of the root lists, both at the file-size cap.
target_sources(Core PRIVATE
    Source/Timeline/TakeCaptureWriter.cpp
    Source/Timeline/TakeCaptureWriter.h
    Source/AudioEngine/AudioEngineInputRecording.cpp
    Source/AudioEngine/InputTakeRecorder.cpp
    Source/AudioEngine/InputTakeRecorder.h
    Source/AudioEngine/DeviceInputEnable.cpp
    Source/AudioEngine/DeviceInputEnable.h
)
target_sources(AppUI PRIVATE
    Source/MainComponent/MainComponentAudioTakes.cpp
)
