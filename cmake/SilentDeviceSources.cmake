# The silent audio device (docs/architecture/audio-engine.md#automation-launch-the-silent-device): a real audio thread
# with no hardware, used by an automation launch and by tests.
target_sources(Core PRIVATE
    Source/AudioEngine/SilentAudioDevice.cpp
    Source/AudioEngine/SilentAudioDevice.h
)
